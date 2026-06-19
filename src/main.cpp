#include <stdio.h>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "abl_link.h"
#include "tap_tempo.h"
#include "ethernet_config.h"
#include "tm1637.h"

// ── Pin assignments ───────────────────────────────────────────────────────────
#define PIN_TAP_BUTTON  GPIO_NUM_35
#define PIN_LED         GPIO_NUM_2
#define PIN_NUDGE_PLUS  GPIO_NUM_32
#define PIN_NUDGE_MINUS GPIO_NUM_33
#define PIN_ENC_A       GPIO_NUM_36
#define PIN_ENC_B       GPIO_NUM_39
#define PIN_ENC_SW      GPIO_NUM_4
#define PIN_DISP_CLK    GPIO_NUM_14
#define PIN_DISP_DIO    GPIO_NUM_15

// ── Constants ─────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS     5
#define LINK_QUANTUM    4.0
#define LED_FLASH_MS    20
#define NUDGE_US        20000   // 20ms phase nudge in microseconds
#define ENC_BPM_STEP    0.1
#define DISPLAY_MS      100

// ── Globals ───────────────────────────────────────────────────────────────────
static TapTempo      tapTempo;
static TM1637Display display(PIN_DISP_CLK, PIN_DISP_DIO);

static abl_link              s_link;
static abl_link_session_state s_session;
static bool linkEnabled = false;

static int      lastButton    = 1;
static int      lastNudgePlus = 1;
static int      lastNudgeMinus= 1;
static int      lastEncSw     = 1;
static int      lastEncA      = 1;
static uint32_t lastDebounce      = 0;
static uint32_t lastNudgePlusDb   = 0;
static uint32_t lastNudgeMinusDb  = 0;
static uint32_t lastEncSwDb       = 0;
static uint32_t lastEncTurn       = 0;

static bool     ledOn   = false;
static uint32_t ledOnAt = 0;
static int      lastBeat = -1;

// ── Time helpers ──────────────────────────────────────────────────────────────

static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline uint64_t now_us() { return (uint64_t)esp_timer_get_time(); }

// ── LED ───────────────────────────────────────────────────────────────────────

static void flash_led() {
    gpio_set_level(PIN_LED, 1);
    ledOn   = true;
    ledOnAt = now_ms();
}

static void update_led() {
    if (ledOn && (now_ms() - ledOnAt) >= LED_FLASH_MS) {
        gpio_set_level(PIN_LED, 0);
        ledOn = false;
    }
}

static void beat_flash() {
    if (!linkEnabled) return;

    abl_link_capture_app_session_state(s_link, s_session);
    double beat = abl_link_beat_at_time(s_session, now_us(), LINK_QUANTUM);
    int    b    = (int)floor(beat);

    if (lastBeat == -1) { lastBeat = b; return; }
    if (b != lastBeat)  { lastBeat = b; flash_led(); }
}

// ── Display + peer sync ───────────────────────────────────────────────────────

static void update_display() {
    static uint32_t lastUpdate = 0;
    if (now_ms() - lastUpdate < DISPLAY_MS) return;
    lastUpdate = now_ms();

    if (!linkEnabled) {
        const uint8_t dashes[] = {TM1637Display::SEG_G, TM1637Display::SEG_G,
                                   TM1637Display::SEG_G, TM1637Display::SEG_G};
        display.setSegments(dashes);
        return;
    }

    // Display the actual Link session tempo. Never write network state back
    // into tapTempo — that creates a feedback path where peer messages
    // (including reflections of our own) corrupt the local tempo reference.
    abl_link_capture_app_session_state(s_link, s_session);
    int val = (int)round(abl_link_tempo(s_session) * 10.0);
    display.showNumberDecEx(val, 0x20, false, 4, 0);
}

// ── Link helpers ──────────────────────────────────────────────────────────────

static void set_link_tempo(double bpm) {
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, bpm, now_us());
    abl_link_commit_app_session_state(s_link, s_session);
}

static void go_live(double bpm, uint32_t sessionStartMs) {
    abl_link_enable(s_link, true);
    linkEnabled = true;

    uint64_t linkNow    = now_us();
    uint64_t tap1LinkUs = linkNow - ((uint64_t)(now_ms() - sessionStartMs) * 1000ULL);

    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, bpm, linkNow);
    abl_link_force_beat_at_time(s_session, 0.0, tap1LinkUs, LINK_QUANTUM);
    abl_link_commit_app_session_state(s_link, s_session);
}

// offsetUs > 0 advances the timeline (beats come sooner); < 0 delays it.
static void nudge_phase(int32_t offset_us) {
    if (!linkEnabled) return;
    uint64_t t = now_us();
    abl_link_capture_app_session_state(s_link, s_session);
    double beat = abl_link_beat_at_time(s_session, t, LINK_QUANTUM);
    abl_link_force_beat_at_time(s_session, beat, (uint64_t)((int64_t)t - offset_us), LINK_QUANTUM);
    abl_link_commit_app_session_state(s_link, s_session);
}

static void reset_downbeat() {
    if (!linkEnabled) return;
    uint64_t t = now_us();
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_force_beat_at_time(s_session, 0.0, t, LINK_QUANTUM);
    abl_link_commit_app_session_state(s_link, s_session);
}

// ── Input handlers ────────────────────────────────────────────────────────────

static void handle_button() {
    int      reading = gpio_get_level(PIN_TAP_BUTTON);
    uint32_t now     = now_ms();

    if (reading == 0 && lastButton == 1 && (now - lastDebounce) > DEBOUNCE_MS) {
        lastDebounce = now;

        TapResult r = tapTempo.tap();

        if (r.wentLive) {
            go_live(tapTempo.bpm(), tapTempo.sessionStartMs());
            printf("Live: %.1f BPM\n", tapTempo.bpm());
        } else if (r.bpmChanged && linkEnabled) {
            set_link_tempo(tapTempo.bpm());
            printf("Tap: %.1f BPM  peers: %llu\n", tapTempo.bpm(),
                   (unsigned long long)abl_link_num_peers(s_link));
        } else if (r.newSession) {
            printf("New session — tap 3 more times\n");
        }

        if (!linkEnabled) flash_led();
    }

    lastButton = reading;
}

static void handle_nudge() {
    int      plus  = gpio_get_level(PIN_NUDGE_PLUS);
    int      minus = gpio_get_level(PIN_NUDGE_MINUS);
    uint32_t now   = now_ms();

    if (plus == 0 && lastNudgePlus == 1 && (now - lastNudgePlusDb) > DEBOUNCE_MS) {
        lastNudgePlusDb = now;
        nudge_phase(NUDGE_US);
        printf("Nudge +20ms\n");
    }
    lastNudgePlus = plus;

    if (minus == 0 && lastNudgeMinus == 1 && (now - lastNudgeMinusDb) > DEBOUNCE_MS) {
        lastNudgeMinusDb = now;
        nudge_phase(-NUDGE_US);
        printf("Nudge -20ms\n");
    }
    lastNudgeMinus = minus;
}

static void handle_encoder() {
    int      a   = gpio_get_level(PIN_ENC_A);
    int      b   = gpio_get_level(PIN_ENC_B);
    int      sw  = gpio_get_level(PIN_ENC_SW);
    uint32_t now = now_ms();

    if (a == 0 && lastEncA == 1 && (now - lastEncTurn) > DEBOUNCE_MS) {
        lastEncTurn = now;
        if (linkEnabled) {
            double newBpm = tapTempo.bpm() + (b == 1 ? ENC_BPM_STEP : -ENC_BPM_STEP);
            tapTempo.setBpm(newBpm);
            set_link_tempo(tapTempo.bpm());
            printf("Enc: %.1f BPM\n", tapTempo.bpm());
        }
    }
    lastEncA = a;

    if (sw == 0 && lastEncSw == 1 && (now - lastEncSwDb) > DEBOUNCE_MS) {
        lastEncSwDb = now;
        reset_downbeat();
        printf("Downbeat reset\n");
    }
    lastEncSw = sw;
}

// ── Status ────────────────────────────────────────────────────────────────────

static void print_status() {
    static uint32_t lastPrint = 0;
    if (now_ms() - lastPrint < 2000) return;
    lastPrint = now_ms();

    if (!linkEnabled) {
        printf("Cold start — tap 4 times to go live\n");
        return;
    }

    abl_link_capture_app_session_state(s_link, s_session);
    printf("Link: %.2f BPM  peers: %llu  eth: %s\n",
           abl_link_tempo(s_session),
           (unsigned long long)abl_link_num_peers(s_link),
           ethConnected ? ethIPStr : "disconnected");
}

// ── GPIO init ─────────────────────────────────────────────────────────────────

static void init_gpio() {
    // GPIO35/36/39 are input-only with no internal pull-up — requires external
    // 10kΩ pull-up resistors to 3.3V on TAP_BUTTON, ENC_A, and ENC_B.
    const gpio_num_t inputs_no_pullup[] = {
        PIN_TAP_BUTTON, PIN_ENC_A, PIN_ENC_B
    };
    for (gpio_num_t pin : inputs_no_pullup) {
        gpio_config_t cfg = {
            .pin_bit_mask  = (1ULL << pin),
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    // GPIO32, GPIO33, GPIO4 have internal pull-ups.
    const gpio_num_t inputs_pullup[] = {
        PIN_NUDGE_PLUS, PIN_NUDGE_MINUS, PIN_ENC_SW
    };
    for (gpio_num_t pin : inputs_pullup) {
        gpio_config_t cfg = {
            .pin_bit_mask  = (1ULL << pin),
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_ENABLE,
            .pull_down_en  = GPIO_PULLDOWN_DISABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << PIN_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(PIN_LED, 0);
}

// ── Entry point ───────────────────────────────────────────────────────────────

extern "C" void app_main(void) {
    // NVS — required by the networking stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    esp_netif_init();

    init_gpio();

    display.init();
    display.setBrightness(7);

    initEthernet();

    printf("Waiting for Ethernet");
    uint32_t start = now_ms();
    while (!ethConnected && (now_ms() - start) < 10000) {
        vTaskDelay(pdMS_TO_TICKS(250));
        printf(".");
        fflush(stdout);
    }
    printf("\n");

    if (ethConnected) {
        printf("IP: %s\n", ethIPStr);
    } else {
        printf("No Ethernet — running standalone\n");
    }

    s_link    = abl_link_create(120.0);
    s_session = abl_link_create_session_state();

    // Always start on Link at 120 BPM; tapping overrides the tempo once live.
    tapTempo.setBpm(120.0);
    abl_link_enable(s_link, true);
    linkEnabled = true;
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, 120.0, now_us());
    abl_link_commit_app_session_state(s_link, s_session);
    printf("Link live at 120.0 BPM\n");

    // Main polling loop
    while (true) {
        handle_button();
        handle_nudge();
        handle_encoder();
        beat_flash();
        update_led();
        update_display();
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
