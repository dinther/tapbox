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
#include "driver/i2s_std.h"
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
#include "esp_mac.h"

// ── Pin assignments ────────────────────────────────────────────────────────────
#define PIN_TAP_BUTTON  GPIO_NUM_35  // input-only — needs external 10k pullup to 3V3
#define PIN_SELECT      GPIO_NUM_39  // input-only — needs external 10k pullup to 3V3
#define PIN_DISP_CLK    GPIO_NUM_14
#define PIN_DISP_DIN    GPIO_NUM_2
#define PIN_DISP_LOAD   GPIO_NUM_15
#define PIN_I2S_BCLK    GPIO_NUM_4   // INMP441 SCK
#define PIN_I2S_WS      GPIO_NUM_12  // INMP441 WS  (strapping pin; eFuse-locked on WT32-ETH01)
#define PIN_I2S_DIN     GPIO_NUM_36  // INMP441 SD  (was battery ADC)
#define MIC_SAMPLE_RATE 32000

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
static constexpr uint8_t CH_d = 0x3D;
static constexpr uint8_t CH_e = 0x6F;
static constexpr uint8_t CH_h = 0x17;
static constexpr uint8_t CH_I = 0x30;
static constexpr uint8_t CH_L = 0x0E;
static constexpr uint8_t CH_o = 0x1D;
static constexpr uint8_t CH_n = 0x15;
static constexpr uint8_t CH_P = 0x67;
static constexpr uint8_t CH_r = 0x05;
static constexpr uint8_t CH_S = 0x5B;
static constexpr uint8_t CH_t = 0x0F;
static constexpr uint8_t CH_u = 0x1C;
static constexpr uint8_t CH_C    = 0x4E;  // segments A,D,E,F
static constexpr uint8_t CH_F    = 0x47;  // segments A,E,F,G
static constexpr uint8_t CH_J    = 0x38;  // segments B,C,D

// Mode indicator bars (single horizontal segment, see project memory for encoding)
static constexpr uint8_t BAR_TOP = 0x40;  // segment A  → CDJ mode
static constexpr uint8_t BAR_MID = 0x01;  // segment G  → Manual mode
static constexpr uint8_t BAR_BOT = 0x08;  // segment D  → Audio mode

// ── Firmware version ───────────────────────────────────────────────────────────
#define FW_MAJOR 1
#define FW_MINOR 11
#define FW_PATCH 1

// ── Menu option tables ─────────────────────────────────────────────────────────
static const double kSignatures[] = { 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 };
static const int    kSigCount     = 6;

#define DEFAULT_NUDGE_MS 20  // used by OSC /nudge when no argument is given

static const int    kBritLevels[] = { 1, 5, 10, 15 };  // user levels 1-4 → MAX7219 intensity
static const int    kBritCount    = 4;

// ── Sync mode (mutually exclusive) ──────────────────────────────────────────────
enum SyncMode { MODE_CDJ = 0, MODE_AUDIO = 1, MODE_MANUAL = 2 };

// ── Persistent settings ────────────────────────────────────────────────────────
static int g_sigIdx   = 2;             // kSignatures[2] = 4/4
static int g_brit     = 1;             // brightness level index 0–3 (→ kBritLevels)
static int g_net      = 0;             // 0=DHCP, 1=static
static int g_mode     = MODE_AUDIO;    // 0=CDJ, 1=Audio (mic), 2=Manual
// Mic / beat-detection tuning knobs (live-adjustable while tuning; folded into a
// preset later). See [[project-inmp441-plan]].
static int g_micWin   = 4;             // beat-accept window, ± BPM around tapped tempo (1–10)
static int g_micSlew  = 10;            // tempo slew limit, units of 0.1%/sec (0–50)
static int g_micThr   = 8;             // onset threshold: energy > baseline*(1+thr/10) (0–30)
static int g_micGate  = 17;           // absolute noise-gate floor (0–50, log scale: 10^(g/10)×1e-6)
static int g_micFreq  = 150;           // kick-band low-pass cutoff, Hz (60–300)
static int g_ip[4]    = {192, 168,   1, 200};
static int g_sn[4]    = {255, 255, 255,   0};
static int g_gt[4]    = {192, 168,   1,   1};
static char g_wifi_ssid[64] = {};
static char g_wifi_pass[64] = {};

// ── RTC memory — survives esp_restart() but not power-on; used to restore
//    settings if NVS is erased on the boot following an OTA update. ────────────
struct RtcSettings {
    uint32_t magic;
    int sigIdx, brit, net, mode;
    int micWin, micSlew, micThr, micGate, micFreq;
    int ip[4], sn[4], gt[4];
    char wifi_ssid[64], wifi_pass[64];
};
static constexpr uint32_t RTC_MAGIC = 0xCAFEB00B;
RTC_DATA_ATTR static RtcSettings s_rtc;

// ── Core globals ───────────────────────────────────────────────────────────────
static TapTempo                  tapTempo;
static MAX7219Display            display(PIN_DISP_CLK, PIN_DISP_DIN, PIN_DISP_LOAD);

static abl_link               s_link;
static abl_link_session_state s_session;
static bool   linkEnabled = false;
static double linkQuantum = 4.0;

// ── Button state ───────────────────────────────────────────────────────────────
struct BtnCtx {
    int      last         = 1;
    uint32_t last_db      = 0;
    uint32_t pressed_at   = 0;
    bool     long_fired   = false;
    uint32_t auto_incr_at = 0;  // tap button only
};
static BtnCtx s_tap_ctx;
static BtnCtx s_sel_ctx;

enum BothHeldState { BH_IDLE, BH_HELD, BH_OTA, BH_RESET };
static BothHeldState s_bh_state = BH_IDLE;
static uint32_t      s_bh_since = 0;

static volatile bool g_cdj_active = false;  // set by cdj_task; read by display + input

// ── Mic / beat-detection runtime state ──────────────────────────────────────────
static i2s_chan_handle_t s_i2s_rx     = nullptr;
static volatile double   g_mic_bpm    = 0.0;    // last raw detected BPM (display hint)
static volatile bool     g_mic_locked = false;  // mic has a stable tracked BPM
static volatile bool     g_mic_armed  = false;  // a tap has anchored the tracker
static volatile double   g_mic_tracked = 0.0;   // adaptive anchor / applied BPM
static volatile double   g_mic_tapAnchor = 0.0; // last tap-set tempo (clamp reference)

// Live telemetry for the web page's audio-tuning chart — written every mic
// block (~4 ms), read every 50 ms by the WS push task. Energy is peak-held and
// onset is latched between pushes: the mic loop runs ~12x faster than the
// chart, so plain sampling would miss most kick peaks and detected beats.
static volatile double g_tele_energy    = 0.0;  // peak since last push; reset by push task
static volatile double g_tele_baseline  = 0.0;
static volatile double g_tele_threshold = 0.0;  // baseline * threshFactor, same units as energy
static volatile double g_tele_gate      = 0.0;
static volatile bool   g_tele_onset     = false;  // latched since last push; reset by push task
// Running totals (wrap-safe on the client): every detected onset, and every
// onset that passed the accept window and actually influenced the tempo.
static volatile uint32_t g_tele_det = 0;
static volatile uint32_t g_tele_acc = 0;

static bool g_manual_locked = false;  // true after first 4-tap session in Manual mode

static bool g_wifi_enabled = false;
static bool g_wifi_as_ap   = false;
static bool g_wifi_got_ip  = false;
static bool g_eth_got_ip   = false;
static bool g_eth_lost     = false;
static bool g_ap_started   = false;
static char g_wifi_ip_str[16] = {};

// AP/web-config PIN — see compute_security_pin() for how this is derived.
// Declared here (rather than down by the WiFi/HTTP code) so the boot/reconnect
// scroll splash can always append it, in every network mode.
static char g_ap_pin[9] = "00000000";

// ── Non-blocking one-shot scroll (boot/reconnect IP splash) ───────────────────
#define PIN_HOLD_MS 6000  // how long the scroll pauses once the PIN fills the display
static uint8_t  s_scroll_buf[48]    = {};
static int      s_scroll_len        = 0;
static int      s_scroll_offset     = -7;
static uint32_t s_scroll_step_ms    = 0;
static bool     s_scroll_active     = false;
static bool     s_scroll_holding    = false;  // paused with the PIN fully shown
static uint32_t s_scroll_hold_start = 0;

// ── Menu state ─────────────────────────────────────────────────────────────────
enum AppMode { MODE_NORMAL, MODE_MENU_NAV, MODE_MENU_EDIT, MODE_MENU_CONFIRM,
               MODE_OTA_CONFIRM };
static AppMode  appMode       = MODE_NORMAL;
static uint32_t menuEnteredAt = 0;

// Mic tuning (uind/SLEu/thr/gAte) and static-IP octets (IP/Sub./Hub.) are
// configured on the web page only — see http_get_root(). The on-device menu
// only carries settings usable without a browser at hand.
enum MenuIdx {
    MENU_SIG = 0, MENU_BRIT, MENU_MODE,
    MENU_NET, MENU_CURIP,
    MENU_RESET, MENU_VER, MENU_DONE,
    MENU_COUNT  // 8
};
static int menuItem    = 0;
static int menuEditVal = 0;

// Snapshot of network config taken on menu entry; compared on exit so we
// reboot exactly once if the Lan. mode actually changed.
static int g_net_snap = 0;

// ── Time helpers ───────────────────────────────────────────────────────────────
static inline uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
static inline uint64_t now_us() { return (uint64_t)esp_timer_get_time(); }

static void show_boot_reboot();          // forward declaration
static void nvs_save_wifi();             // forward declaration
static void nvs_save_ota_pending(bool);  // forward declaration
static void rtc_save_settings();         // forward declaration
static void wifi_init();                 // forward declaration
static void wifi_stop();                 // forward declaration

static void eth_lost_cb(void *, esp_event_base_t, int32_t, void *) {
    g_eth_lost = true;
}
static void eth_got_ip_cb(void *, esp_event_base_t, int32_t, void *) {
    g_eth_got_ip = true;
}
static void eth_connected_cb(void *, esp_event_base_t, int32_t, void *) {
    // For static IP, ethernet_config.h sets ethConnected in this same event
    // (its handler registered first). If true here, this is a static IP reconnect.
    if (ethConnected) g_eth_got_ip = true;
}

// ── Settings ───────────────────────────────────────────────────────────────────

static void apply_settings() {
    linkQuantum = kSignatures[g_sigIdx];
    display.setIntensity(kBritLevels[g_brit]);
}

static void nvs_load_settings() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK) {
        apply_settings();
        return;
    }
    int32_t v;
    if (nvs_get_i32(h, "sig",  &v) == ESP_OK && v >= 0 && v < kSigCount)   g_sigIdx   = (int)v;
    if (nvs_get_i32(h, "brit", &v) == ESP_OK && v >= 0 && v <= 3)          g_brit     = (int)v;
    if (nvs_get_i32(h, "mode", &v) == ESP_OK && v >= 0 && v <= 2)          g_mode     = (int)v;
    if (nvs_get_i32(h, "mwin", &v) == ESP_OK && v >= 0 && v <= 10)         g_micWin   = (int)v;
    if (nvs_get_i32(h, "mslew",&v) == ESP_OK && v >= 0 && v <= 50)         g_micSlew  = (int)v;
    if (nvs_get_i32(h, "mthr", &v) == ESP_OK && v >= 0 && v <= 30)         g_micThr   = (int)v;
    if (nvs_get_i32(h, "mgate",&v) == ESP_OK && v >= 0 && v <= 50)         g_micGate  = (int)v;
    if (nvs_get_i32(h, "mfreq",&v) == ESP_OK && v >= 60 && v <= 300)       g_micFreq  = (int)v;
    if (nvs_get_i32(h, "net",  &v) == ESP_OK && v >= 0 && v <= 2)          g_net      = (int)v;
    if (nvs_get_i32(h, "ip0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[0]    = (int)v;
    if (nvs_get_i32(h, "ip1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[1]    = (int)v;
    if (nvs_get_i32(h, "ip2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[2]    = (int)v;
    if (nvs_get_i32(h, "ip3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_ip[3]    = (int)v;
    if (nvs_get_i32(h, "sn0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[0]    = (int)v;
    if (nvs_get_i32(h, "sn1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[1]    = (int)v;
    if (nvs_get_i32(h, "sn2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[2]    = (int)v;
    if (nvs_get_i32(h, "sn3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_sn[3]    = (int)v;
    if (nvs_get_i32(h, "gt0",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[0]    = (int)v;
    if (nvs_get_i32(h, "gt1",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[1]    = (int)v;
    if (nvs_get_i32(h, "gt2",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[2]    = (int)v;
    if (nvs_get_i32(h, "gt3",  &v) == ESP_OK && v >= 0 && v <= 255)        g_gt[3]    = (int)v;
    // WiFi credentials loaded here — avoids a second NVS open in wifi_init()
    size_t len;
    len = sizeof(g_wifi_ssid); nvs_get_str(h, "wifi_ssid", g_wifi_ssid, &len);
    len = sizeof(g_wifi_pass); nvs_get_str(h, "wifi_pass", g_wifi_pass, &len);
    nvs_close(h);
    apply_settings();
}

static void nvs_save_settings() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "sig",  (int32_t)g_sigIdx);
    nvs_set_i32(h, "brit", (int32_t)g_brit);
    nvs_set_i32(h, "mode", (int32_t)g_mode);
    nvs_set_i32(h, "mwin", (int32_t)g_micWin);
    nvs_set_i32(h, "mslew",(int32_t)g_micSlew);
    nvs_set_i32(h, "mthr", (int32_t)g_micThr);
    nvs_set_i32(h, "mgate",(int32_t)g_micGate);
    nvs_set_i32(h, "mfreq",(int32_t)g_micFreq);
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
    g_sigIdx = 2; g_brit = 1; g_mode = MODE_AUDIO;
    g_micWin = 4; g_micSlew = 10; g_micThr = 8; g_micGate = 17; g_micFreq = 150;
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
    return item != MENU_RESET;
}

static int menu_get_val(int item) {
    switch (item) {
        case MENU_SIG:   return g_sigIdx;
        case MENU_BRIT:  return g_brit;
        case MENU_MODE:  return g_mode;
        case MENU_NET:   return g_net;
        case MENU_CURIP: {
            const char *ip = ethConnected                       ? ethIPStr       :
                             (g_wifi_enabled && !g_wifi_as_ap) ? g_wifi_ip_str  :
                             g_wifi_as_ap                      ? "192.168.4.1"  : "0.0.0.0";
            const char *p = strrchr(ip, '.');
            return p ? atoi(p + 1) : 0;
        }
        default: return 0;
    }
}

static int menu_val_min(int item) { (void)item; return 0; }

static int menu_val_max(int item) {
    switch (item) {
        case MENU_SIG:   return kSigCount - 1;
        case MENU_BRIT:  return kBritCount - 1;
        case MENU_MODE:  return 2;
        case MENU_NET:   return 2;
        default: return 0;
    }
}

static void menu_commit(int item, int val) {
    switch (item) {
        case MENU_SIG:   g_sigIdx   = val; break;
        case MENU_BRIT:  g_brit     = val; break;
        case MENU_MODE:  g_mode     = val; break;
        case MENU_NET:   g_net      = val; break;
    }
}

// ── Display ────────────────────────────────────────────────────────────────────

// Digit segment bytes for use in static label arrays: 0=0x7E 1=0x30 2=0x6D 3=0x79 4=0x33
static const uint8_t kMenuLabels[MENU_COUNT][4] = {
    { CH_B, CH_e, CH_a, CH_t                                       },  // Beat
    { CH_L, CH_e, CH_d, MAX7219Display::SEG_BLANK                  },  // Led
    { CH_n, CH_o, CH_d, CH_e                                       },  // node (mode)
    { CH_L, CH_a, CH_n | MAX7219Display::SEG_DP, MAX7219Display::SEG_BLANK },  // Lan.
    { CH_A, CH_d, CH_d, CH_r                                               },  // Addr (current IP)
    { CH_r, CH_S, CH_e, CH_t                                       },  // rSet
    { CH_u, CH_e, CH_r, MAX7219Display::SEG_BLANK                  },  // vEr  (CH_u renders as 'v')
    { CH_d, CH_o, CH_n, CH_e                                       },  // done
};

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
        case MENU_BRIT:
            render_int3(segs, val + 1);  // show 1-4 (user level)
            break;
        case MENU_MODE:
            // 3-char value, right-justified: CdJ / Aud / tAP
            if (val == MODE_CDJ)       { segs[5] = CH_C; segs[6] = CH_d; segs[7] = CH_J; }
            else if (val == MODE_AUDIO){ segs[5] = CH_A; segs[6] = CH_u; segs[7] = CH_d; }
            else                       { segs[5] = CH_t; segs[6] = CH_A; segs[7] = CH_P; }
            break;
        case MENU_NET:
            // 4-char value fills positions 4-7, overriding the blank separator
            if (val == 0) {
                segs[4] = CH_A; segs[5] = CH_u; segs[6] = CH_t; segs[7] = CH_o;
            } else if (val == 1) {
                segs[4] = CH_S; segs[5] = CH_t; segs[6] = CH_a; segs[7] = CH_t;
            } else {
                segs[4] = segs[5] = MAX7219Display::SEG_BLANK;
                segs[6] = CH_A; segs[7] = CH_P;
            }
            break;
        case MENU_CURIP:
            render_int3(segs, val);
            break;
        case MENU_RESET:
            segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
            break;
        case MENU_VER:
            segs[5] = MAX7219Display::digit(FW_MAJOR) | MAX7219Display::SEG_DP;
            segs[6] = MAX7219Display::digit(FW_MINOR) | MAX7219Display::SEG_DP;
            segs[7] = MAX7219Display::digit(FW_PATCH);
            break;
        case MENU_DONE:
            break;
    }
}

static void update_display() {
    static uint32_t lastUpdate = 0;
    uint32_t now = now_ms();
    if (now - lastUpdate < DISPLAY_MS) return;
    lastUpdate = now;

    uint8_t segs[8] = {};

    if (s_bh_state == BH_OTA) return;  // hold UPd.---- preview while user still holding

    // One-shot scroll (boot/reconnect IP splash) takes priority over all modes
    if (s_scroll_active) {
        if (s_scroll_holding) {
            // PIN fills the whole display — hold it steady so it's actually
            // readable, instead of scrolling straight past it.
            if (now - s_scroll_hold_start >= PIN_HOLD_MS) {
                s_scroll_holding = false;
                s_scroll_offset++;
                s_scroll_step_ms = now;
            }
            return;
        }
        if (now - s_scroll_step_ms >= 300) {
            s_scroll_step_ms = now;
            for (int d = 0; d < 8; d++) {
                int idx = s_scroll_offset + d;
                segs[d] = (idx >= 0 && idx < s_scroll_len) ? s_scroll_buf[idx] : MAX7219Display::SEG_BLANK;
            }
            display.setSegments(segs);
            if (s_scroll_offset == s_scroll_len - 8) {
                s_scroll_holding    = true;
                s_scroll_hold_start = now;
            } else if (++s_scroll_offset > s_scroll_len) {
                s_scroll_active = false;
            }
        }
        return;
    }

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
            // Lock dot on beat digit: solid=locked, blinking=Audio searching, blank=inactive
            bool locked_state = (g_mode == MODE_CDJ    && g_cdj_active)
                             || (g_mode == MODE_AUDIO  && g_mic_locked)
                             || (g_mode == MODE_MANUAL && g_manual_locked);
            bool searching    = (g_mode == MODE_AUDIO && g_mic_armed && !g_mic_locked);
            bool dot = locked_state || (searching && (now_us() / 250000ULL) % 2 == 0);
            segs[5] = MAX7219Display::digit((int)floor(phase) + 1) | (dot ? MAX7219Display::SEG_DP : 0);
            // Persistent mode bar (top=CDJ, middle=Manual, bottom=Audio) — placed
            // behind the beat counter so the counter's left side stays clean.
            segs[6] = (g_mode == MODE_CDJ) ? BAR_TOP
                    : (g_mode == MODE_MANUAL) ? BAR_MID : BAR_BOT;
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

    if (appMode == MODE_OTA_CONFIRM) {
        segs[0] = CH_u; segs[1] = CH_P; segs[2] = CH_d; segs[3] = MAX7219Display::SEG_BLANK;
        segs[4] = CH_S; segs[5] = CH_u; segs[6] = CH_r; segs[7] = CH_e;
        display.setSegments(segs);
        return;
    }

    // MODE_MENU_NAV or MODE_MENU_EDIT
    memcpy(segs, kMenuLabels[menuItem], 4);

    int  val   = (appMode == MODE_MENU_EDIT) ? menuEditVal : menu_get_val(menuItem);
    bool blank = (appMode == MODE_MENU_EDIT) && ((now / 250) & 1);
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

    if (esp_ota_get_next_update_partition(NULL) == NULL) {
        printf("OTA: no update partition found\n");
        show_err(); return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));  // let DNS and routing settle after IP assignment

    esp_http_client_config_t http_cfg = {};
    http_cfg.url                   = OTA_URL;
    http_cfg.crt_bundle_attach     = esp_crt_bundle_attach;
    http_cfg.timeout_ms            = 30000;
    http_cfg.max_redirection_count = 5;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t h = nullptr;
    esp_err_t begin_err = esp_https_ota_begin(&ota_cfg, &h);
    if (begin_err != ESP_OK) {
        printf("OTA begin failed: %s (0x%x)\n", esp_err_to_name(begin_err), begin_err);
        show_err(); return;
    }

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
        rtc_save_settings();
        esp_restart();
    } else {
        printf("OTA perform/finish failed: %s (0x%x)\n", esp_err_to_name(err), err);
        esp_https_ota_abort(h);
        show_err();
    }
}

// ── OTA pending trigger ────────────────────────────────────────────────────────

static void trigger_ota_pending() {
    uint8_t segs[8] = {};
    memcpy(segs, kOtaLabel, 4);
    segs[4] = segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
    display.setSegments(segs);
    nvs_save_ota_pending(true);
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        const esp_partition_t *otadata = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
        if (otadata) esp_partition_erase_range(otadata, 0, otadata->size);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
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

static uint8_t char_to_seg(char c) {
    if (c >= '0' && c <= '9') return MAX7219Display::digit(c - '0');
    switch (c) {
        case 'A': case 'a': return CH_A;
        case 'D': case 'd': return CH_d;
        case 'E': case 'e': return CH_e;
        case 'H': case 'h': return CH_h;
        case 'I': case 'i': return CH_I;
        case 'P': case 'p': return CH_P;
        case 'S': case 's': return CH_S;
        case 'T': case 't': return CH_t;
        default:            return MAX7219Display::SEG_BLANK;
    }
}

// Queues a non-blocking one-shot scroll; driven from update_display() at 300 ms/step.
static void start_scroll_splash(const uint8_t *label_segs, int label_len, const char *ip) {
    s_scroll_len     = 0;
    s_scroll_offset  = -7;
    s_scroll_step_ms = 0;
    s_scroll_holding = false;
    for (int i = 0; i < label_len && s_scroll_len < 46; i++)
        s_scroll_buf[s_scroll_len++] = label_segs[i];
    s_scroll_buf[s_scroll_len++] = MAX7219Display::SEG_BLANK;
    for (int i = 0; ip[i] && s_scroll_len < 47; i++) {
        if (ip[i] == '.') s_scroll_buf[s_scroll_len++] = MAX7219Display::SEG_BLANK;
        else               s_scroll_buf[s_scroll_len++] = char_to_seg(ip[i]);
    }
    // PIN is needed to log into the web page in every mode, so it always
    // follows the address, not just in AP mode.
    if (s_scroll_len < 47) s_scroll_buf[s_scroll_len++] = MAX7219Display::SEG_BLANK;
    for (int i = 0; g_ap_pin[i] && s_scroll_len < 47; i++)
        s_scroll_buf[s_scroll_len++] = char_to_seg(g_ap_pin[i]);
    s_scroll_active = true;
}


// ── WiFi & HTTP config server ──────────────────────────────────────────────────

static bool           s_wifi_joined      = false;
static bool           g_sta_failed       = false;
static bool           g_wifi_initialized = false;
static bool           s_wifi_use_static  = false;  // g_net==1 applied to STA netif
static esp_netif_t   *s_ap_netif         = nullptr;
static esp_netif_t   *s_sta_netif        = nullptr;
static httpd_handle_t s_httpd            = nullptr;

// g_ap_pin is declared earlier (near the other WiFi globals) so the scroll
// splash can use it. Doubles as the WPA2 password for AP mode and the HTTP
// Basic Auth password for the config page in every mode.
static char g_auth_expected[48] = {};

static void base64_encode(const char *in, char *out, size_t out_size) {
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(in), oi = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = (uint32_t)(uint8_t)in[i] << 16;
        if (i + 1 < len) n |= (uint32_t)(uint8_t)in[i + 1] << 8;
        if (i + 2 < len) n |= (uint32_t)(uint8_t)in[i + 2];
        char b[4] = { tbl[(n >> 18) & 0x3F], tbl[(n >> 12) & 0x3F],
                      (i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=',
                      (i + 2 < len) ? tbl[n & 0x3F]        : '=' };
        for (int k = 0; k < 4 && oi + 1 < out_size; k++) out[oi++] = b[k];
    }
    out[oi] = '\0';
}

// Derives the 8-digit PIN from the base MAC with a simple mixing step so it
// isn't just the MAC's digits reformatted.
static void compute_security_pin() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 6; i++) {
        h ^= mac[i];
        h *= 0x01000193u;
        h  = (h << 5) | (h >> 27);
    }
    snprintf(g_ap_pin, sizeof(g_ap_pin), "%08u", (unsigned)(h % 100000000u));

    char cred[24];
    snprintf(cred, sizeof(cred), "tapbox:%s", g_ap_pin);
    char b64[32];
    base64_encode(cred, b64, sizeof(b64));
    snprintf(g_auth_expected, sizeof(g_auth_expected), "Basic %s", b64);
}

// Gates the web config page behind the same PIN in every mode (AP and normal
// network) — the page currently has no auth at all otherwise.
static bool http_check_auth(httpd_req_t *req) {
    char hdr[64];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK
        && strcmp(hdr, g_auth_expected) == 0)
        return true;
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"tapbox\"");
    httpd_resp_send(req, "Auth required", HTTPD_RESP_USE_STRLEN);
    return false;
}

static void nvs_save_wifi() {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifi_ssid", g_wifi_ssid);
    nvs_set_str(h, "wifi_pass", g_wifi_pass);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_ota_pending(bool pending) {
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "ota_pend", pending ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

static void rtc_save_settings() {
    s_rtc.magic    = RTC_MAGIC;
    s_rtc.sigIdx   = g_sigIdx;
    s_rtc.brit     = g_brit;     s_rtc.mode     = g_mode;    s_rtc.net = g_net;
    s_rtc.micWin   = g_micWin;   s_rtc.micSlew  = g_micSlew;
    s_rtc.micThr   = g_micThr;   s_rtc.micGate  = g_micGate; s_rtc.micFreq = g_micFreq;
    for (int i = 0; i < 4; i++) { s_rtc.ip[i] = g_ip[i]; s_rtc.sn[i] = g_sn[i]; s_rtc.gt[i] = g_gt[i]; }
    strncpy(s_rtc.wifi_ssid, g_wifi_ssid, sizeof(s_rtc.wifi_ssid));
    strncpy(s_rtc.wifi_pass, g_wifi_pass, sizeof(s_rtc.wifi_pass));
}

// If NVS was erased on the OTA reboot, write the pre-OTA settings back so
// nvs_load_settings() finds them. Magic is cleared so this runs only once.
static void rtc_restore_to_nvs_if_valid() {
    if (s_rtc.magic != RTC_MAGIC) return;
    s_rtc.magic = 0;
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "sig",       s_rtc.sigIdx);
    nvs_set_i32(h, "brit",      s_rtc.brit);
    nvs_set_i32(h, "mode",      s_rtc.mode);
    nvs_set_i32(h, "mwin",      s_rtc.micWin);
    nvs_set_i32(h, "mslew",     s_rtc.micSlew);
    nvs_set_i32(h, "mthr",      s_rtc.micThr);
    nvs_set_i32(h, "mgate",     s_rtc.micGate);
    nvs_set_i32(h, "mfreq",     s_rtc.micFreq);
    nvs_set_i32(h, "net",       s_rtc.net);
    nvs_set_i32(h, "ip0",  s_rtc.ip[0]); nvs_set_i32(h, "ip1",  s_rtc.ip[1]);
    nvs_set_i32(h, "ip2",  s_rtc.ip[2]); nvs_set_i32(h, "ip3",  s_rtc.ip[3]);
    nvs_set_i32(h, "sn0",  s_rtc.sn[0]); nvs_set_i32(h, "sn1",  s_rtc.sn[1]);
    nvs_set_i32(h, "sn2",  s_rtc.sn[2]); nvs_set_i32(h, "sn3",  s_rtc.sn[3]);
    nvs_set_i32(h, "gt0",  s_rtc.gt[0]); nvs_set_i32(h, "gt1",  s_rtc.gt[1]);
    nvs_set_i32(h, "gt2",  s_rtc.gt[2]); nvs_set_i32(h, "gt3",  s_rtc.gt[3]);
    nvs_set_str(h, "wifi_ssid", s_rtc.wifi_ssid);
    nvs_set_str(h, "wifi_pass", s_rtc.wifi_pass);
    nvs_commit(h);
    nvs_close(h);
    printf("RTC: settings restored to NVS after OTA reboot\n");
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
    if (!http_check_auth(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html");
    char tmp[192];

    httpd_resp_sendstr_chunk(req,
        "<!DOCTYPE html><html><head>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>tapbox</title><style>"
        // Dark theme matching the web installer page (docs/index.html):
        // bg #0e0e0e, panels #161616, borders #222/#2a2a2a, accent #B7F7A5
        "*{box-sizing:border-box}"
        ":root{color-scheme:dark}"
        "body{font:15px -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
        "background:#0e0e0e;color:#e0e0e0;max-width:400px;margin:20px auto;padding:0 15px}"
        "h2{margin-bottom:4px;color:#fff;font-size:2rem;font-weight:800;letter-spacing:-0.02em}"
        "h2 span,label span{color:#B7F7A5}"
        "h3{margin:20px 0 6px;color:#fff;font-size:1.05rem;"
        "border-bottom:1px solid #222;padding-bottom:5px}"
        "label{display:block;margin:8px 0 2px;font-size:13px;color:#999}"
        "input,select{width:100%;padding:7px;background:#161616;color:#e0e0e0;"
        "border:1px solid #2a2a2a;border-radius:4px}"
        "input:focus,select:focus{outline:1px solid #B7F7A5}"
        "button{width:100%;padding:11px;margin-top:14px;border:none;"
        "border-radius:4px;font-size:15px;font-weight:700;cursor:pointer}"
        ".btn-net{background:#c60;color:#fff}"
        ".btn-disp{background:#B7F7A5;color:#0e0e0e}"
        ".btn-rst{background:#161616;color:#999;border:1px solid #2a2a2a}"
        "#tabs{display:flex;gap:6px;margin:14px 0 2px}"
        ".tb{flex:1;width:auto;margin-top:0;padding:9px 0;background:#161616;color:#999;"
        "border:1px solid #2a2a2a;border-radius:4px;font-size:14px;font-weight:700;cursor:pointer}"
        ".tb.on{background:#1d2417;color:#B7F7A5;border-color:#4a6b3a}"
        ".tab{display:none}.tab.on{display:block}"
        "input:disabled,select:disabled,button:disabled{opacity:.4;cursor:default}"
        "input[type=range]{padding:0;height:28px;border:none;background:none;accent-color:#B7F7A5}"
        "#mchart{width:100%;height:330px;background:#111;border:1px solid #2a2a2a;"
        "border-radius:6px;display:block}"
        "#mtop{display:flex;align-items:flex-end;gap:22px;margin:10px 0 8px}"
        ".mbig{font-variant-numeric:tabular-nums}"
        ".mbig div{font-size:30px;font-weight:800;color:#B7F7A5;width:4.2ch;"
        "text-align:right;line-height:1}"
        ".mbig span{display:block;color:#777;font-size:11px;text-transform:uppercase;"
        "letter-spacing:.05em;margin-top:2px}"
        "#mstate{display:inline-block;width:96px;text-align:center;padding:6px 0;"
        "margin-left:auto;border-radius:4px;font-size:12px;font-weight:700;background:#161616}"
        "#mstat{font-size:12px;color:#777;margin:4px 0 2px}"
        ".mnum{font-size:13px;color:#ccc;margin:2px 0;font-variant-numeric:tabular-nums}"
        ".mnum b{color:#B7F7A5;font-weight:700;display:inline-block;min-width:2.4ch;text-align:right}"
        "</style></head><body>"
        "<h2>tap<span>box</span></h2>"
        "<div id=tabs>"
        "<button type=button class=\"tb on\" onclick=\"tab(0)\">Network</button>"
        "<button type=button class=tb onclick=\"tab(1)\">Settings</button>"
        "<button type=button class=tb onclick=\"tab(2)\">BPM tuning</button>"
        "</div>"

        "<div class=\"tab on\" id=t0>"
        "<form method=post action=/save>"
        "<h3>WiFi</h3>"
        "<label>Network name (SSID)</label>");
    snprintf(tmp, sizeof(tmp), "<input name=ssid value=\"%s\">", g_wifi_ssid);
    httpd_resp_sendstr_chunk(req, tmp);
    httpd_resp_sendstr_chunk(req,
        "<label>Password</label>"
        "<input name=pass type=password placeholder=\"(leave blank to keep current)\">"
        "<h3>Network Mode</h3>"
        "<label>Applies to whichever interface is active (Ethernet or WiFi)</label>"
        "<select name=net onchange=\"updEnable()\">");
    snprintf(tmp, sizeof(tmp),
        "<option value=0%s>DHCP</option><option value=1%s>Static</option>",
        g_net == 0 ? " selected" : "", g_net == 1 ? " selected" : "");
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<option value=2%s>Access Point</option></select>",
        g_net == 2 ? " selected" : "");
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
        "<button class=btn-net>Save Network &mdash; tapbox will reboot</button>"
        "</form>"
        "</div>"

        "<div class=tab id=t1>"
        "<form method=post action=/apply>"
        "<h3>Settings</h3>"
        "<label>Time signature</label><select name=sig>");
    static const char *sigs[] = {"2","3","4","5","6","7"};
    for (int i = 0; i < 6; i++) {
        snprintf(tmp, sizeof(tmp), "<option value=%d%s>%s</option>",
            i, i == g_sigIdx ? " selected" : "", sigs[i]);
        httpd_resp_sendstr_chunk(req, tmp);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
    httpd_resp_sendstr_chunk(req, "<label>Sync mode</label><select name=mode onchange=\"updEnable()\">");
    static const char *modes[] = {"CDJ", "Audio (mic)", "Manual"};
    for (int i = 0; i < 3; i++) {
        snprintf(tmp, sizeof(tmp), "<option value=%d%s>%s</option>",
            i, i == g_mode ? " selected" : "", modes[i]);
        httpd_resp_sendstr_chunk(req, tmp);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
    httpd_resp_sendstr_chunk(req, "<label>Brightness</label><select name=brit>");
    static const char *brits[] = {"Dim", "Normal", "High", "Too Much"};
    for (int i = 0; i < 4; i++) {
        snprintf(tmp, sizeof(tmp), "<option value=%d%s>%s</option>",
            i, i == g_brit ? " selected" : "", brits[i]);
        httpd_resp_sendstr_chunk(req, tmp);
    }
    httpd_resp_sendstr_chunk(req, "</select>");
    httpd_resp_sendstr_chunk(req,
        "<button class=btn-disp>Save Settings</button>"
        "</form>"
        "</div>"

        "<div class=tab id=t2>"
        "<form method=post action=/apply>"
        "<h3>BPM Tuning</h3>"
        "<div id=mnote class=mnum style=\"display:none;color:#e90\">"
        "Audio sync is off &mdash; set Sync mode to Audio on the Settings tab</div>"
        "<div id=mtop>"
        "<div class=mbig><div id=mrawv>&hellip;</div><span>Measured BPM</span></div>"
        "<div class=mbig><div id=mtrkv>&hellip;</div><span>Link BPM</span></div>"
        "<div id=mstate>&hellip;</div>"
        "</div>"
        "<canvas id=mchart width=320 height=330></canvas>"
        "<div id=mstat>connecting&hellip;</div>"
        "<div class=mnum id=mnum2>&nbsp;</div>");

    snprintf(tmp, sizeof(tmp), "<label>Accept window: <span id=mwinv>%d</span> &plusmn;BPM</label>", g_micWin);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mwin type=range min=1 max=10 value=%d "
        "oninput=\"mwinv.textContent=this.value;applyField('mwin',this.value)\">", g_micWin);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Tempo slew: <span id=mslewv>%d</span> (0.1%%/sec)</label>", g_micSlew);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mslew type=range min=0 max=50 value=%d "
        "oninput=\"mslewv.textContent=this.value;applyField('mslew',this.value)\">", g_micSlew);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Onset threshold: <span id=mthrv>%d</span></label>", g_micThr);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mthr type=range min=0 max=30 value=%d "
        "oninput=\"mthrv.textContent=this.value;applyField('mthr',this.value)\">", g_micThr);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Noise gate: <span id=mgatev>%d</span></label>", g_micGate);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mgate type=range min=0 max=50 value=%d "
        "oninput=\"mgatev.textContent=this.value;applyField('mgate',this.value)\">", g_micGate);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Kick filter: <span id=mfreqv>%d</span> Hz</label>", g_micFreq);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mfreq type=range min=60 max=300 step=5 value=%d "
        "oninput=\"mfreqv.textContent=this.value;applyField('mfreq',this.value)\">", g_micFreq);
    httpd_resp_sendstr_chunk(req, tmp);

    httpd_resp_sendstr_chunk(req,
        "<button type=button id=mrst class=btn-rst onclick=\"mDefaults()\">Reset Audio Defaults</button>"
        "<button class=btn-disp>Save Tuning</button>"
        "</form>"
        "</div>"

        "<script>"
        "function tab(i){for(var j=0;j<3;j++){"
          "document.getElementById('t'+j).className=(j==i)?'tab on':'tab';"
          "document.getElementsByClassName('tb')[j].className=(j==i)?'tb on':'tb';"
        "}}"
        // Disable (never hide) fields that don't apply to the current modes
        "function updEnable(){"
          "var st=document.querySelector('[name=net]').value=='1';"
          "['ip','sn','gw'].forEach(function(n){document.querySelector('[name='+n+']').disabled=!st;});"
          "var au=document.querySelector('[name=mode]').value=='1';"
          "['mwin','mslew','mthr','mgate','mfreq'].forEach(function(n){document.querySelector('[name='+n+']').disabled=!au;});"
          "mrst.disabled=!au;"
          "mchart.style.opacity=au?1:.35;"
          "mnote.style.display=au?'none':'block';"
        "}"
        "updEnable();"

        "var mPending={},mTimer=null;"
        "function applyField(n,v){"
          "mPending[n]=v;"
          "if(mTimer)return;"
          "mTimer=setTimeout(function(){"
            "var body=[];for(var k in mPending)body.push(k+'='+mPending[k]);"
            "mPending={};mTimer=null;"
            "fetch('/apply',{method:'POST',"
              "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
              "body:body.join('&')});"
          "},200);"
        "}"

        // Must mirror the firmware defaults in factory_reset()
        "function mDefaults(){"
          "var d={mwin:4,mslew:10,mthr:8,mgate:17,mfreq:150};"
          "for(var k in d){"
            "document.querySelector('[name='+k+']').value=d[k];"
            "document.getElementById(k+'v').textContent=d[k];"
            "applyField(k,d[k]);"
          "}"
        "}"

        "var mHist=[],mStats=[];"
        "function mConnect(){"
          "var ws=new WebSocket('ws://'+location.host+'/ws');"
          "ws.onopen=function(){mstat.textContent='live';};"
          "ws.onclose=function(){mstat.textContent='disconnected \\u2014 retrying\\u2026';setTimeout(mConnect,2000);};"
          "ws.onerror=function(){ws.close();};"
          "ws.onmessage=function(ev){"
            "var p=ev.data.split(',');"
            "var pt={e:+p[0],b:+p[1],t:+p[2],g:+p[3],o:+p[4]};"
            "mHist.push(pt);"
            "if(mHist.length>150)mHist.shift();"
            "var raw=+p[5],trk=+p[6],st=+p[7];"
            "mrawv.textContent=raw?raw.toFixed(1):'\\u2014';"
            "mtrkv.textContent=trk?trk.toFixed(1):'\\u2014';"
            "mstate.textContent=st==2?'LOCKED':st==1?'SEARCHING':'IDLE';"
            "mstate.style.color=st==2?'#B7F7A5':st==1?'#e90':'#777';"
            "mStats.push({ts:Date.now(),d:+p[8],a:+p[9]});"
            "while(mStats.length&&Date.now()-mStats[0].ts>10000)mStats.shift();"
            "if(mStats.length>1){"
              "var f=mStats[0],l=mStats[mStats.length-1];"
              "var dd=(l.d-f.d+100000)%100000,da=(l.a-f.a+100000)%100000;"
              "mnum2.innerHTML='Last 10s: <b>'+dd+'</b> beats detected &nbsp;\\u00b7&nbsp; <b>'+da+'</b> accepted';"
            "}"
            "mDraw();"
          "};"
        "}"
        "function mDraw(){"
          "var c=document.getElementById('mchart'),ctx=c.getContext('2d'),W=c.width,H=c.height;"
          "ctx.clearRect(0,0,W,H);"
          // Fixed log scale, 5 decades (1..1e5 chart units) — never rescales.
          "function y(v){return H-Math.max(0,Math.min(Math.log10(v<1?1:v)/5,1))*H;}"
          // Faint decade gridlines (10, 100, 1k, 10k)
          "ctx.strokeStyle='#1e1e1e';ctx.lineWidth=1;ctx.setLineDash([]);"
          "for(var d=1;d<5;d++){var gy=y(Math.pow(10,d));"
            "ctx.beginPath();ctx.moveTo(0,gy);ctx.lineTo(W,gy);ctx.stroke();}"
          "var last=mHist[mHist.length-1]||{t:0,g:0};"
          "ctx.lineWidth=2;"
          "ctx.strokeStyle='#e90';ctx.setLineDash([4,3]);"
          "ctx.beginPath();ctx.moveTo(0,y(last.t));ctx.lineTo(W,y(last.t));ctx.stroke();"
          "ctx.strokeStyle='#c33';"
          "ctx.beginPath();ctx.moveTo(0,y(last.g));ctx.lineTo(W,y(last.g));ctx.stroke();"
          "ctx.setLineDash([]);"
          "ctx.strokeStyle='#0af';ctx.lineWidth=2;ctx.beginPath();"
          "for(var i=0;i<mHist.length;i++){"
            "var x=i/((mHist.length-1)||1)*W;"
            "if(i===0)ctx.moveTo(x,y(mHist[i].e));else ctx.lineTo(x,y(mHist[i].e));"
          "}"
          "ctx.stroke();"
          "ctx.fillStyle='#2ecc71';"
          "for(var i=0;i<mHist.length;i++){"
            "if(mHist[i].o){var x=i/((mHist.length-1)||1)*W;"
              "ctx.beginPath();ctx.arc(x,y(mHist[i].e),3,0,7);ctx.fill();}"
          "}"
        "}"
        "mConnect();"
        "</script></body></html>");
    httpd_resp_sendstr_chunk(req, nullptr);
    return ESP_OK;
}

static char *recv_body(httpd_req_t *req) {
    int total = req->content_len;
    if (total <= 0 || total > 1024) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad request"); return nullptr; }
    char *body = (char *)malloc(total + 1);
    if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return nullptr; }
    int received = httpd_req_recv(req, body, total);
    if (received <= 0) { free(body); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv"); return nullptr; }
    body[received] = '\0';
    return body;
}

static esp_err_t http_post_save(httpd_req_t *req) {
    if (!http_check_auth(req)) return ESP_OK;
    char *body = recv_body(req);
    if (!body) return ESP_FAIL;

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
    free(body);

    nvs_save_settings();
    nvs_save_wifi();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body>"
        "<h2>Saved. tapbox is rebooting...</h2>"
        "<p>Reconnect to your network if needed, then check the display "
        "for the new IP address.</p>"
        "</body></html>");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t http_post_apply(httpd_req_t *req) {
    if (!http_check_auth(req)) return ESP_OK;
    char *body = recv_body(req);
    if (!body) return ESP_FAIL;

    char tmp[64];
    if (form_field(body, "sig",  tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v < kSigCount)   g_sigIdx   = v; }
    if (form_field(body, "mode", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 2)          g_mode     = v; }
    if (form_field(body, "brit", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 3)           g_brit     = v; }
    if (form_field(body, "mwin", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 1 && v <= 10)           g_micWin   = v; }
    if (form_field(body, "mslew",tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 50)           g_micSlew  = v; }
    if (form_field(body, "mthr", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 30)           g_micThr   = v; }
    if (form_field(body, "mgate",tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 50)           g_micGate  = v; }
    if (form_field(body, "mfreq",tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 60 && v <= 300)          g_micFreq  = v; }
    free(body);

    nvs_save_settings();
    apply_settings();

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Live audio-tuning chart. IMPORTANT (ESP-IDF 6.x): the framework completes
// the WS handshake itself and does NOT call the uri handler for the handshake
// GET (httpd_uri.c: "do not call the uri->handler"), so there is no place to
// capture a client fd at connect time. Instead, clients are discovered on
// every push via httpd_get_client_list() + httpd_ws_get_fd_info().
//
// Not gated behind http_check_auth(): browsers don't reliably attach cached
// Basic Auth credentials to a WebSocket handshake, and this channel is
// read-only telemetry anyway — there's nothing for a client to command.
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;  // not reached on IDF 6.x, kept for safety
    // Drain any incoming frame (the page never sends any; browsers may send CLOSE)
    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) return ret;
    if (pkt.len) {
        uint8_t *buf = (uint8_t *)calloc(1, pkt.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
        free(buf);
    }
    return ret;
}

struct WsPushArg { char text[112]; };

// Runs in httpd context (via httpd_queue_work): send the frame to every
// connected websocket client.
static void ws_send_async(void *arg) {
    WsPushArg *a = (WsPushArg *)arg;
    int    fds[CONFIG_LWIP_MAX_SOCKETS];
    size_t n = CONFIG_LWIP_MAX_SOCKETS;
    if (s_httpd && httpd_get_client_list(s_httpd, &n, fds) == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            if (httpd_ws_get_fd_info(s_httpd, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
            httpd_ws_frame_t frame = {};
            frame.final   = true;
            frame.type    = HTTPD_WS_TYPE_TEXT;
            frame.payload = (uint8_t *)a->text;
            frame.len     = strlen(a->text);
            if (httpd_ws_send_frame_async(s_httpd, fds[i], &frame) != ESP_OK)
                printf("WS send failed (fd %d)\n", fds[i]);
        }
    }
    free(a);
}

// Any connected websocket client right now? Cheap check so the push task can
// idle while nobody has the tuning page open.
static bool ws_client_connected() {
    int    fds[CONFIG_LWIP_MAX_SOCKETS];
    size_t n = CONFIG_LWIP_MAX_SOCKETS;
    if (!s_httpd || httpd_get_client_list(s_httpd, &n, fds) != ESP_OK) return false;
    for (size_t i = 0; i < n; i++)
        if (httpd_ws_get_fd_info(s_httpd, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) return true;
    return false;
}

// Pushes energy/baseline/threshold/gate/onset at 20 Hz to whichever browser
// has the audio-tuning chart open — cheap to compute, so runs unconditionally
// and just does nothing while no client is connected.
static void ws_push_task(void *) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!ws_client_connected()) continue;
        WsPushArg *a = (WsPushArg *)malloc(sizeof(WsPushArg));
        if (!a) continue;
        // e,baseline,threshold,gate,onset,rawBPM,trackedBPM,state,detected,accepted
        snprintf(a->text, sizeof(a->text), "%.0f,%.0f,%.0f,%.0f,%d,%.1f,%.1f,%d,%u,%u",
                 g_tele_energy * 1e6, g_tele_baseline * 1e6,
                 g_tele_threshold * 1e6, g_tele_gate * 1e6,
                 g_tele_onset ? 1 : 0,
                 (double)g_mic_bpm, (double)g_mic_tracked,
                 g_mic_locked ? 2 : (g_mic_armed ? 1 : 0),
                 (unsigned)(g_tele_det % 100000), (unsigned)(g_tele_acc % 100000));
        g_tele_energy = 0.0;      // restart peak-hold window
        g_tele_onset  = false;    // consume the latched beat
        if (httpd_queue_work(s_httpd, ws_send_async, a) != ESP_OK) free(a);
    }
}

static void http_server_start() {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 4;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) { s_httpd = nullptr; return; }
    httpd_uri_t get_h   = { .uri = "/",      .method = HTTP_GET,  .handler = http_get_root,   .user_ctx = nullptr };
    httpd_uri_t save_h  = { .uri = "/save",  .method = HTTP_POST, .handler = http_post_save,  .user_ctx = nullptr };
    httpd_uri_t apply_h = { .uri = "/apply", .method = HTTP_POST, .handler = http_post_apply, .user_ctx = nullptr };
    httpd_uri_t ws_h    = { .uri = "/ws",    .method = HTTP_GET,  .handler = ws_handler,      .user_ctx = nullptr,
                            .is_websocket = true };
    httpd_register_uri_handler(s_httpd, &get_h);
    httpd_register_uri_handler(s_httpd, &save_h);
    httpd_register_uri_handler(s_httpd, &apply_h);
    httpd_register_uri_handler(s_httpd, &ws_h);
    printf("HTTP config server started\n");
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_wifi_joined = true;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        if (s_wifi_use_static) {
            // Static IP: there is no GOT_IP event (DHCP client is stopped), so
            // mark connected here instead, mirroring ethernet_config.h.
            esp_netif_ip_info_t info;
            if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK)
                esp_ip4addr_ntoa(&info.ip, g_wifi_ip_str, sizeof(g_wifi_ip_str));
            s_wifi_joined = true;
            g_wifi_got_ip = true;
            printf("WiFi IP (static): %s\n", g_wifi_ip_str);
            http_server_start();
        } else {
            printf("WiFi: associated — waiting for DHCP\n");
        }
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
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    strncpy((char *)ap_cfg.ap.password, g_ap_pin, sizeof(ap_cfg.ap.password));

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    http_server_start();

    g_wifi_enabled = true;
    g_wifi_as_ap   = true;
    s_wifi_joined  = false;
    g_ap_started   = true;
    printf("WiFi AP: tapbox (PIN %s) — config at 192.168.4.1\n", g_ap_pin);
}

static void wifi_start_sta() {
    wifi_common_init();
    if (g_wifi_enabled) esp_wifi_stop();
    if (!s_sta_netif) s_sta_netif = esp_netif_create_default_wifi_sta();

    s_wifi_use_static = (g_net == 1);
    if (s_wifi_use_static) {
        esp_netif_dhcpc_stop(s_sta_netif);
        esp_netif_ip_info_t info = {};
        info.ip.addr      = htonl(((uint32_t)g_ip[0]<<24)|((uint32_t)g_ip[1]<<16)|((uint32_t)g_ip[2]<<8)|(uint32_t)g_ip[3]);
        info.netmask.addr = htonl(((uint32_t)g_sn[0]<<24)|((uint32_t)g_sn[1]<<16)|((uint32_t)g_sn[2]<<8)|(uint32_t)g_sn[3]);
        info.gw.addr      = htonl(((uint32_t)g_gt[0]<<24)|((uint32_t)g_gt[1]<<16)|((uint32_t)g_gt[2]<<8)|(uint32_t)g_gt[3]);
        esp_netif_set_ip_info(s_sta_netif, &info);
    }

    wifi_config_t sta_cfg = {};
    strncpy((char *)sta_cfg.sta.ssid,     g_wifi_ssid, sizeof(sta_cfg.sta.ssid));
    strncpy((char *)sta_cfg.sta.password, g_wifi_pass, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    esp_wifi_start();
    esp_wifi_connect();

    g_wifi_enabled = true;
    g_wifi_as_ap   = false;
    s_wifi_joined  = false;
    printf("WiFi STA: connecting to %s%s\n", g_wifi_ssid, s_wifi_use_static ? " (static IP)" : "");
}

static void wifi_stop() {
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = nullptr; }
    if (g_wifi_enabled) { esp_wifi_stop(); g_wifi_enabled = false; }
    printf("WiFi stopped\n");
}

// Credentials already loaded into g_wifi_ssid/g_wifi_pass by nvs_load_settings() at boot.
static void wifi_init() {
    if (g_net == 2 || g_wifi_ssid[0] == '\0') wifi_start_ap();
    else                                        wifi_start_sta();
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

static void capture_net_snapshot() {
    g_net_snap = g_net;
}

static bool net_config_changed() {
    return g_net != g_net_snap;
}

static void exit_menu() {
    if (appMode == MODE_MENU_EDIT && menuItem == MENU_BRIT)
        display.setIntensity(kBritLevels[g_brit]);
    appMode = MODE_NORMAL;
    if (net_config_changed()) show_boot_reboot();  // one reboot to apply net changes
}

static double current_link_bpm() {
    abl_link_capture_app_session_state(s_link, s_session);
    return abl_link_tempo(s_session);
}

static void do_tap() {
    // CDJ is ground truth only while in CDJ mode and a player is broadcasting
    if (g_mode == MODE_CDJ && g_cdj_active) return;

    TapResult r = tapTempo.tap();

    if (g_mode == MODE_AUDIO) {
        // Audio mode: the mic supplies tempo, the tap supplies the downbeat.
        // bpmChanged (4+ taps) covers both the first go-live AND every later
        // re-tap; wentLive only fires once ever, so we must use bpmChanged.
        if (r.bpmChanged) {
            // 4 taps → tempo override; re-anchors the mic tracker on it
            go_live(tapTempo.bpm(), tapTempo.sessionStartMs());
            g_mic_tracked   = tapTempo.bpm();
            g_mic_tapAnchor = tapTempo.bpm();
            g_mic_armed     = true;
            printf("Audio: tap override %.1f BPM\n", tapTempo.bpm());
        } else if (g_mic_armed) {
            // Already tracking → a lone tap only re-asserts the downbeat at the
            // current tempo. The mic owns BPM, so do NOT change it.
            if (linkEnabled) reset_downbeat();
            printf("Audio: downbeat re-synced at %.1f BPM\n", (double)g_mic_tracked);
        } else {
            // First tap → arm the tracker with the mic's current estimate,
            // downbeat = now.
            double seed = (g_mic_bpm > 0.0) ? (double)g_mic_bpm
                        : (linkEnabled ? current_link_bpm() : 120.0);
            if (!linkEnabled) go_live(seed, now_ms());   // downbeat = this tap
            else { set_link_tempo(seed); reset_downbeat(); }
            g_mic_tracked   = seed;
            g_mic_tapAnchor = seed;
            g_mic_armed     = true;
            printf("Audio: armed at %.1f BPM (mic %.1f)\n", seed, (double)g_mic_bpm);
        }
        return;
    }

    // CDJ-mode fallback (no player present) or Manual mode → classic tap tempo
    if (r.wentLive) {
        go_live(tapTempo.bpm(), tapTempo.sessionStartMs());
        g_manual_locked = true;
        printf("Live: %.1f BPM\n", tapTempo.bpm());
    } else if (r.bpmChanged && linkEnabled) {
        set_link_tempo(tapTempo.bpm());
        g_manual_locked = true;
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
            if (menuItem == MENU_BRIT) display.setIntensity(kBritLevels[menuEditVal]);
            menuEnteredAt = now;
            break;
        }
        case MODE_OTA_CONFIRM:
        case MODE_MENU_CONFIRM:
            appMode = MODE_NORMAL;
            break;
        default: break;
    }
}

// ── Button polling ─────────────────────────────────────────────────────────────

// Returns true if button is currently held. Sets fell=true on press edge,
// short_rise=true on release edge when no long-press fired.
static bool btn_poll(BtnCtx &ctx, gpio_num_t pin, uint32_t now,
                     bool &fell, bool &short_rise) {
    int reading = gpio_get_level(pin);
    fell = short_rise = false;
    if (reading != ctx.last && (now - ctx.last_db) >= DEBOUNCE_MS) {
        ctx.last_db = now;
        if (reading == 0) {
            ctx.pressed_at = now;
            ctx.long_fired = false;
            fell = true;
        } else {
            if (!ctx.long_fired) short_rise = true;
        }
    }
    ctx.last = reading;
    return reading == 0;
}

static void handle_button() {
    uint32_t now = now_ms();
    bool fell, short_rise;
    bool held = btn_poll(s_tap_ctx, PIN_TAP_BUTTON, now, fell, short_rise);

    if (s_bh_state != BH_IDLE) return;

    if (fell) {
        s_tap_ctx.auto_incr_at = now;
        if (appMode == MODE_NORMAL) do_tap();
    }
    if (short_rise) on_button_short_press();

    // Auto-increment while held in edit modes (starts at 500 ms, speeds up at 1500 ms)
    if (held && appMode == MODE_MENU_EDIT) {
        uint32_t held_ms  = now - s_tap_ctx.pressed_at;
        uint32_t interval = (held_ms < 1500) ? 200 : 50;
        if (held_ms >= 500 && (now - s_tap_ctx.auto_incr_at) >= interval) {
            s_tap_ctx.auto_incr_at = now;
            s_tap_ctx.long_fired   = true;
            int lo = menu_val_min(menuItem), hi = menu_val_max(menuItem);
            menuEditVal = lo + ((menuEditVal - lo + 1 + (hi - lo + 1)) % (hi - lo + 1));
            if (menuItem == MENU_BRIT) display.setIntensity(kBritLevels[menuEditVal]);
            menuEnteredAt = now;
        }
    }

}

// Select button: confirm/enter on short press; back/exit on long press
static void on_select_short_press() {
    uint32_t now = now_ms();
    switch (appMode) {
        case MODE_NORMAL:
            capture_net_snapshot();
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
            break;
        case MODE_MENU_NAV:
            if (menuItem == MENU_DONE) {
                exit_menu();
            } else if (menuItem == MENU_VER) {
                menuEnteredAt = now;
            } else if (menuItem == MENU_CURIP) {
                // Prefix the scroll with the active interface, matching the boot
                // splash labels: Eth / StA (WiFi station) / AP.
                static const uint8_t kEth[] = { CH_e, CH_t, CH_h };
                static const uint8_t kSta[] = { CH_S, CH_t, CH_A };
                static const uint8_t kAp[]  = { CH_A, CH_P };
                const uint8_t *label = nullptr;
                int label_len = 0;
                const char *ip;
                if (ethConnected) {
                    label = kEth; label_len = 3; ip = ethIPStr;
                } else if (g_wifi_enabled && !g_wifi_as_ap) {
                    label = kSta; label_len = 3; ip = g_wifi_ip_str;
                } else if (g_wifi_as_ap) {
                    label = kAp;  label_len = 2; ip = "192.168.4.1";
                } else {
                    ip = "0.0.0.0";  // disconnected: no interface label
                }
                start_scroll_splash(label, label_len, ip);
                menuEnteredAt = now;
            } else {
                menuEditVal   = menu_get_val(menuItem);
                appMode       = MODE_MENU_EDIT;
                menuEnteredAt = now;
            }
            break;
        case MODE_MENU_EDIT: {
            // Network changes reboot once on menu exit (see exit_menu /
            // check_menu_timeout), so mode edits just commit here.
            menu_commit(menuItem, menuEditVal);
            apply_settings();
            nvs_save_settings();
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now;
            break;
        }
        case MODE_OTA_CONFIRM:
            trigger_ota_pending();  // does not return
            break;
        case MODE_MENU_CONFIRM:
            factory_reset();  // does not return
            break;
    }
}

static void on_select_long_press() {
    switch (appMode) {
        case MODE_MENU_EDIT:
            // Back out of editing without committing the in-progress value.
            appMode       = MODE_MENU_NAV;
            menuEnteredAt = now_ms();
            break;
        case MODE_MENU_NAV:
        case MODE_MENU_CONFIRM:
        case MODE_OTA_CONFIRM:
            exit_menu();
            break;
        default: break;
    }
}

static void handle_select() {
    uint32_t now = now_ms();
    bool fell, short_rise;
    bool held = btn_poll(s_sel_ctx, PIN_SELECT, now, fell, short_rise);
    (void)fell;

    if (s_bh_state != BH_IDLE) return;

    if (short_rise) on_select_short_press();

    if (held && !s_sel_ctx.long_fired && (now - s_sel_ctx.pressed_at) >= SELECT_LONG_MS) {
        s_sel_ctx.long_fired = true;
        on_select_long_press();
    }
}

static void check_menu_timeout() {
    if (appMode == MODE_NORMAL) return;
    uint32_t now = now_ms();
    if (now - menuEnteredAt < MENU_TIMEOUT_MS) return;
    if (appMode == MODE_MENU_EDIT && menuItem == MENU_BRIT)
        display.setIntensity(kBritLevels[g_brit]);
    if (menuItem == MENU_DONE) menuItem = 0;
    appMode = MODE_NORMAL;
    if (net_config_changed()) show_boot_reboot();  // one reboot to apply net changes
}

// Both-button hold combo: 3 s → OTA pending + reboot; 8 s → factory reset confirm
static void handle_system_buttons() {
    uint32_t now = now_ms();
    bool tap_held = (gpio_get_level(PIN_TAP_BUTTON) == 0);
    bool sel_held = (gpio_get_level(PIN_SELECT) == 0);
    bool both = tap_held && sel_held;

    // Release handling runs regardless of appMode so state always gets cleaned up
    if (!both && s_bh_state != BH_IDLE) {
        if (s_bh_state == BH_OTA && appMode == MODE_MENU_NAV) {
            appMode       = MODE_OTA_CONFIRM;
            menuEnteredAt = now;
        }
        s_tap_ctx.long_fired = true;  // always suppress button release after a combo
        s_sel_ctx.long_fired = true;
        s_bh_state = BH_IDLE;
        s_bh_since = 0;
        return;
    }

    if (both) {
        if (s_bh_since == 0) s_bh_since = now;
        uint32_t ms = now - s_bh_since;

        // Always suppress individual button handlers when both are held
        if (s_bh_state == BH_IDLE) s_bh_state = BH_HELD;

        // 3 s / 8 s actions only fire from the menu
        if (appMode == MODE_MENU_NAV) {
            menuEnteredAt = now;  // keep menu alive during hold (prevents 6 s timeout)
            if (ms >= 8000 && s_bh_state == BH_OTA) {
                s_bh_state           = BH_RESET;
                appMode              = MODE_MENU_CONFIRM;
                menuEnteredAt        = now;
                s_tap_ctx.long_fired = true;  // prevent spurious short-press on release
                s_sel_ctx.long_fired = true;
            } else if (ms >= 3000 && s_bh_state == BH_HELD) {
                s_bh_state = BH_OTA;
                uint8_t segs[8] = {};
                memcpy(segs, kOtaLabel, 4);
                segs[4] = segs[5] = segs[6] = segs[7] = MAX7219Display::SEG_DASH;
                display.setSegments(segs);
            }
        }
    }
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
    } else if (strcmp(addr, "/nudge") == 0) {
        int ms = (args_len >= 4) ? ((tags[1] == 'f') ? (int)osc_float(args) : (int)(int32_t)osc_u32(args))
                                  : DEFAULT_NUDGE_MS;
        nudge_phase(ms * 1000);
        printf("OSC nudge %dms\n", ms);
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

// ── CDJ Pro DJ Link listener (Tier 1: passive beat tracking) ──────────────────

#define CDJ_PORT       50001
#define CDJ_TIMEOUT_MS 2000

static const uint8_t kCdjMagic[] = {
    0x51, 0x73, 0x70, 0x74, 0x31, 0x57, 0x6d, 0x4a, 0x4f, 0x4c
};

struct CdjPlayer {
    double   bpm        = 0.0;
    uint8_t  beatInBar  = 0;   // 1–4; 0 = not yet received
    uint32_t lastBeatMs = 0;
};

static CdjPlayer s_cdj_players[5];  // index 1–4; 0 unused

static bool cdj_parse_beat(const uint8_t *buf, int len) {
    if (len < 96) return false;
    if (memcmp(buf, kCdjMagic, 10) != 0) return false;
    if (buf[0x0a] != 0x28) return false;

    int player = buf[0x21];
    if (player < 1 || player > 4) return false;

    uint16_t rawBpm   = ((uint16_t)buf[0x5a] << 8) | buf[0x5b];
    uint32_t rawPitch = ((uint32_t)buf[0x54] << 24) | ((uint32_t)buf[0x55] << 16) |
                        ((uint32_t)buf[0x56] <<  8) |  (uint32_t)buf[0x57];
    double effectiveBpm = (rawBpm / 100.0) * ((double)rawPitch / 1048576.0);

    if (effectiveBpm < 20.0 || effectiveBpm > 400.0) return false;

    s_cdj_players[player].bpm        = effectiveBpm;
    s_cdj_players[player].beatInBar  = buf[0x5c];
    s_cdj_players[player].lastBeatMs = now_ms();
    return true;
}

// Returns the lowest-numbered player that sent a beat packet within CDJ_TIMEOUT_MS.
static int cdj_active_player() {
    uint32_t now = now_ms();
    for (int p = 1; p <= 4; p++) {
        if (s_cdj_players[p].lastBeatMs > 0 &&
            (now - s_cdj_players[p].lastBeatMs) < CDJ_TIMEOUT_MS)
            return p;
    }
    return 0;
}

static void cdj_task(void *) {
    abl_link_session_state cdj_sess = abl_link_create_session_state();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { vTaskDelete(nullptr); return; }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(CDJ_PORT);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    // 200 ms receive timeout so the loop can check the mode even without traffic
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static uint8_t buf[256];
    uint8_t prev_bb  = 0;
    int     prev_best = 0;

    while (true) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n > 0) cdj_parse_beat(buf, n);

        if (g_mode != MODE_CDJ || !linkEnabled) {
            g_cdj_active = false;
            prev_bb   = 0;
            prev_best = 0;
            continue;
        }

        int best = cdj_active_player();
        if (best > 0) {
            g_cdj_active     = true;
            double  bpm      = s_cdj_players[best].bpm;
            uint8_t bb       = s_cdj_players[best].beatInBar;
            uint64_t t       = now_us();

            // Reset phase tracking when switching players
            if (best != prev_best) { prev_bb = 0; prev_best = best; }

            abl_link_capture_app_session_state(s_link, cdj_sess);
            abl_link_set_tempo(cdj_sess, bpm, t);

            // Snap Link bar to CDJ downbeat whenever beat-in-bar jumps to 1
            if (bb == 1 && prev_bb != 1)
                abl_link_force_beat_at_time(cdj_sess, 0.0, t, linkQuantum);

            abl_link_commit_app_session_state(s_link, cdj_sess);
            prev_bb = bb;
        } else {
            g_cdj_active = false;
            prev_bb      = 0;
            prev_best    = 0;
        }
    }
}

// ── Status ─────────────────────────────────────────────────────────────────────

static void print_status() {
    static uint32_t lastPrint = 0;
    uint32_t now = now_ms();
    if (now - lastPrint < 2000) return;
    lastPrint = now;
    if (!linkEnabled) { printf("Cold start — tap 4 times to go live\n"); return; }
    abl_link_capture_app_session_state(s_link, s_session);
    static const char *kModeStr[] = { "CDJ", "Audio", "Manual" };
    printf("Link: %.2f BPM  sig: %.0f  peers: %llu  eth: %s  mode: %s%s\n",
           abl_link_tempo(s_session), linkQuantum,
           (unsigned long long)abl_link_num_peers(s_link),
           ethConnected ? ethIPStr : "disconnected",
           kModeStr[g_mode],
           (g_mode == MODE_CDJ && g_cdj_active) ? " (cdj active)"
           : (g_mode == MODE_AUDIO && g_mic_locked) ? " (mic locked)" : "");
}

// ── GPIO init ──────────────────────────────────────────────────────────────────

static void init_gpio() {
    // Tap (IO35) and Select (IO39) are input-only pins with NO internal pulls —
    // external 10k pullups to 3V3 are required on the board.
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_TAP_BUTTON) | (1ULL << PIN_SELECT),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);
}

// ── INMP441 I2S microphone ──────────────────────────────────────────────────────

static void init_i2s_mic() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, nullptr, &s_i2s_rx) != ESP_OK) {
        printf("I2S: channel alloc failed\n");
        s_i2s_rx = nullptr;
        return;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    // INMP441 channel selection on the ESP32 is finicky; in bring-up, MONO mode
    // returned all-zero samples on both slot settings, so we capture BOTH slots
    // in stereo and use only the left (mic) samples (see BEAT_DETECTION.md §3).
    // INMP441 L/R tied to GND → data is on the left slot.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    if (i2s_channel_init_std_mode(s_i2s_rx, &std_cfg) != ESP_OK ||
        i2s_channel_enable(s_i2s_rx) != ESP_OK) {
        printf("I2S: init/enable failed\n");
        s_i2s_rx = nullptr;
    }
}

// Energy-based kick-band beat detector with tap-anchored, octave-folded,
// slew-limited BPM tracking. See [[project-inmp441-plan]].
static void mic_task(void *) {
    if (!s_i2s_rx) { vTaskDelete(nullptr); return; }
    abl_link_session_state mic_sess = abl_link_create_session_state();

    static int32_t samples[256];
    double   lp = 0.0, hp = 0.0, xprev = 0.0;  // band-pass state
    double   baseline = 0.0;                    // adaptive energy floor
    uint32_t lastOnset = 0, lastApply = 0, lastDbg = 0;
    uint64_t lastOnsetUs = 0;                   // precise (sub-frame) onset time
    uint64_t lastAccUs   = 0;                   // last ACCEPTED onset — beat-chain reference
    int      consec = 0;
    double   avgInterval   = 0.0;               // EMA of clean inter-beat interval (ms)
    double   lastTapAnchor = 0.0;               // detect re-arm → reset the average
    while (true) {
        size_t br = 0;
        if (i2s_channel_read(s_i2s_rx, samples, sizeof(samples), &br, pdMS_TO_TICKS(100)) != ESP_OK)
            continue;
        int n = (int)(br / sizeof(int32_t));
        if (n <= 0) continue;

        // Single-pole low-pass coefficient for the current cutoff (recomputed
        // every block so the tuning slider takes effect live).
        double alpha = 1.0 - exp(-2.0 * M_PI * (double)g_micFreq / MIC_SAMPLE_RATE);

        double energy  = 0.0;
        int    cnt     = 0;
        double peakE   = 0.0;                               // sub-frame onset timing:
        int    peakIdx = 0;                                 // loudest sample in the block
        for (int i = 0; i < n; i += 2) {                   // left slot only (mic data)
            double x = (double)samples[i] / 2147483648.0;  // normalize to ~[-1,1)
            double y = x - xprev + 0.995 * hp;             // DC blocker (high-pass)
            xprev = x; hp = y;
            lp += (y - lp) * alpha;                         // low-pass → kick band
            double e2 = lp * lp;
            energy += e2;
            if (e2 > peakE) { peakE = e2; peakIdx = cnt; }
            cnt++;
        }
        energy /= (cnt > 0 ? cnt : 1);
        baseline += (energy - baseline) * 0.01;            // slow adaptive floor (~0.4 s)

        // Sub-frame onset time: timestamp the loudest sample in the block instead of
        // the read boundary. This beats the read-block quantization (the resolution
        // floor) — the kick's energy peak is a stable per-beat reference.
        uint64_t tEnd    = now_us();
        uint64_t blockUs = (uint64_t)cnt * 1000000ULL / MIC_SAMPLE_RATE;
        uint64_t tPeak   = tEnd - blockUs + (uint64_t)peakIdx * 1000000ULL / MIC_SAMPLE_RATE;

        uint32_t now = now_ms();

        if (g_mode != MODE_AUDIO) { g_mic_armed = false; g_mic_locked = false; consec = 0; avgInterval = 0.0; }

        double threshFactor = 1.0 + (double)g_micThr / 10.0;
        // Log mapping: slider 1–50 → ~1e-6..1e-1 normalized energy. Linear was
        // capped at 5e-4 — the whole range sat in the bottom sliver of the
        // tuning chart and couldn't reach loud-venue levels at all.
        double gate = (g_micGate > 0) ? pow(10.0, (double)g_micGate / 10.0) * 1e-6 : 0.0;
        bool onset = (energy > baseline * threshFactor) &&
                     (energy > gate) &&
                     (lastOnsetUs == 0 || (tPeak - lastOnsetUs) > 250000ULL);  // refractory → ≤240 BPM

        if (energy > g_tele_energy) g_tele_energy = energy;  // peak-hold until next push
        g_tele_baseline  = baseline;
        g_tele_threshold = baseline * threshFactor;
        g_tele_gate      = gate;
        if (onset) { g_tele_onset = true; g_tele_det = g_tele_det + 1; }  // latched until next push

        if (onset) {
            if (lastOnsetUs != 0) {
                double interval = (double)(tPeak - lastOnsetUs) / 1000.0;  // ms, sub-frame precise
                double bpm = 60000.0 / interval;
                if (bpm >= 50.0 && bpm <= 220.0) g_mic_bpm = bpm;   // raw hint for display

                if (g_mode == MODE_AUDIO && g_mic_armed) {
                    // Judge acceptance against the STABLE tapped tempo, not the
                    // wandering output — otherwise a small drift makes the window
                    // admit one interval tail and reject the other, ratcheting away.
                    double anchor = (g_mic_tapAnchor > 0.0) ? g_mic_tapAnchor : g_mic_tracked;
                    if (anchor != lastTapAnchor) { avgInterval = 0.0; lastTapAnchor = anchor; lastAccUs = 0; }

                    // Measure spacing from the last ACCEPTED beat when we have one —
                    // a spurious detection between two kicks then can't shatter the
                    // beat chain, and the octave fold below absorbs 2-4 beat spans.
                    // Before the first acceptance (or after the chain goes stale)
                    // fall back to consecutive-onset spacing.
                    double aInt = interval;
                    if (lastAccUs != 0) aInt = (double)(tPeak - lastAccUs) / 1000.0;
                    double aBpm = 60000.0 / aInt;
                    if (aBpm < 25.0) {          // chain stale (>~4 beats) — restart it
                        lastAccUs = 0;
                        aInt = interval;
                        aBpm = bpm;
                    }
                    if (aBpm >= 25.0 && aBpm <= 220.0) {
                        double cand = aBpm;
                        while (cand > anchor * 1.4) cand *= 0.5;   // fold octaves to anchor
                        while (cand < anchor * 0.7) cand *= 2.0;
                        double tol = (double)g_micWin;   // accept window, ± BPM
                        if (fabs(cand - anchor) <= tol) {
                            g_tele_acc = g_tele_acc + 1;
                            lastAccUs = tPeak;
                            // Average the clean ~1-beat interval (EMA) → accurate, continuous
                            // tempo, unbiased (interval domain) and free of the 8 ms grid.
                            if (cand == aBpm) {                      // un-folded → true 1-beat
                                if (avgInterval <= 0.0) avgInterval = aInt;
                                else                    avgInterval += 0.08 * (aInt - avgInterval);
                            }
                            double target = (avgInterval > 0.0) ? (60000.0 / avgInterval) : cand;

                            // Deadband: once trk is within ~0.4 BPM of the target, freeze it
                            // so the displayed tempo stops flickering. Genuine drift beyond
                            // the band still moves it, slew-limited (0.1%/sec).
                            double delta = target - g_mic_tracked;
                            if (fabs(delta) > 0.4) {
                                double dt      = (lastApply ? (now - lastApply) : 500) / 1000.0;
                                double maxStep = (double)g_micSlew * 0.001 * g_mic_tracked * dt;
                                if (delta >  maxStep) delta =  maxStep;
                                if (delta < -maxStep) delta = -maxStep;
                                g_mic_tracked += delta;
                            }
                            // Clamp ±20% of the tapped tempo (octave/subharmonic guard)
                            double lo = anchor * 0.80, hi = anchor * 1.20;
                            if (g_mic_tracked < lo) g_mic_tracked = lo;
                            if (g_mic_tracked > hi) g_mic_tracked = hi;
                            lastApply = now;
                            lastOnset = now;  // last ACCEPTED beat — drives the lock-timeout below
                            if (++consec >= 4) g_mic_locked = true;

                            if (linkEnabled) {
                                uint64_t t = now_us();
                                abl_link_capture_app_session_state(s_link, mic_sess);
                                abl_link_set_tempo(mic_sess, g_mic_tracked, t);
                                // Phase-lock (only once locked): gently pull the grid so
                                // the detected kick lands on the NEAREST beat. Nearest-beat
                                // correction preserves the tapped bar/downbeat.
                                if (g_mic_locked) {
                                    double beat    = abl_link_beat_at_time(mic_sess, t, linkQuantum);
                                    double nearest = floor(beat + 0.5);
                                    double fixed   = beat + 0.15 * (nearest - beat);  // low gain
                                    abl_link_force_beat_at_time(mic_sess, fixed, t, linkQuantum);
                                }
                                abl_link_commit_app_session_state(s_link, mic_sess);
                            }
                        }
                        // No consec reset on a windowed-out value → lock stays sticky
                    }
                }
            }
            lastOnsetUs = tPeak;
        }

        // Drop the lock if no ACCEPTED beat (lastOnset, set only on acceptance
        // above) has reinforced it recently — raw detections that never pass
        // the accept window must not keep the lock alive. Hold = 6 beat-periods
        // (min 3.5s) so slow tempos get the same tolerance in musical time.
        uint32_t lockHold = (g_mic_tracked > 60.0)
                          ? (uint32_t)(6.0 * 60000.0 / g_mic_tracked) : 6000;
        if (lockHold < 3500) lockHold = 3500;
        if (g_mic_locked && (now - lastOnset) > lockHold) { g_mic_locked = false; consec = 0; avgInterval = 0.0; }

        // Tuning telemetry while in Audio mode
        if (g_mode == MODE_AUDIO && (now - lastDbg) >= 500) {
            lastDbg = now;
            printf("mic e=%.0f base=%.0f thrx=%.2f gate=%.0f raw=%.1f trk=%.1f armed=%d lock=%d\n",
                   energy * 1e6, baseline * 1e6, threshFactor, gate * 1e6,
                   (double)g_mic_bpm, (double)g_mic_tracked,
                   g_mic_armed ? 1 : 0, g_mic_locked ? 1 : 0);
        }
    }
}

// ── Entry point ────────────────────────────────────────────────────────────────

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    rtc_restore_to_nvs_if_valid();

    esp_event_loop_create_default();
    esp_netif_init();

    compute_security_pin();

    init_gpio();
    init_i2s_mic();
    display.init();

    // Earliest possible "alive" indicator: light every segment the moment the
    // display is up, so the operator can see the board booted and the display
    // works — well before the (slow) Ethernet link and IP splash appear. This
    // persists through the Ethernet wait until the splash/normal view replaces it.
    {
        display.setIntensity(8);
        uint8_t boot[8];
        for (int i = 0; i < 8; i++) boot[i] = 0xFF;  // all segments + DP
        display.setSegments(boot);
    }

    nvs_load_settings();  // loads all settings including WiFi credentials

    if (esp_reset_reason() == ESP_RST_BROWNOUT) {
        g_brit = 0;
        display.setIntensity(kBritLevels[0]);
        printf("Brownout detected — brightness forced to level 1\n");
    }

    // Check for OTA pending flag (set by holding both buttons in normal mode)
    bool ota_pending = false;
    {
        nvs_handle_t nvs_h;
        if (nvs_open("settings", NVS_READONLY, &nvs_h) == ESP_OK) {
            int32_t v = 0;
            if (nvs_get_i32(nvs_h, "ota_pend", &v) == ESP_OK) ota_pending = (v == 1);
            nvs_close(nvs_h);
        }
    }
    if (ota_pending) nvs_save_ota_pending(false);  // clear flag immediately

    if (g_net != 2) {
        initEthernet(g_net, g_ip, g_sn, g_gt);

        // Static IP only needs link-up (~2 s); DHCP needs IP assignment (~5 s).
        uint32_t eth_timeout = (g_net == 1) ? 3000 : 5000;
        printf("Waiting for Ethernet");
        uint32_t start = now_ms();
        while (!ethConnected && (now_ms() - start) < eth_timeout) {
            vTaskDelay(pdMS_TO_TICKS(250));
            printf(".");
            fflush(stdout);
        }
        printf("\n");
    }

    if (!ethConnected) wifi_init();

    // Register after the boot wait so the initial IP event doesn't trigger switching
    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, eth_lost_cb,     nullptr);
    esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_CONNECTED,    eth_connected_cb, nullptr);
    esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP,         eth_got_ip_cb,   nullptr);

    s_link    = abl_link_create(120.0);
    s_session = abl_link_create_session_state();
    tapTempo.setBpm(120.0);

    if (ethConnected) {
        printf("IP: %s\n", ethIPStr);
        abl_link_enable(s_link, true);
        linkEnabled = true;
        abl_link_capture_app_session_state(s_link, s_session);
        abl_link_set_tempo(s_session, 120.0, now_us());
        abl_link_commit_app_session_state(s_link, s_session);
        printf("Link live at 120.0 BPM\n");
        http_server_start();
        if (ota_pending) { ota_pending = false; perform_ota(); }
        else { static const uint8_t kEth[] = { CH_e, CH_t, CH_h }; start_scroll_splash(kEth, 3, ethIPStr); }
    } else {
        printf("No Ethernet — waiting for WiFi\n");
    }

    // Confirm to the bootloader that this firmware booted successfully.
    // If the device was just OTA-updated, this prevents rollback on next boot.
    esp_ota_mark_app_valid_cancel_rollback();

    xTaskCreate(osc_task, "osc", 4096, nullptr, 5, nullptr);
    printf("OSC listening on port %d\n", OSC_PORT);
    xTaskCreate(cdj_task, "cdj", 4096, nullptr, 5, nullptr);
    printf("CDJ listener on port %d\n", CDJ_PORT);
    xTaskCreate(mic_task, "mic", 4096, nullptr, 5, nullptr);
    printf("Mic beat detector started\n");
    xTaskCreate(ws_push_task, "ws_push", 3072, nullptr, 4, nullptr);

    while (true) {
        handle_system_buttons();
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
        if (g_wifi_got_ip) {
            g_wifi_got_ip = false;
            abl_link_enable(s_link, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            abl_link_enable(s_link, true);
            linkEnabled = true;
            printf("Link re-enabled on WiFi interface\n");
            if (ota_pending) { ota_pending = false; perform_ota(); }
            else { static const uint8_t kSta[] = { CH_S, CH_t, CH_A }; start_scroll_splash(kSta, 3, g_wifi_ip_str); }
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
            http_server_start();
            printf("Link re-enabled on Ethernet interface\n");
            if (ota_pending) { ota_pending = false; perform_ota(); }
            else { static const uint8_t kEth[] = { CH_e, CH_t, CH_h }; start_scroll_splash(kEth, 3, ethIPStr); }
        }
        if (g_ap_started) {
            g_ap_started = false;
            abl_link_enable(s_link, false);
            vTaskDelay(pdMS_TO_TICKS(100));
            abl_link_enable(s_link, true);
            linkEnabled = true;
            static const uint8_t kAp[] = { CH_A, CH_P };
            start_scroll_splash(kAp, 2, "192.168.4.1");
        }
        update_display();
        print_status();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
