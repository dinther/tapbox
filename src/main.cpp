#include <stdio.h>
#include <cmath>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "abl_link.h"
#include "tap_tempo.h"
#include "ethernet_config.h"
#include "max7219.h"
#include "lwip/sockets.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

// ── Pin assignments ────────────────────────────────────────────────────────────
#define PIN_TAP_BUTTON  GPIO_NUM_12
#define PIN_SELECT      GPIO_NUM_4
#define PIN_DISP_CLK    GPIO_NUM_14
#define PIN_DISP_DIN    GPIO_NUM_2
#define PIN_DISP_LOAD   GPIO_NUM_15
#define PIN_BATT_ADC    GPIO_NUM_36  // ADC1_CH0

// ── Constants ──────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS     5
#define DISPLAY_MS      50
#define MENU_TIMEOUT_MS 6000
#define LONG_PRESS_MS   2000
#define SELECT_LONG_MS  1000
#define OSC_PORT        8000
#define OTA_URL         "https://dinther.github.io/tapbox/firmware.bin"

// ── 7-segment characters (D6=A … D0=G) ────────────────────────────────────────
static constexpr uint8_t CH_a = 0x7D;
static constexpr uint8_t CH_A = 0x77;
static constexpr uint8_t CH_b = 0x1F;
static constexpr uint8_t CH_B = 0x7F;
static constexpr uint8_t CH_c = 0x0D;
static constexpr uint8_t CH_d = 0x3D;
static constexpr uint8_t CH_e = 0x6F;
static constexpr uint8_t CH_g = 0x7B;
static constexpr uint8_t CH_h = 0x17;
static constexpr uint8_t CH_H = 0x37;
static constexpr uint8_t CH_i = 0x10;
static constexpr uint8_t CH_I = 0x30;
static constexpr uint8_t CH_L = 0x0E;
static constexpr uint8_t CH_o = 0x1D;
static constexpr uint8_t CH_n = 0x15;
static constexpr uint8_t CH_P = 0x67;
static constexpr uint8_t CH_r = 0x05;
static constexpr uint8_t CH_S = 0x5B;
static constexpr uint8_t CH_t = 0x0F;
static constexpr uint8_t CH_u = 0x1C;
static constexpr uint8_t CH_F = 0x47;  // segments A,E,F,G

// ── Firmware version ───────────────────────────────────────────────────────────
#define FW_MAJOR 1
#define FW_MINOR 3
#define FW_PATCH 0

// ── Menu option tables ─────────────────────────────────────────────────────────
static const double kSignatures[] = { 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 };
static const int    kSigCount     = 6;

static const double kAccStep[]    = { 1.0, 0.5, 0.1 };   // Lo, Std, Hi
static const int    kAccNudgeMs[] = { 50,  20,  5   };   // Lo, Std, Hi
static const int    kAccCount     = 3;

// ── Persistent settings ────────────────────────────────────────────────────────
static int g_sigIdx  = 2;              // kSignatures[2] = 4/4
static int g_accIdx  = 1;              // kAccStep[1]    = Std
static int g_brit    = 7;              // brightness 1–15
static int g_net     = 0;              // 0=DHCP, 1=static
static int g_ip[4]   = {192, 168,   1, 200};
static int g_sn[4]   = {255, 255, 255,   0};
static int g_gt[4]   = {192, 168,   1,   1};
static char g_wifi_ssid[64] = {};
static char g_wifi_pass[64] = {};

// ── Runtime values derived from settings ──────────────────────────────────────
static double g_bpmStep = 0.1;
static int    g_nudgeUs = 20000;

// ── Core globals ───────────────────────────────────────────────────────────────
static TapTempo                  tapTempo;
static MAX7219Display            display(PIN_DISP_CLK, PIN_DISP_DIN, PIN_DISP_LOAD);
static adc_oneshot_unit_handle_t s_adc1 = nullptr;

static abl_link               s_link;
static abl_link_session_state s_session;
static bool   linkEnabled = false;
static double linkQuantum = 4.0;

static int      lastButton      = 1;
static uint32_t lastDebounce    = 0;
static uint32_t s_btnPressedAt  = 0;
static bool     s_btnLongFired  = false;
static uint32_t s_btnAutoIncrAt = 0;

static int      lastSelect      = 1;
static uint32_t lastSelectDb    = 0;
static uint32_t s_selPressedAt  = 0;
static bool     s_selLongFired  = false;

static bool     g_wifi_enabled  = false;
static bool     g_wifi_as_ap    = false;
static bool     g_wifi_got_ip   = false;
static bool     g_eth_got_ip    = false;  // Ethernet came back after being down
static bool     g_eth_lost      = false;  // Ethernet link dropped

// ── Menu state ─────────────────────────────────────────────────────────────────
enum AppMode { MODE_NORMAL, MODE_MENU_NAV, MODE_MENU_EDIT, MODE_MENU_CONFIRM,
               MODE_SUBMENU_NAV, MODE_SUBMENU_EDIT };
static AppMode  appMode       = MODE_NORMAL;
static uint32_t menuEnteredAt = 0;

enum MenuIdx {
    MENU_SIG = 0, MENU_ACC, MENU_BRIT,
    MENU_NET, MENU_IP, MENU_SN, MENU_GT,
    MENU_RESET, MENU_VER, MENU_BAT, MENU_DONE,
    MENU_COUNT  // 11
};
static int menuItem    = 0;
static int menuEditVal = 0;
static int menuSubItem = 0;  // active octet (0–3) when in sub-menu

// ── Time helpers ───────────────────────────────────────────────────────────────
static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline uint64_t now_us() { return (uint64_t)esp_timer_get_time(); }

static void show_boot_reboot();  // forward declaration
static void nvs_save_wifi();     // forward declaration
static void wifi_init();         // forward declaration
static void wifi_stop();         // forward declaration

static void eth_lost_cb(void *, esp_event_base_t, int32_t, void *) {
    g_eth_lost = true;
}
static void eth_got_ip_cb(void *, esp_event_base_t, int32_t, void *) {
    g_eth_got_ip = true;
}

// ── Settings ───────────────────────────────────────────────────────────────────

static void apply_settings() {
    linkQuantum = kSignatures[g_sigIdx];
    g_bpmStep   = kAccStep[g_accIdx];
    g_nudgeUs   = kAccNudgeMs[g_accIdx] * 1000;
    display.setIntensity(g_brit);
}

static void nvs_load_settings() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK) {
        apply_settings();
        return;
    }
    int32_t v;
    if (nvs_get_i32(h, "sig",  &v) == ESP_OK && v >= 0 && v < kSigCount)  g_sigIdx = (int)v;
    if (nvs_get_i32(h, "acc",  &v) == ESP_OK && v >= 0 && v < kAccCount)  g_accIdx = (int)v;
    if (nvs_get_i32(h, "brit", &v) == ESP_OK && v >= 1 && v <= 15)        g_brit   = (int)v;
    if (nvs_get_i32(h, "net",  &v) == ESP_OK && v >= 0 && v <= 1)          g_net     = (int)v;
    if (nvs_get_i32(h, "ip0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[0]   = (int)v;
    if (nvs_get_i32(h, "ip1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[1]   = (int)v;
    if (nvs_get_i32(h, "ip2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[2]   = (int)v;
    if (nvs_get_i32(h, "ip3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[3]   = (int)v;
    if (nvs_get_i32(h, "sn0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[0]   = (int)v;
    if (nvs_get_i32(h, "sn1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[1]   = (int)v;
    if (nvs_get_i32(h, "sn2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[2]   = (int)v;
    if (nvs_get_i32(h, "sn3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[3]   = (int)v;
    if (nvs_get_i32(h, "gt0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[0]   = (int)v;
    if (nvs_get_i32(h, "gt1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[1]   = (int)v;
    if (nvs_get_i32(h, "gt2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[2]   = (int)v;
    if (nvs_get_i32(h, "gt3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[3]   = (int)v;
    nvs_close(h);
    apply_settings();
}

static void nvs_save_settings() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "sig",  (int32_t)g_sigIdx);
    nvs_set_i32(h, "acc",  (int32_t)g_accIdx);
    nvs_set_i32(h, "brit", (int32_t)g_brit);
    nvs_set_i32(h, "net",  (int32_t)g_net);
    nvs_set_i32(h, "ip0",  (int32_t)g_ip[0]);
    nvs_set_i32(h, "ip1",  (int32_t)g_ip[1]);
    nvs_set_i32(h, "ip2",  (int32_t)g_ip[2]);
    nvs_set_i32(h, "ip3",  (int32_t)g_ip[3]);
    nvs_set_i32(h, "sn0",  (int32_t)g_sn[0]);
    nvs_set_i32(h, "sn1",  (int32_t)g_sn[1]);
    nvs_set_i32(h, "sn2",  (int32_t)g_sn[2]);
    nvs_set_i32(h, "sn3",  (int32_t)g_sn[3]);
    nvs_set_i32(h, "gt0",  (int32_t)g_gt[0]);
    nvs_set_i32(h, "gt1",  (int32_t)g_gt[1]);
    nvs_set_i32(h, "gt2",  (int32_t)g_gt[2]);
    nvs_set_i32(h, "gt3",  (int32_t)g_gt[3]);
    nvs_commit(h);
    nvs_close(h);
}

static void factory_reset() {
    g_sigIdx = 2; g_accIdx = 1; g_brit = 7;
    g_net = 0;
    g_ip[0] = 192; g_ip[1] = 168; g_ip[2] =   1; g_ip[3] = 200;
    g_sn[0] = 255; g_sn[1] = 255; g_sn[2] = 255; g_sn[3] =   0;
    g_gt[0] = 192; g_gt[1] = 168; g_gt[2] =   1; g_gt[3] =   1;
    g_wifi_ssid[0] = '\0';
    g_wifi_pass[0] = '\0';
    nvs_save_settings();
    nvs_save_wifi();
    apply_settings();
    // Erase OTA data so the bootloader returns to the factory partition.
    // Guard: only if a factory partition exists — devices that were upgraded via OTA
    // (never reprogrammed) have no factory partition and must not be left bootless.
    const esp_partition_t *factory_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory_part) {
        const esp_partition_t *otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
        if (otadata) esp_partition_erase_range(otadata, 0, otadata->size);
    }
    show_boot_reboot();
}

// ── Menu value helpers ─────────────────────────────────────────────────────────

static bool menu_item_visible(int item) {
    if (item == MENU_IP || item == MENU_SN || item == MENU_GT) return g_net == 1;
    return true;
}

static int menu_get_val(int item) {
    switch (item) {
        case MENU_SIG:  return g_sigIdx;
        case MENU_ACC:  return g_accIdx;
        case MENU_BRIT: return g_brit;
        case MENU_NET:  return g_net;
        default: return 0;
    }
}

static int menu_val_min(int item) { return (item == MENU_BRIT) ? 1 : 0; }

static int menu_val_max(int item) {
    switch (item) {
        case MENU_SIG:  return kSigCount - 1;
        case MENU_ACC:  return kAccCount - 1;
        case MENU_BRIT: return 15;
        case MENU_NET:  return 1;
        default: return 0;
    }
}

static void menu_commit(int item, int val) {
    switch (item) {
        case MENU_SIG:  g_sigIdx = val; break;
        case MENU_ACC:  g_accIdx = val; break;
        case MENU_BRIT: g_brit   = val; break;
        case MENU_NET:  g_net    = val; break;
    }
}

static int *submenu_array() {
    return (menuItem == MENU_IP) ? g_ip : (menuItem == MENU_SN) ? g_sn : g_gt;
}

// ── Display ────────────────────────────────────────────────────────────────────

// Digit segment bytes for use in static label arrays: 0=0x7E 1=0x30 2=0x6D 3=0x79 4=0x33
static const uint8_t kMenuLabels[MENU_COUNT][4] = {
    { CH_B, CH_e, CH_a, CH_t                                       },  // Beat
    { CH_A, CH_c, CH_c | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK },  // Acc.
    { CH_L, CH_e, CH_d, MAX7219Display::SEG_BLANK                  },  // Led
    { CH_L, CH_a, CH_n | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK },  // Lan.
    { CH_I, CH_P, MAX7219Display::SEG_BLANK, MAX7219Display::SEG_BLANK     },  // IP
    { CH_S, CH_u, CH_b | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK },  // Sub.
    { CH_H, CH_u, CH_b | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK },  // Hub.
    { CH_r, CH_S, CH_e, CH_t                                          },  // rSet
    { CH_u, CH_e, CH_r, MAX7219Display::SEG_BLANK                  },  // vEr  (CH_u renders as 'v')
    { CH_b, CH_A, CH_t, MAX7219Display::SEG_BLANK },  // bAt
    { CH_d, CH_o, CH_n, CH_e                      },  // done
};

static int read_battery_pct() {
    if (!s_adc1) return 0;

    // Average 16 samples to reduce ADC noise
    int32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc1, ADC_CHANNEL_0, &raw);
        sum += raw;
    }
    // 100k/100k divider, ADC_ATTEN_DB_12 full scale ≈ 3900 mV at raw 4095
    uint32_t batt_mv = (uint32_t)(sum / 16) * 7800 / 4095;

    // EMA to damp display jitter (~0.8 s time constant at 50 ms update rate)
    static int32_t filtered_mv = 0;
    static bool    initialized  = false;
    if (!initialized) { filtered_mv = (int32_t)batt_mv; initialized = true; }
    else               { filtered_mv = (filtered_mv * 15 + (int32_t)batt_mv) / 16; }

    uint32_t fmv = (uint32_t)filtered_mv;
    static const uint32_t v[]   = {3000, 3400, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
    static const int      soc[] = {   0,   10,   20,   35,   50,   65,   80,   90,  100};

    if (fmv <= v[0]) return 0;
    if (fmv >= v[8]) return 100;
    for (int i = 0; i < 8; i++) {
        if (fmv < v[i + 1])
            return soc[i] + (int)((fmv - v[i]) * (soc[i + 1] - soc[i]) / (v[i + 1] - v[i]));
    }
    return 100;
}

static void render_int3(uint8_t *segs, int val) {
    int h = val / 100, t = (val / 10) % 10, u = val % 10;
    segs[5] = h       ? MAX7219Display::digit(h) : MAX7219Display::SEG_BLANK;
    segs[6] = (h||t)  ? MAX7219Display::digit(t) : MAX7219Display::SEG_BLANK;
    segs[7] = MAX7219Display::digit(u);
}

static void render_menu_value(uint8_t *segs, int item, int val) {
    switch (item) {
        case MENU_SIG:
            segs[5] = segs[6] = MAX7219Display::SEG_BLANK;
            segs[7] = MAX7219Display::digit((int)kSignatures[val]);
            break;
        case MENU_ACC:
            if (val == 1) {
                segs[5] = CH_S; segs[6] = CH_t; segs[7] = CH_d;  // Std
            } else {
                segs[5] = MAX7219Display::SEG_BLANK;
                segs[6] = (val == 0) ? CH_L : CH_h;
                segs[7] = (val == 0) ? CH_o : CH_i;               // Lo / Hi
            }
            break;
        case MENU_BRIT:
            render_int3(segs, val);
            break;
        case MENU_NET:
            // 4-char value fills positions 4-7, overriding the blank separator
            if (val == 0) {
                segs[4] = CH_A; segs[5] = CH_u; segs[6] = CH_t; segs[7] = CH_o;
            } else {
                segs[4] = CH_S; segs[5] = CH_t; segs[6] = CH_a; segs[7] = CH_t;
            }
            break;
        case MENU_IP: case MENU_SN: case MENU_GT:
            segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
            break;
        case MENU_RESET:
            segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
            break;
        case MENU_VER:
            segs[5] = MAX7219Display::digit(FW_MAJOR) | MAX7219Display::SEG_DP;
            segs[6] = MAX7219Display::digit(FW_MINOR) | MAX7219Display::SEG_DP;
            segs[7] = MAX7219Display::digit(FW_PATCH);
            break;
        case MENU_BAT:
            render_int3(segs, read_battery_pct());
            break;
        case MENU_DONE:
            break;
    }
}

static void update_display() {
    static uint32_t lastUpdate = 0;
    if (now_ms() - lastUpdate < DISPLAY_MS) return;
    lastUpdate = now_ms();

    uint8_t segs[8] = {};

    if (appMode == MODE_NORMAL) {
        if (!linkEnabled) {
            for (int i = 0; i < 8; i++) segs[i] = MAX7219Display::SEG_DASH;
        } else {
            abl_link_capture_app_session_state(s_link, s_session);
            double bpm   = abl_link_tempo(s_session);
            double phase = abl_link_phase_at_time(s_session, now_us(), linkQuantum);
            int    peers = (int)abl_link_num_peers(s_link);

            int bpmX10   = (int)round(bpm * 10.0);
            int hundreds = bpmX10 / 1000;
            int tens     = (bpmX10 / 100) % 10;
            int units    = (bpmX10 / 10)  % 10;
            int tenths   = bpmX10 % 10;

            segs[0] = hundreds           ? MAX7219Display::digit(hundreds) : MAX7219Display::SEG_BLANK;
            segs[1] = (hundreds || tens) ? MAX7219Display::digit(tens)     : MAX7219Display::SEG_BLANK;
            segs[2] = MAX7219Display::digit(units) | MAX7219Display::SEG_DP;
            segs[3] = MAX7219Display::digit(tenths);
            segs[5] = MAX7219Display::digit((int)floor(phase) + 1);
            segs[7] = MAX7219Display::digit(peers > 9 ? 9 : peers);
        }
        display.setSegments(segs);
        return;
    }

    if (appMode == MODE_MENU_CONFIRM) {
        segs[0] = CH_r; segs[1] = CH_S; segs[2] = CH_e; segs[3] = CH_t;
        segs[4] = CH_S; segs[5] = CH_u; segs[6] = CH_r; segs[7] = CH_e;
        display.setSegments(segs);
        return;
    }

    if (appMode == MODE_SUBMENU_NAV || appMode == MODE_SUBMENU_EDIT) {
        if (menuSubItem == 4) {
            segs[0] = CH_d; segs[1] = CH_o; segs[2] = CH_n; segs[3] = CH_e;
        } else {
            segs[0] = MAX7219Display::digit(0);  // O
            segs[1] = CH_c;
            segs[2] = CH_t;
            segs[3] = MAX7219Display::digit(menuSubItem + 1);
            int  val   = (appMode == MODE_SUBMENU_EDIT) ? menuEditVal : submenu_array()[menuSubItem];
            bool blank = (appMode == MODE_SUBMENU_EDIT) && ((now_ms() / 250) & 1);
            if (!blank) render_int3(segs, val);
        }
        display.setSegments(segs);
        return;
    }

    // MODE_MENU_NAV or MODE_MENU_EDIT
    memcpy(segs, kMenuLabels[menuItem], 4);

    int  val   = (appMode == MODE_MENU_EDIT) ? menuEditVal : menu_get_val(menuItem);
    bool blank = (appMode == MODE_MENU_EDIT) && ((now_ms() / 250) & 1);
    if (!blank) render_menu_value(segs, menuItem, val);

    display.setSegments(segs);
}

// ── OTA update ────────────────────────────────────────────────────────────────

static const uint8_t kOtaLabel[4] = {
    CH_u, CH_P, CH_d | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK  // UPd.
};

static void perform_ota() {
    uint8_t segs[8] = {};
    memcpy(segs, kOtaLabel, 4);
    segs[4] = segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
    display.setSegments(segs);

    auto show_err = [&]() {
        memcpy(segs, kOtaLabel, 4);
        segs[4] = segs[5] = MAX7219Display::SEG_BLANK;
        segs[6] = CH_e; segs[7] = CH_r;
        display.setSegments(segs);
        vTaskDelay(pdMS_TO_TICKS(3000));
        appMode       = MODE_MENU_NAV;
        menuEnteredAt = now_ms();
    };

    esp_http_client_config_t http_cfg = {};
    http_cfg.url               = OTA_URL;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.timeout_ms        = 30000;
    http_cfg.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t h = nullptr;
    if (esp_https_ota_begin(&ota_cfg, &h) != ESP_OK) { show_err(); return; }

    int total = esp_https_ota_get_image_size(h);
    esp_err_t err;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (total > 0) {
            int pct = esp_https_ota_get_image_len_read(h) * 100 / total;
            memcpy(segs, kOtaLabel, 4);
            render_int3(segs, pct);
            display.setSegments(segs);
        }
    }

    if (err == ESP_OK && esp_https_ota_is_complete_data_received(h) &&
        esp_https_ota_finish(h) == ESP_OK) {
        memcpy(segs, kOtaLabel, 4);
        segs[4] = CH_d; segs[5] = CH_o; segs[6] = CH_n; segs[7] = CH_e;
        display.setSegments(segs);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        esp_https_ota_abort(h);
        show_err();
    }
}

// ── Boot reboot display ────────────────────────────────────────────────────────

static void show_boot_reboot() {
    uint8_t segs[8] = {};
    segs[0] = CH_b;
    segs[1] = MAX7219Display::digit(0);  // O
    segs[2] = MAX7219Display::digit(0);  // O
    segs[3] = CH_t;
    display.setSegments(segs);
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

// ── IP splash ──────────────────────────────────────────────────────────────────

static void fill_octet(uint8_t *slots, int val) {
    slots[2] = MAX7219Display::digit(val % 10); val /= 10;
    slots[1] = val ? MAX7219Display::digit(val % 10) : MAX7219Display::SEG_BLANK; val /= 10;
    slots[0] = val ? MAX7219Display::digit(val % 10) : MAX7219Display::SEG_BLANK;
}

static void show_ip_splash(const char *ip) {
    int o[4];
    if (sscanf(ip, "%d.%d.%d.%d", &o[0], &o[1], &o[2], &o[3]) != 4) return;
    for (int pair = 0; pair < 2; pair++) {
        uint8_t segs[8] = {};
        fill_octet(segs + 1, o[pair * 2]);
        fill_octet(segs + 5, o[pair * 2 + 1]);
        display.setSegments(segs);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ── WiFi & HTTP config server ──────────────────────────────────────────────────

static bool           s_wifi_joined      = false;
static bool           g_sta_failed       = false;
static bool           g_show_ap_splash   = false;
static char           g_wifi_ip_str[16]  = {};
static uint32_t       s_wifi_start_ms    = 0;
static bool           g_wifi_initialized = false;
static esp_netif_t   *s_ap_netif         = nullptr;
static esp_netif_t   *s_sta_netif        = nullptr;
static httpd_handle_t s_httpd            = nullptr;

static void nvs_save_wifi() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifi_ssid", g_wifi_ssid);
    nvs_set_str(h, "wifi_pass", g_wifi_pass);
    nvs_commit(h);
    nvs_close(h);
}

static void url_decode(char *s) {
    char *d = s;
    while (*s) {
        if (*s == '+') { *d++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            unsigned v = 0; sscanf(s + 1, "%2x", &v);
            *d++ = (char)v; s += 3;
        } else { *d++ = *s++; }
    }
    *d = '\0';
}

static bool form_field(const char *body, const char *key, char *out, int out_size) {
    char search[32]; snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '&');
    int len = end ? (int)(end - p) : (int)strlen(p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len); out[len] = '\0';
    url_decode(out);
    return true;
}

static esp_err_t http_get_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    char tmp[128];

    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>tapbox</title><style>"
        "*{box-sizing:border-box}"
        "body{font:15px sans-serif;max-width:400px;margin:20px auto;padding:0 15px}"
        "h3{margin:14px 0 4px}"
        "label{display:block;margin:6px 0 1px;font-size:13px;color:#555}"
        "input,select{width:100%;padding:7px;border:1px solid #ccc;border-radius:3px}"
        "button{width:100%;padding:11px;margin-top:10px;background:#07c;color:#fff;"
        "border:none;border-radius:3px;font-size:15px;cursor:pointer}"
        "#sb{display:none}"
        "</style></head><body><h2>tapbox</h2>"
        "<form method=post action=/save>"
        "<h3>WiFi</h3>"
        "<label>Network name (SSID)</label>");
    snprintf(tmp, sizeof(tmp), "<input name=ssid value=\"%s\">", g_wifi_ssid);
    httpd_resp_sendstr_chunk(req, tmp);
    httpd_resp_sendstr_chunk(req,
        "<label>Password</label>"
        "<input name=pass type=password placeholder=\"(leave blank to keep current)\">"
        "<h3>Ethernet</h3><label>Mode</label>"
        "<select name=net"
        " onchange=\"document.getElementById('sb').style.display="
        "this.value==='1'?'block':'none'\">");
    snprintf(tmp, sizeof(tmp),
        "<option value=0%s>DHCP</option><option value=1%s>Static</option></select>",
        g_net == 0 ? " selected" : "", g_net == 1 ? " selected" : "");
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<div id=sb><label>IP</label><input name=ip value=\"%d.%d.%d.%d\">",
        g_ip[0], g_ip[1], g_ip[2], g_ip[3]);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<label>Subnet</label><input name=sn value=\"%d.%d.%d.%d\">",
        g_sn[0], g_sn[1], g_sn[2], g_sn[3]);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<label>Gateway</label><input name=gw value=\"%d.%d.%d.%d\"></div>",
        g_gt[0], g_gt[1], g_gt[2], g_gt[3]);
    httpd_resp_sendstr_chunk(req, tmp);

    httpd_resp_sendstr_chunk(req,
        "<h3>Display</h3><label>Time signature</label><select name=sig>");
    static const char *sigs[] = {"2","3","4","5","6","7"};
    for (int i = 0; i < 6; i++) {
        snprintf(tmp, sizeof(tmp), "<option value=%d%s>%s</option>",
            i, i == g_sigIdx ? " selected" : "", sigs[i]);
        httpd_resp_sendstr_chunk(req, tmp);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
    snprintf(tmp, sizeof(tmp),
        "<label>Brightness (1-15)</label>"
        "<input name=brit type=number min=1 max=15 value=%d>", g_brit);
    httpd_resp_sendstr_chunk(req, tmp);
    httpd_resp_sendstr_chunk(req, "<label>Accuracy</label><select name=acc>");
    static const char *accs[] = {"Low","Standard","High"};
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "<option value=%d%s>%s</option>",
            i, i == g_accIdx ? " selected" : "", accs[i]);
        httpd_resp_sendstr_chunk(req, tmp);
    }
    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<button>Save &amp; reboot</button></form>"
        "<script>"
        "var s=document.querySelector('[name=net]');"
        "if(s.value==='1')document.getElementById('sb').style.display='block'"
        "</script></body></html>");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

static esp_err_t http_post_save(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request");
        return ESP_FAIL;
    }
    char *body = (char *)malloc(total + 1);
    if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return ESP_FAIL; }
    body[received] = '\0';

    char tmp[64], ip_str[20];
    if (form_field(body, "ssid", tmp, sizeof(tmp)) && tmp[0])
        snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", tmp);
    if (form_field(body, "pass", tmp, sizeof(tmp)) && tmp[0])
        snprintf(g_wifi_pass, sizeof(g_wifi_pass), "%s", tmp);
    if (form_field(body, "net",  tmp, sizeof(tmp))) g_net = atoi(tmp);
    if (form_field(body, "ip", ip_str, sizeof(ip_str)))
        sscanf(ip_str, "%d.%d.%d.%d", &g_ip[0], &g_ip[1], &g_ip[2], &g_ip[3]);
    if (form_field(body, "sn", ip_str, sizeof(ip_str)))
        sscanf(ip_str, "%d.%d.%d.%d", &g_sn[0], &g_sn[1], &g_sn[2], &g_sn[3]);
    if (form_field(body, "gw", ip_str, sizeof(ip_str)))
        sscanf(ip_str, "%d.%d.%d.%d", &g_gt[0], &g_gt[1], &g_gt[2], &g_gt[3]);
    if (form_field(body, "sig",  tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v < kSigCount) g_sigIdx = v; }
    if (form_field(body, "brit", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 1 && v <= 15)       g_brit   = v; }
    if (form_field(body, "acc",  tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v < kAccCount) g_accIdx = v; }
    free(body);

    nvs_save_settings();
    nvs_save_wifi();
    apply_settings();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body>"
        "<h2>Saved. tapbox is rebooting...</h2>"
        "<p>Reconnect to your WiFi network if needed, "
        "then visit <a href='http://tapbox.local'>tapbox.local</a></p>"
        "</body></html>");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static void http_server_start() {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) { s_httpd = nullptr; return; }
    httpd_uri_t get_h  = { .uri = "/",     .method = HTTP_GET,  .handler = http_get_root,  .user_ctx = nullptr };
    httpd_uri_t post_h = { .uri = "/save", .method = HTTP_POST, .handler = http_post_save, .user_ctx = nullptr };
    httpd_register_uri_handler(s_httpd, &get_h);
    httpd_register_uri_handler(s_httpd, &post_h);
    printf("HTTP config server started\n");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_wifi_joined = true;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        printf("WiFi: associated — waiting for DHCP\n");
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        printf("WiFi: disconnected, reason=%d\n", e->reason);
        if (g_wifi_enabled && !g_wifi_as_ap) {
            if (s_wifi_joined) {
                s_wifi_joined = false;  // next failure → AP
                esp_wifi_connect();     // was connected — one retry
            } else {
                g_sta_failed = true;    // never connected or retry failed → AP
            }
        }
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_joined = true;
        g_wifi_got_ip = true;
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&e->ip_info.ip, g_wifi_ip_str, sizeof(g_wifi_ip_str));
        printf("WiFi IP: %s\n", g_wifi_ip_str);
        http_server_start();
    }
}

static void wifi_common_init() {
    if (g_wifi_initialized) return;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr);
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&init_cfg);
    g_wifi_initialized = true;
}

static void wifi_start_ap() {
    wifi_common_init();
    if (g_wifi_enabled) esp_wifi_stop();
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {};
    const char *ap_ssid = "tapbox";
    memcpy(ap_cfg.ap.ssid, ap_ssid, strlen(ap_ssid));
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(ap_ssid);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();
    esp_wifi_set_max_tx_power(34);  // 8.5 dBm — reduce current spike on marginal supplies

    http_server_start();

    g_wifi_enabled  = true;
    g_wifi_as_ap    = true;
    s_wifi_joined   = false;
    s_wifi_start_ms = now_ms();
    g_show_ap_splash = true;
    printf("WiFi AP: tapbox (open) — config at 192.168.4.1\n");
}

static void wifi_start_sta() {
    wifi_common_init();
    if (g_wifi_enabled) esp_wifi_stop();
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     g_wifi_ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, g_wifi_pass, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    esp_wifi_set_max_tx_power(34);  // 8.5 dBm — reduce current spike on marginal supplies
    esp_wifi_connect();

    g_wifi_enabled  = true;
    g_wifi_as_ap    = false;
    s_wifi_joined   = false;
    s_wifi_start_ms = now_ms();
    printf("WiFi STA: connecting to %s\n", g_wifi_ssid);
}

static void wifi_stop() {
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = nullptr; }
    if (g_wifi_enabled) { esp_wifi_stop(); g_wifi_enabled = false; }
    printf("WiFi stopped (60 s timeout)\n");
}

static void wifi_init() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) == ESP_OK) {
        size_t len;
        len = sizeof(g_wifi_ssid); nvs_get_str(h, "wifi_ssid", g_wifi_ssid, &len);
        len = sizeof(g_wifi_pass); nvs_get_str(h, "wifi_pass", g_wifi_pass, &len);
        nvs_close(h);
    }
    if (g_wifi_ssid[0] == '\0') wifi_start_ap();
    else                        wifi_start_sta();
}


// ── Link helpers ───────────────────────────────────────────────────────────────

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

// ── Input ──────────────────────────────────────────────────────────────────────

static void exit_menu() {
    if (appMode == MODE_MENU_EDIT && menuItem == MENU_BRIT)
        display.setIntensity(g_brit);
    appMode = MODE_NORMAL;
}

static void do_tap() {
    TapResult r = tapTempo.tap();
    if (r.wentLive) {
        go_live(tapTempo.bpm(), tapTempo.sessionStartMs());
        printf("Live: %.1f BPM\n", tapTempo.bpm());
    } else if (r.bpmChanged && linkEnabled) {
        set_link_tempo(tapTempo.bpm());
        printf("Tap: %.1f BPM\n", tapTempo.bpm());
    } else if (r.newSession) {
        if (linkEnabled) reset_downbeat();
        printf("New session — tap 3 more times\n");
    }
}

// Tap button: cycles items/values in menu; auto-increments in edit modes while held
static void on_button_short_press() {
    uint32_t now = now_ms();
    switch (appMode) {
        case MODE_MENU_NAV: {
            int next = menuItem;
            do { next = (next + 1) % MENU_COUNT; } while (!menu_item_visible(next));
            menuItem      = next;
            menuEnteredAt = now;
            break;
        }
        case MODE_MENU_EDIT: {
            int lo = menu_val_min(menuItem), hi = menu_val_max(menuItem);
            menuEditVal = lo + ((menuEditVal - lo + 1 + (hi - lo + 1)) % (hi - lo + 1));
            if (menuItem == MENU_BRIT) display.setIntensity(menuEditVal);
            menuEnteredAt = now;
            break;
        }
        case MODE_SUBMENU_NAV:
            menuSubItem   = (menuSubItem + 1) % 5;
            menuEnteredAt = now;
            break;
        case MODE_SUBMENU_EDIT:
            menuEditVal   = (menuEditVal + 1) % 256;
            menuEnteredAt = now;
            break;
        default: break;
    }
}

static void handle_button() {
    int      reading = gpio_get_level(PIN_TAP_BUTTON);
    uint32_t now     = now_ms();

    if (reading != lastButton && (now - lastDebounce) >= DEBOUNCE_MS) {
        lastDebounce = now;
        if (reading == 0) {
            s_btnPressedAt  = now;
            s_btnLongFired  = false;
            s_btnAutoIncrAt = now;
            if (appMode == MODE_NORMAL) do_tap();
        } else {
            if (!s_btnLongFired) on_button_short_press();
        }
    }
    lastButton = reading;

    // Auto-increment while held in edit modes (starts at 500 ms, speeds up at 1500 ms)
    bool inEdit = (appMode == MODE_MENU_EDIT || appMode == MODE_SUBMENU_EDIT);
    if (reading == 0 && inEdit) {
        uint32_t held     = now - s_btnPressedAt;
        uint32_t interval = (held < 1500) ? 200 : 50;
        if (held >= 500 && (now - s_btnAutoIncrAt) >= interval) {
            s_btnAutoIncrAt = now;
            s_btnLongFired  = true;
            if (appMode == MODE_MENU_EDIT) {
                int lo = menu_val_min(menuItem), hi = menu_val_max(menuItem);
                menuEditVal = lo + ((menuEditVal - lo + 1 + (hi - lo + 1)) % (hi - lo + 1));
                if (menuItem == MENU_BRIT) display.setIntensity(menuEditVal);
            } else {
                menuEditVal = (menuEditVal + 1) % 256;
            }
            menuEnteredAt = now;
        }
    }

    // Long press in normal mode only: enter menu
    if (reading == 0 && !s_btnLongFired && (now - s_btnPressedAt) >= LONG_PRESS_MS) {
        s_btnLongFired = true;
        if (appMode == MODE_NORMAL) {
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
        }
    }
}

// Select button: confirm/enter on short press; back/exit on long press
static void on_select_short_press() {
    uint32_t now = now_ms();
    switch (appMode) {
        case MODE_NORMAL:
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
            break;
        case MODE_MENU_NAV:
            if (menuItem == MENU_DONE) {
                exit_menu();
            } else if (menuItem == MENU_VER || menuItem == MENU_BAT) {
                menuEnteredAt = now;
            } else if (menuItem == MENU_RESET) {
                appMode       = MODE_MENU_CONFIRM;
                menuEnteredAt = now;
            } else if (menuItem == MENU_IP || menuItem == MENU_SN || menuItem == MENU_GT) {
                menuSubItem   = 0;
                appMode       = MODE_SUBMENU_NAV;
                menuEnteredAt = now;
            } else {
                menuEditVal   = menu_get_val(menuItem);
                appMode       = MODE_MENU_EDIT;
                menuEnteredAt = now;
            }
            break;
        case MODE_MENU_EDIT: {
            bool netChanged = (menuItem == MENU_NET && menuEditVal != g_net);
            menu_commit(menuItem, menuEditVal);
            apply_settings();
            nvs_save_settings();
            if (netChanged) show_boot_reboot();
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
            break;
        }
        case MODE_MENU_CONFIRM:
            factory_reset();  // does not return
            break;
        case MODE_SUBMENU_NAV:
            if (menuSubItem == 4) {
                appMode       = MODE_MENU_NAV;
                menuEnteredAt = now;
            } else {
                menuEditVal   = submenu_array()[menuSubItem];
                appMode       = MODE_SUBMENU_EDIT;
                menuEnteredAt = now;
            }
            break;
        case MODE_SUBMENU_EDIT:
            submenu_array()[menuSubItem] = menuEditVal;
            nvs_save_settings();
            appMode       = MODE_SUBMENU_NAV;
            menuEnteredAt = now;
            break;
    }
}

static void on_select_long_press() {
    uint32_t now = now_ms();
    switch (appMode) {
        case MODE_SUBMENU_NAV:
        case MODE_SUBMENU_EDIT:
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
            break;
        case MODE_MENU_NAV:
        case MODE_MENU_EDIT:
        case MODE_MENU_CONFIRM:
            exit_menu();
            break;
        default: break;
    }
}

static void handle_select() {
    int      reading = gpio_get_level(PIN_SELECT);
    uint32_t now     = now_ms();

    if (reading != lastSelect && (now - lastSelectDb) >= DEBOUNCE_MS) {
        lastSelectDb = now;
        if (reading == 0) {
            s_selPressedAt = now;
            s_selLongFired = false;
        } else {
            if (!s_selLongFired) on_select_short_press();
        }
    }
    lastSelect = reading;

    if (reading == 0 && !s_selLongFired && (now - s_selPressedAt) >= SELECT_LONG_MS) {
        s_selLongFired = true;
        on_select_long_press();
    }
}

static void check_menu_timeout() {
    if (appMode == MODE_NORMAL) return;
    if ((now_ms() - menuEnteredAt) < MENU_TIMEOUT_MS) return;
    if (appMode == MODE_MENU_EDIT && menuItem == MENU_BRIT)
        display.setIntensity(g_brit);
    appMode = MODE_NORMAL;
}

// ── OSC server ─────────────────────────────────────────────────────────────────

static uint32_t osc_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static float osc_float(const uint8_t *p) {
    uint32_t u = osc_u32(p); float f; memcpy(&f, &u, 4); return f;
}

static void osc_handle(const uint8_t *buf, int len) {
    if (len < 4) return;
    const char *addr       = (const char *)buf;
    int         addr_bytes = ((strlen(addr) + 4) & ~3);
    if (addr_bytes >= len) return;
    const char *tags       = (const char *)(buf + addr_bytes);
    if (tags[0] != ',') return;
    int         tags_bytes = ((strlen(tags) + 4) & ~3);
    const uint8_t *args    = buf + addr_bytes + tags_bytes;
    int         args_len   = len - addr_bytes - tags_bytes;

    if (strcmp(addr, "/tap") == 0) {
        TapResult r = tapTempo.tap();
        if (r.wentLive)                       { go_live(tapTempo.bpm(), tapTempo.sessionStartMs()); printf("OSC live: %.1f BPM\n", tapTempo.bpm()); }
        else if (r.bpmChanged && linkEnabled) { set_link_tempo(tapTempo.bpm()); printf("OSC tap: %.1f BPM\n", tapTempo.bpm()); }
        else if (r.newSession)                { printf("OSC tap: new session\n"); }
    } else if (strcmp(addr, "/bpm") == 0 && args_len >= 4) {
        float bpm = (tags[1] == 'f') ? osc_float(args) : (float)(int32_t)osc_u32(args);
        tapTempo.setBpm(bpm);
        set_link_tempo(tapTempo.bpm());
        printf("OSC bpm: %.1f\n", tapTempo.bpm());
    } else if (strcmp(addr, "/nudge_up") == 0) {
        nudge_phase(g_nudgeUs);
        printf("OSC nudge +%dms\n", kAccNudgeMs[g_accIdx]);
    } else if (strcmp(addr, "/nudge_down") == 0) {
        nudge_phase(-g_nudgeUs);
        printf("OSC nudge -%dms\n", kAccNudgeMs[g_accIdx]);
    } else if (strcmp(addr, "/downbeat") == 0) {
        reset_downbeat();
        printf("OSC downbeat reset\n");
    } else if (strcmp(addr, "/signature") == 0 && args_len >= 4) {
        int sig = (tags[1] == 'f') ? (int)osc_float(args) : (int)(int32_t)osc_u32(args);
        for (int i = 0; i < kSigCount; i++) {
            if ((int)kSignatures[i] == sig) { g_sigIdx = i; apply_settings(); break; }
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

// ── Status ─────────────────────────────────────────────────────────────────────

static void print_status() {
    static uint32_t lastPrint = 0;
    if (now_ms() - lastPrint < 2000) return;
    lastPrint = now_ms();
    if (!linkEnabled) { printf("Cold start — tap 4 times to go live\n"); return; }
    abl_link_capture_app_session_state(s_link, s_session);
    printf("Link: %.2f BPM  sig: %.0f  peers: %llu  eth: %s\n",
           abl_link_tempo(s_session), linkQuantum,
           (unsigned long long)abl_link_num_peers(s_link),
           ethConnected ? ethIPStr : "disconnected");
}

// ── GPIO init ──────────────────────────────────────────────────────────────────

static void init_gpio() {
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_TAP_BUTTON) | (1ULL << PIN_SELECT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

static void init_adc() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    adc_oneshot_new_unit(&unit_cfg, &s_adc1);

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(s_adc1, ADC_CHANNEL_0, &chan_cfg);
}

// ── Entry point ────────────────────────────────────────────────────────────────

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    esp_netif_init();

    init_gpio();
    init_adc();
    display.init();
    nvs_load_settings();

    // Both buttons held at power-on → OTA update mode
    bool ota_at_boot = (gpio_get_level((gpio_num_t)PIN_TAP_BUTTON) == 0 &&
                        gpio_get_level((gpio_num_t)PIN_SELECT)      == 0);

    initEthernet(g_net, g_ip, g_sn, g_gt);

    printf("Waiting for Ethernet");
    uint32_t start = now_ms();
    while (!ethConnected && (now_ms() - start) < 10000) {
        vTaskDelay(pdMS_TO_TICKS(250));
        printf(".");
        fflush(stdout);
    }
    printf("\n");

    if (!ethConnected) wifi_init();

    // Register after the boot wait so the initial IP event doesn't trigger switching
    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, eth_lost_cb,   nullptr);
    esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP,         eth_got_ip_cb, nullptr);

    if (ethConnected) {
        printf("IP: %s\n", ethIPStr);
        show_ip_splash(ethIPStr);
        if (ota_at_boot) { ota_at_boot = false; perform_ota(); }
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

    // Confirm to the bootloader that this firmware booted successfully.
    // If the device was just OTA-updated, this prevents rollback on next boot.
    esp_ota_mark_app_valid_cancel_rollback();

    xTaskCreate(osc_task, "osc", 4096, nullptr, 5, nullptr);
    printf("OSC listening on port %d\n", OSC_PORT);

    while (true) {
        handle_button();
        handle_select();
        check_menu_timeout();
        if (g_sta_failed) {
            g_sta_failed = false;
            printf("WiFi STA failed — falling back to AP\n");
            wifi_stop();
            wifi_start_ap();
        }
        if (g_eth_lost) {
            g_eth_lost = false;
            printf("Ethernet lost — starting WiFi\n");
            wifi_init();
        }
        if (g_show_ap_splash) {
            g_show_ap_splash = false;
            show_ip_splash("192.168.4.1");
        }
        if (g_wifi_got_ip) {
            g_wifi_got_ip = false;
            show_ip_splash(g_wifi_ip_str);
            abl_link_enable(s_link, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            abl_link_enable(s_link, true);
            linkEnabled = true;
            printf("Link re-enabled on WiFi interface\n");
            if (ota_at_boot) { ota_at_boot = false; perform_ota(); }
        }
        if (g_eth_got_ip) {
            g_eth_got_ip = false;
            if (g_wifi_enabled) {
                printf("Ethernet back — stopping WiFi\n");
                wifi_stop();
            }
            abl_link_enable(s_link, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            abl_link_enable(s_link, true);
            linkEnabled = true;
            printf("Link re-enabled on Ethernet interface\n");
            if (ota_at_boot) { ota_at_boot = false; perform_ota(); }
        }
        update_display();
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
