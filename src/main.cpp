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
#include "max7219.h"
#include "lwip/sockets.h"

// ── Pin assignments ───────────────────────────────────────────────────────────
#define PIN_TAP_BUTTON  GPIO_NUM_35
#define PIN_ENC_A       GPIO_NUM_36
#define PIN_ENC_B       GPIO_NUM_39
#define PIN_ENC_SW      GPIO_NUM_4
#define PIN_DISP_CLK    GPIO_NUM_14
#define PIN_DISP_DIN    GPIO_NUM_2
#define PIN_DISP_LOAD   GPIO_NUM_15

// ── Constants ─────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS     5
#define ENC_BPM_STEP    0.1
#define DISPLAY_MS      50
#define MENU_TIMEOUT_MS 3000
#define NUDGE_US        20000
#define OSC_PORT        8000

// ── Time signatures available in the menu ────────────────────────────────────
static const double kSignatures[] = { 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 };
static const int    kSigCount     = sizeof(kSignatures) / sizeof(kSignatures[0]);

// ── Globals ───────────────────────────────────────────────────────────────────
static TapTempo      tapTempo;
static MAX7219Display display(PIN_DISP_CLK, PIN_DISP_DIN, PIN_DISP_LOAD);

static abl_link               s_link;
static abl_link_session_state s_session;
static bool   linkEnabled = false;
static double linkQuantum = 4.0;  // active time signature (beats per bar)

static int      lastButton = 1;
static int      lastEncSw  = 1;
static int      lastEncA   = 1;
static uint32_t lastDebounce = 0;
static uint32_t lastEncSwDb  = 0;
static uint32_t lastEncTurn  = 0;

enum AppMode { MODE_NORMAL, MODE_MENU };
static AppMode  appMode       = MODE_NORMAL;
static uint32_t menuEnteredAt = 0;
static int      menuSelIdx    = 2;  // index into kSignatures; 2 = 4/4 default

// ── Time helpers ──────────────────────────────────────────────────────────────

static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline uint64_t now_us() { return (uint64_t)esp_timer_get_time(); }

// ── Display ───────────────────────────────────────────────────────────────────

// Layout: [BPM hundreds][BPM tens][BPM units.][BPM tenths] [blank] [beat] [blank] [peers]
static void update_display() {
    static uint32_t lastUpdate = 0;
    if (now_ms() - lastUpdate < DISPLAY_MS) return;
    lastUpdate = now_ms();

    uint8_t segs[8] = {};

    if (appMode == MODE_MENU) {
        segs[0] = segs[1] = segs[2] = segs[3] = MAX7219Display::SEG_DASH;
        segs[7] = MAX7219Display::digit((int)kSignatures[menuSelIdx]);
        display.setSegments(segs);
        return;
    }

    if (!linkEnabled) {
        for (int i = 0; i < 8; i++) segs[i] = MAX7219Display::SEG_DASH;
        display.setSegments(segs);
        return;
    }

    abl_link_capture_app_session_state(s_link, s_session);
    double bpm   = abl_link_tempo(s_session);
    double phase = abl_link_phase_at_time(s_session, now_us(), linkQuantum);
    int    peers = (int)abl_link_num_peers(s_link);

    int bpmX10   = (int)round(bpm * 10.0);
    int hundreds = bpmX10 / 1000;
    int tens     = (bpmX10 / 100) % 10;
    int units    = (bpmX10 / 10) % 10;
    int tenths   = bpmX10 % 10;
    int beatInBar = (int)floor(phase) + 1;  // 1-based, 1 to linkQuantum

    segs[0] = hundreds ? MAX7219Display::digit(hundreds) : MAX7219Display::SEG_BLANK;
    segs[1] = (hundreds || tens) ? MAX7219Display::digit(tens) : MAX7219Display::SEG_BLANK;
    segs[2] = MAX7219Display::digit(units) | MAX7219Display::SEG_DP;
    segs[3] = MAX7219Display::digit(tenths);
    // segs[4] blank
    segs[5] = MAX7219Display::digit(beatInBar);
    // segs[6] blank
    segs[7] = MAX7219Display::digit(peers > 9 ? 9 : peers);

    display.setSegments(segs);
}

// ── IP splash ────────────────────────────────────────────────────────────────

static void fill_octet(uint8_t *slots, int val) {
    slots[2] = MAX7219Display::digit(val % 10); val /= 10;
    slots[1] = val ? MAX7219Display::digit(val % 10) : MAX7219Display::SEG_BLANK; val /= 10;
    slots[0] = val ? MAX7219Display::digit(val % 10) : MAX7219Display::SEG_BLANK;
}

static void show_ip_splash() {
    int o[4];
    if (sscanf(ethIPStr, "%d.%d.%d.%d", &o[0], &o[1], &o[2], &o[3]) != 4) return;

    for (int pair = 0; pair < 2; pair++) {
        uint8_t segs[8] = {};
        fill_octet(segs + 1, o[pair * 2]);      // octets 0,2 in slots 1-3
        fill_octet(segs + 5, o[pair * 2 + 1]);  // octets 1,3 in slots 5-7
        display.setSegments(segs);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ── Link helpers ──────────────────────────────────────────────────────────────

static void set_link_tempo(double bpm) {
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, bpm, now_us());
    abl_link_commit_app_session_state(s_link, s_session);
}

static void nudge_phase(int32_t offset_us) {
    if (!linkEnabled) return;
    uint64_t t = now_us();
    abl_link_capture_app_session_state(s_link, s_session);
    double beat = abl_link_beat_at_time(s_session, t, linkQuantum);
    abl_link_force_beat_at_time(s_session, beat, (uint64_t)((int64_t)t - offset_us), linkQuantum);
    abl_link_commit_app_session_state(s_link, s_session);
}

static void reset_downbeat() {
    if (!linkEnabled) return;
    uint64_t t = now_us();
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_force_beat_at_time(s_session, 0.0, t, linkQuantum);
    abl_link_commit_app_session_state(s_link, s_session);
}

static void go_live(double bpm, uint32_t sessionStartMs) {
    abl_link_enable(s_link, true);
    linkEnabled = true;

    uint64_t linkNow    = now_us();
    uint64_t tap1LinkUs = linkNow - ((uint64_t)(now_ms() - sessionStartMs) * 1000ULL);

    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, bpm, linkNow);
    abl_link_force_beat_at_time(s_session, 0.0, tap1LinkUs, linkQuantum);
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
    }

    lastButton = reading;
}

static void handle_encoder() {
    int      a   = gpio_get_level(PIN_ENC_A);
    int      b   = gpio_get_level(PIN_ENC_B);
    int      sw  = gpio_get_level(PIN_ENC_SW);
    uint32_t now = now_ms();

    if (a == 0 && lastEncA == 1 && (now - lastEncTurn) > DEBOUNCE_MS) {
        lastEncTurn = now;
        if (appMode == MODE_MENU) {
            menuSelIdx    = (b == 1)
                            ? (menuSelIdx + 1) % kSigCount
                            : (menuSelIdx + kSigCount - 1) % kSigCount;
            menuEnteredAt = now;  // reset timeout on interaction
        } else if (linkEnabled) {
            double newBpm = tapTempo.bpm() + (b == 1 ? ENC_BPM_STEP : -ENC_BPM_STEP);
            tapTempo.setBpm(newBpm);
            set_link_tempo(tapTempo.bpm());
            printf("Enc: %.1f BPM\n", tapTempo.bpm());
        }
    }
    lastEncA = a;

    if (sw == 0 && lastEncSw == 1 && (now - lastEncSwDb) > DEBOUNCE_MS) {
        lastEncSwDb = now;
        if (appMode == MODE_NORMAL) {
            appMode       = MODE_MENU;
            menuEnteredAt = now;
            for (int i = 0; i < kSigCount; i++) {
                if (kSignatures[i] == linkQuantum) { menuSelIdx = i; break; }
            }
        } else {
            linkQuantum = kSignatures[menuSelIdx];
            appMode     = MODE_NORMAL;
            printf("Time sig: %.0f\n", linkQuantum);
        }
    }
    lastEncSw = sw;
}

static void check_menu_timeout() {
    if (appMode == MODE_MENU && (now_ms() - menuEnteredAt) >= MENU_TIMEOUT_MS) {
        appMode = MODE_NORMAL;
    }
}

// ── OSC server ───────────────────────────────────────────────────────────────

static uint32_t osc_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static float osc_float(const uint8_t *p) {
    uint32_t u = osc_u32(p);
    float f; memcpy(&f, &u, 4); return f;
}

static void osc_handle(const uint8_t *buf, int len) {
    // Null-terminate so string functions are safe
    if (len < 4) return;

    const char *addr = (const char *)buf;
    int addr_bytes   = ((strlen(addr) + 4) & ~3);   // padded length incl. null
    if (addr_bytes >= len) return;

    const char *tags = (const char *)(buf + addr_bytes);
    if (tags[0] != ',') return;
    int tags_bytes = ((strlen(tags) + 4) & ~3);
    const uint8_t *args = buf + addr_bytes + tags_bytes;
    int args_len        = len - addr_bytes - tags_bytes;

    if (strcmp(addr, "/tap") == 0) {
        TapResult r = tapTempo.tap();
        if (r.wentLive) {
            go_live(tapTempo.bpm(), tapTempo.sessionStartMs());
            printf("OSC live: %.1f BPM\n", tapTempo.bpm());
        } else if (r.bpmChanged && linkEnabled) {
            set_link_tempo(tapTempo.bpm());
            printf("OSC tap: %.1f BPM\n", tapTempo.bpm());
        } else if (r.newSession) {
            printf("OSC tap: new session\n");
        }
    } else if (strcmp(addr, "/bpm") == 0 && args_len >= 4) {
        float bpm = (tags[1] == 'f') ? osc_float(args)
                                     : (float)(int32_t)osc_u32(args);
        tapTempo.setBpm(bpm);
        set_link_tempo(tapTempo.bpm());
        printf("OSC bpm: %.1f\n", tapTempo.bpm());
    } else if (strcmp(addr, "/nudge_up") == 0) {
        nudge_phase(NUDGE_US);
        printf("OSC nudge +20ms\n");
    } else if (strcmp(addr, "/nudge_down") == 0) {
        nudge_phase(-NUDGE_US);
        printf("OSC nudge -20ms\n");
    } else if (strcmp(addr, "/downbeat") == 0) {
        reset_downbeat();
        printf("OSC downbeat reset\n");
    } else if (strcmp(addr, "/signature") == 0 && args_len >= 4) {
        int sig = (tags[1] == 'f') ? (int)osc_float(args)
                                   : (int)(int32_t)osc_u32(args);
        for (int i = 0; i < kSigCount; i++) {
            if ((int)kSignatures[i] == sig) { linkQuantum = kSignatures[i]; break; }
        }
        printf("OSC signature: %d\n", sig);
    }
}

static void osc_task(void *) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(nullptr); return; }

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(OSC_PORT);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    static uint8_t buf[256];
    while (true) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n > 0) { buf[n] = 0; osc_handle(buf, n); }
    }
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
    printf("Link: %.2f BPM  sig: %.0f  peers: %llu  eth: %s\n",
           abl_link_tempo(s_session),
           linkQuantum,
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

    // GPIO4 has an internal pull-up (encoder switch).
    gpio_config_t enc_sw_cfg = {
        .pin_bit_mask  = (1ULL << PIN_ENC_SW),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&enc_sw_cfg);
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
    display.setIntensity(8);

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
        show_ip_splash();
    } else {
        printf("No Ethernet — running standalone\n");
    }

    s_link    = abl_link_create(120.0);
    s_session = abl_link_create_session_state();

    tapTempo.setBpm(120.0);
    abl_link_enable(s_link, true);
    linkEnabled = true;
    abl_link_capture_app_session_state(s_link, s_session);
    abl_link_set_tempo(s_session, 120.0, now_us());
    abl_link_commit_app_session_state(s_link, s_session);
    printf("Link live at 120.0 BPM\n");

    xTaskCreate(osc_task, "osc", 4096, nullptr, 5, nullptr);
    printf("OSC listening on port %d\n", OSC_PORT);

    while (true) {
        handle_button();
        handle_encoder();
        check_menu_timeout();
        update_display();
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
