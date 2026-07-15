#include <stdio.h>
#include <cmath>
#include <string.h>
#include <cstdarg>
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
#include "driver/ledc.h"
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

#include "dsp/ring_buffer.h"
#include "dsp/fft_processor.h"
#include "dsp/mel_filterbank.h"
#include "dsp/superflux_onset.h"
#include "dsp/btrack.h"

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
#define PIN_LINE_SCKI   GPIO_NUM_5   // PCM1808 SCKI — LEDC 8MHz (see PCM1808_PLAN.md)
#define PIN_LINE_BCK    GPIO_NUM_17  // PCM1808 BCK  (2MHz, PCM1808-generated → input)
#define PIN_LINE_WS     GPIO_NUM_32  // PCM1808 LRC  (31.25kHz, PCM1808-generated → input)
#define PIN_LINE_DIN    GPIO_NUM_33  // PCM1808 OUT  (audio data → input)
#define LINE_SAMPLE_RATE 31250       // 8MHz SCKI ÷ 256fs

// ── Constants ──────────────────────────────────────────────────────────────────
#define DEBOUNCE_MS     20  // lockout after an accepted edge; Sanwa OBSF release chatter exceeds the old 5ms
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
static constexpr uint8_t CH_i = 0x10;  // lowercase i: segment C only
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
static constexpr uint8_t CH_y    = 0x3B;  // segments B,C,D,F,G

// Mode indicator bars (single horizontal segment, see project memory for encoding)
static constexpr uint8_t BAR_TOP = 0x40;  // segment A  → CDJ mode
static constexpr uint8_t BAR_MID = 0x01;  // segment G  → Manual mode
static constexpr uint8_t BAR_BOT = 0x08;  // segment D  → Audio mode

// ── Firmware version ───────────────────────────────────────────────────────────
#define FW_MAJOR 1
#define FW_MINOR 14
#define FW_PATCH 2

// ── Menu option tables ─────────────────────────────────────────────────────────
static const double kSignatures[] = { 2.0, 3.0, 4.0, 5.0, 6.0, 7.0 };
static const int    kSigCount     = 6;

#define DEFAULT_NUDGE_MS 20  // used by OSC /nudge when no argument is given

static const int    kBritLevels[] = { 1, 5, 10, 15 };  // user levels 1-4 → MAX7219 intensity
static const int    kBritCount    = 4;

// ── Sync source (mutually exclusive) ────────────────────────────────────────────
// Values are NVS-persisted — append only, never renumber (a stored 2 must stay
// tap mode across upgrades). MODE_LINE (PCM1808 line-in) appended 2026-07-14.
enum SyncMode { MODE_CDJ = 0, MODE_MIC = 1, MODE_MANUAL = 2, MODE_LINE = 3 };
// Mic and Line-in are both "audio" sources: same tap-anchor/range-gate/lock
// behavior, different capture hardware and sample rate.
static inline bool mode_is_audio(int m) { return m == MODE_MIC || m == MODE_LINE; }

// ── Persistent settings ────────────────────────────────────────────────────────
static int g_sigIdx   = 2;             // kSignatures[2] = 4/4
static int g_brit     = 1;             // brightness level index 0–3 (→ kBritLevels)
static int g_net      = 0;             // 0=DHCP, 1=static
static int g_mode     = MODE_MIC;    // 0=CDJ, 1=Mic, 2=Tap (manual), 3=Line-in
// Mic tuning knobs — the real tempo path's four gates (see mic_task),
// exposed as sliders because none are derivable analytically.
static int g_micFloor    = -100;       // absolute level floor, dB of mean FFT power (-100 = off, up to -10)
static int g_micConfSil  = 30;         // BTrack confidence ×100 to keep lock/PLL fed (0–100)
static int g_micConfMove = 60;         // BTrack confidence ×100 required to MOVE the tempo (0–100)
static int g_micRange    = 6;          // ±BPM window around the tap anchor that audio sensing may move tempo within (1–20)
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
    int micFloor, micConfSil, micConfMove, micRange;
    int ip[4], sn[4], gt[4];
    char wifi_ssid[64], wifi_pass[64];
};
// Bumped whenever RtcSettings' layout changes — a stale layout from the
// pre-OTA firmware must not be restored field-shifted into NVS.
static constexpr uint32_t RTC_MAGIC = 0xCAFEB011;
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
static i2s_chan_handle_t s_i2s_line   = nullptr;  // PCM1808 line-in (slave RX, I2S1)
static volatile double   g_mic_bpm    = 0.0;    // last raw detected BPM (display hint)
static volatile bool     g_mic_locked = false;  // mic has a stable tracked BPM
static volatile bool     g_mic_armed  = false;  // a tap has anchored the tracker
static volatile double   g_mic_tracked = 0.0;   // adaptive anchor / applied BPM
static volatile double   g_mic_tapAnchor = 0.0; // last tap-set tempo (clamp reference)

// Live telemetry for the web page's audio-tuning chart — written by mic_task's
// 20Hz poll, read every 50ms by the WS push task. Beat is latched between
// pushes so a push landing mid-cycle can't miss one.
static volatile float  g_tele_level     = -120.f; // ~800ms-smoothed FFT power, dB (levelEmaDb) — page shows it for the floor slider
static volatile uint8_t g_tele_onset    = 0;  // latched beat since last push (0=none 1=lock-tier 2=move-tier); reset by push task
static volatile float   g_tele_conf     = 0.f;    // BTrack confidence at last poll — chart's confidence trace
static volatile uint32_t g_tele_onsetMs = 0;      // beat hop's time (ms) — page places the tick sub-sample
// Running totals (wrap-safe on the client): beats seen (floor + lock tier),
// and beats that cleared the move tier and could move the tempo.
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
static void do_tap();                    // forward declaration

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
    if (nvs_get_i32(h, "mode", &v) == ESP_OK && v >= 0 && v <= 3)          g_mode     = (int)v;
    if (nvs_get_i32(h, "mfloor",&v)== ESP_OK && v >= -100 && v <= -10)     g_micFloor = (int)v;
    if (nvs_get_i32(h, "mcsil",&v) == ESP_OK && v >= 0 && v <= 100)        g_micConfSil  = (int)v;
    if (nvs_get_i32(h, "mcmove",&v)== ESP_OK && v >= 0 && v <= 100)        g_micConfMove = (int)v;
    if (nvs_get_i32(h, "mrange",&v)== ESP_OK && v >= 1 && v <= 20)         g_micRange = (int)v;
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
    nvs_set_i32(h, "mfloor",(int32_t)g_micFloor);
    nvs_set_i32(h, "mcsil",(int32_t)g_micConfSil);
    nvs_set_i32(h, "mcmove",(int32_t)g_micConfMove);
    nvs_set_i32(h, "mrange",(int32_t)g_micRange);
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
    g_sigIdx = 2; g_brit = 1; g_mode = MODE_MIC;
    g_micFloor = -100; g_micConfSil = 30; g_micConfMove = 60; g_micRange = 6;
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
        case MENU_MODE:  return 3;
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
    { CH_S, CH_r, CH_C, MAX7219Display::SEG_BLANK                  },  // Src (input mode)
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
            // CdJ / Aud / tAP (3-char right-justified); LinE fills pos 4-7
            // like the Lan. values. ("Mic" can't render on 7-seg — no M —
            // so the mic source keeps the Aud glyph.)
            if (val == MODE_CDJ)      { segs[5] = CH_C; segs[6] = CH_d; segs[7] = CH_J; }
            else if (val == MODE_MIC) { segs[5] = CH_A; segs[6] = CH_u; segs[7] = CH_d; }
            else if (val == MODE_LINE){ segs[4] = CH_L; segs[5] = CH_i; segs[6] = CH_n; segs[7] = CH_e; }
            else                      { segs[5] = CH_t; segs[6] = CH_A; segs[7] = CH_P; }
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
                             || (mode_is_audio(g_mode) && g_mic_locked)
                             || (g_mode == MODE_MANUAL && g_manual_locked);
            bool searching    = (mode_is_audio(g_mode) && g_mic_armed && !g_mic_locked);
            bool dot = locked_state || (searching && (now_us() / 250000ULL) % 2 == 0);
            segs[5] = MAX7219Display::digit((int)floor(phase) + 1) | (dot ? MAX7219Display::SEG_DP : 0);
            // Persistent mode bar (top=CDJ, middle=Manual, bottom=Audio) — placed
            // behind the beat counter so the counter's left side stays clean.
            segs[6] = (g_mode == MODE_CDJ)    ? BAR_TOP
                    : (g_mode == MODE_MANUAL) ? BAR_MID
                    : (g_mode == MODE_LINE)   ? (uint8_t)(BAR_TOP | BAR_BOT)
                    :                           BAR_BOT;
            segs[7] = MAX7219Display::digit(peers > 9 ? 9 : peers);
        }
        display.setSegments(segs);
        return;
    }

    if (appMode == MODE_MENU_CONFIRM) {
        segs[0] = CH_r; segs[1] = CH_S; segs[2] = CH_e; segs[3] = CH_t;
        segs[4] = MAX7219Display::SEG_BLANK;
        segs[5] = CH_y; segs[6] = CH_e; segs[7] = CH_S;
        display.setSegments(segs);
        return;
    }

    if (appMode == MODE_OTA_CONFIRM) {
        segs[0] = CH_u; segs[1] = CH_P; segs[2] = CH_d; segs[3] = MAX7219Display::SEG_BLANK;
        segs[4] = MAX7219Display::SEG_BLANK;
        segs[5] = CH_y; segs[6] = CH_e; segs[7] = CH_S;
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
    s_rtc.micFloor = g_micFloor;
    s_rtc.micConfSil = g_micConfSil; s_rtc.micConfMove = g_micConfMove;
    s_rtc.micRange = g_micRange;
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
    nvs_set_i32(h, "mfloor",    s_rtc.micFloor);
    nvs_set_i32(h, "mcsil",     s_rtc.micConfSil);
    nvs_set_i32(h, "mcmove",    s_rtc.micConfMove);
    nvs_set_i32(h, "mrange",    s_rtc.micRange);
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
    // Never let the browser cache this page — it's regenerated firmware UI, and a
    // stale cached copy has already cost a debugging session (JS surviving flashes).
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
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
        "#hdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:4px}"
        "h2{margin:0;color:#fff;font-size:2rem;font-weight:800;letter-spacing:-0.02em}"
        "h2 span,label span{color:#B7F7A5}"
        "#tapbtn{width:60px;height:60px;flex-shrink:0;margin-top:0;padding:0;"
        "border-radius:50%;background:#B7F7A5;color:#0e0e0e;font-size:14px}"
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
        "#mchart{width:100%;height:160px;background:#111;border:1px solid #2a2a2a;"
        "border-radius:6px;display:block}"
        "#mtop{display:flex;align-items:flex-end;gap:22px;margin:10px 0 8px}"
        ".mbig{font-variant-numeric:tabular-nums}"
        ".mbig div{font-size:30px;font-weight:800;color:#B7F7A5;width:4.2ch;"
        "text-align:right;line-height:1}"
        "#mrawv{color:#0af}"
        ".mbig span{display:block;color:#777;font-size:11px;text-transform:uppercase;"
        "letter-spacing:.05em;margin-top:2px;text-align:right}"
        "#mstate{display:inline-block;width:96px;text-align:center;padding:6px 0;"
        "margin-left:auto;border-radius:4px;font-size:12px;font-weight:700;background:#161616}"
        "#mstat{font-size:12px;color:#777;margin:4px 0 2px}"
        ".mnum{font-size:13px;color:#ccc;margin:2px 0;font-variant-numeric:tabular-nums}"
        ".mnum b{color:#B7F7A5;font-weight:700;display:inline-block;min-width:2.4ch;text-align:right}"
        "</style></head><body>"
        "<div id=hdr>"
        "<h2>tap<span>box</span></h2>"
        "<button type=button id=tapbtn onclick=\"doTap()\">Tap</button>"
        "</div>"
        "<div id=tabs>"
        "<button type=button class=\"tb on\" onclick=\"tab(0)\">Network</button>"
        "<button type=button class=tb onclick=\"tab(1)\">Settings</button>"
        "<button type=button class=tb onclick=\"tab(2)\">BPM tuning</button>"
        "<button type=button class=tb onclick=\"tab(3)\">Log</button>"
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
    httpd_resp_sendstr_chunk(req, "<label>Source</label><select name=mode onchange=\"updEnable()\">");
    static const char *modes[] = {"CDJ", "Mic", "Tap", "Line"};
    static const int   modeOrder[] = {0, 1, 3, 2};  // dropdown lists Line before Tap
    for (int k = 0; k < 4; k++) {
        int i = modeOrder[k];
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
        "Audio sync is off &mdash; set Source to Mic or Line on the Settings tab</div>"
        "<div id=mtop>"
        "<div class=mbig><div id=mtapv>&hellip;</div><span>Tap BPM</span></div>"
        "<div class=mbig><div id=mrawv>&hellip;</div><span>Audio BPM</span></div>"
        "<div class=mbig><div id=mtrkv>&hellip;</div><span>Link BPM</span></div>"
        "<div id=mstate>&hellip;</div>"
        "</div>"
        "<canvas id=mchart width=320 height=160></canvas>"
        "<div id=mleg style=\"display:flex;flex-wrap:wrap;gap:4px 12px;font-size:11px;"
        "color:#888;margin:6px 0 2px\">"
        "<span style=color:#e90>&#9644; confidence</span>"
        "<span style=color:#2ecc71>&#9615;&#9679; beat moved tempo</span>"
        "<span style=color:#888>&#9615;&#9679; beat held</span>"
        "<span style=color:#ff5a5a>&#9615;&#9679; beat rejected (floor)</span>"
        "</div>"
        "<div id=mstat>connecting&hellip;</div>"
        "<div class=mnum id=mnum2>&nbsp;</div>"
        "<div class=mnum id=mconf>&nbsp;</div>");

    snprintf(tmp, sizeof(tmp), "<label>Level floor: <span id=mfloorv>%d</span> dB</label>", g_micFloor);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mfloor type=range min=-100 max=-10 value=%d "
        "oninput=\"mfloorv.textContent=this.value;applyField('mfloor',this.value)\">", g_micFloor);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Lock confidence: <span id=mcsilv>%.2f</span></label>", g_micConfSil / 100.0);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mcsil type=range min=0 max=100 value=%d "
        "oninput=\"mcsilv.textContent=(this.value/100).toFixed(2);applyField('mcsil',this.value)\">", g_micConfSil);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>Move confidence: <span id=mcmovev>%.2f</span></label>", g_micConfMove / 100.0);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mcmove type=range min=0 max=100 value=%d "
        "oninput=\"mcmovev.textContent=(this.value/100).toFixed(2);applyField('mcmove',this.value)\">", g_micConfMove);
    httpd_resp_sendstr_chunk(req, tmp);

    snprintf(tmp, sizeof(tmp), "<label>BPM range: &plusmn;<span id=mrangev>%d</span> BPM</label>", g_micRange);
    httpd_resp_sendstr_chunk(req, tmp);
    snprintf(tmp, sizeof(tmp),
        "<input name=mrange type=range min=1 max=20 value=%d "
        "oninput=\"mrangev.textContent=this.value;applyField('mrange',this.value)\">", g_micRange);
    httpd_resp_sendstr_chunk(req, tmp);

    httpd_resp_sendstr_chunk(req,
        "<button type=button id=mrst class=btn-rst onclick=\"mDefaults()\">Reset Audio Defaults</button>"
        "<button class=btn-disp>Save Tuning</button>"
        "</form>"
        "</div>"

        "<div class=tab id=t3>"
        "<h3>Live Log</h3>"
        "<div class=mnum style=\"color:#777\">Tap/arm/lock events, most recent at the bottom.</div>"
        "<pre id=logbox style=\"background:#111;color:#B7F7A5;font-size:12px;line-height:1.4;"
        "height:360px;overflow-y:auto;padding:8px;border-radius:4px;margin:8px 0 0;"
        "white-space:pre-wrap;word-break:break-word\"></pre>"
        "</div>");

    // Firmware version stamp — makes a stale cached page obvious at a glance
    snprintf(tmp, sizeof(tmp),
        "<div style=\"margin-top:18px;font-size:11px;color:#555;text-align:center\">"
        "tapbox firmware v%d.%d.%d</div>", FW_MAJOR, FW_MINOR, FW_PATCH);
    httpd_resp_sendstr_chunk(req, tmp);

    httpd_resp_sendstr_chunk(req,
        "<script>"
        "function doTap(){fetch('/tap',{method:'POST'});}"
        "function tab(i){for(var j=0;j<4;j++){"
          "document.getElementById('t'+j).className=(j==i)?'tab on':'tab';"
          "document.getElementsByClassName('tb')[j].className=(j==i)?'tb on':'tb';"
        "}}"
        // Disable (never hide) fields that don't apply to the current modes
        "function updEnable(){"
          "var st=document.querySelector('[name=net]').value=='1';"
          "['ip','sn','gw'].forEach(function(n){document.querySelector('[name='+n+']').disabled=!st;});"
          "var mv=document.querySelector('[name=mode]').value;"
          "var au=(mv=='1'||mv=='3');"
          "['mfloor','mcsil','mcmove','mrange'].forEach(function(n){document.querySelector('[name='+n+']').disabled=!au;});"
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
          "var d={mfloor:-100,mcsil:30,mcmove:60,mrange:6};"
          "for(var k in d){"
            "document.querySelector('[name='+k+']').value=d[k];"
            "document.getElementById(k+'v').textContent=d[k];"
            "applyField(k,d[k]);"
          "}"
          // Confidence spans display 0.00-1.00, not the raw slider integer
          "mcsilv.textContent='0.30';mcmovev.textContent='0.60';"
        "}"

        // Telemetry CSV (20Hz): levelDb,conf,beat(0/1/2),raw,trk,state,seen,moved,anchor,beatAgeMs
        // History = 120 samples x 50ms = 6s window.
        "var mHist=[],mStats=[],mLastMsg=0,logLines=[];"
        // Cached once — mDraw runs at display rate, no per-frame DOM lookups
        "var mC=document.getElementById('mchart'),mCtx=mC.getContext('2d'),"
        "mW=mC.width,mH=mC.height,"
        "lockEl=document.querySelector('[name=mcsil]'),moveEl=document.querySelector('[name=mcmove]');"
        "function mConnect(){"
          "var ws=new WebSocket('ws://'+location.host+'/ws');"
          "ws.onopen=function(){mstat.textContent='live';};"
          "ws.onclose=function(){mstat.textContent='disconnected \\u2014 retrying\\u2026';setTimeout(mConnect,2000);};"
          "ws.onerror=function(){ws.close();};"
          "ws.onmessage=function(ev){"
            "if(ev.data.charAt(0)==='L'){"
              "logLines.push(ev.data.substring(1));"
              "if(logLines.length>300)logLines.shift();"
              "var lb=document.getElementById('logbox');"
              "if(lb){lb.textContent=logLines.join('\\n');lb.scrollTop=lb.scrollHeight;}"
              "return;"
            "}"
            "var p=ev.data.split(',');"
            // f: beat tick's sub-sample shift left (beat age / 50ms)
            "var r=+p[3],k=+p[4],a=+p[8]||0;"
            "mHist.push({c:+p[1],b:+p[2],f:(+p[9]||0)/50});"
            "if(mHist.length>120)mHist.shift();"
            "mLastMsg=performance.now();"
            "var st=+p[5];"
            "mrawv.textContent=r?r.toFixed(1):'\\u2014';"
            "mtrkv.textContent=k?k.toFixed(1):'\\u2014';"
            "mtapv.textContent=a?a.toFixed(1):'\\u2014';"
            "mstate.textContent=st==2?'LOCKED':st==1?'SEARCHING':'IDLE';"
            "mstate.style.color=st==2?'#B7F7A5':st==1?'#e90':'#777';"
            "mStats.push({ts:Date.now(),d:+p[6],a:+p[7]});"
            "while(mStats.length&&Date.now()-mStats[0].ts>10000)mStats.shift();"
            "if(mStats.length>1){"
              "var f=mStats[0],l=mStats[mStats.length-1];"
              "var dd=(l.d-f.d+100000)%100000,da=(l.a-f.a+100000)%100000;"
              "mnum2.innerHTML='Last 10s: <b>'+dd+'</b> beats &nbsp;\\u00b7&nbsp; <b>'+da+'</b> moved tempo';"
            "}"
            "mconf.innerHTML='Confidence <b>'+(+p[1]).toFixed(2)+'</b> &nbsp;\\u00b7&nbsp; Level <b>'+(+p[0]).toFixed(0)+'</b> dB';"
          "};"
        "}"
        // Confidence strip only (0..1), with the two slider thresholds as
        // live dashed lines and beat ticks. frac (0..1) is progress toward
        // the next 20Hz frame so the scroll glides.
        "function mDraw(frac){"
          "var ctx=mCtx,W=mW,H=mH,CT=10,CH=H-CT;"
          "ctx.clearRect(0,0,W,H);"
          "var n=mHist.length,per=W/119;"
          "function xOf(v){return (v-(n>=120?frac:0))*per;}"
          "function yC(v){if(v<0)v=0;if(v>1)v=1;return CT+CH-v*CH;}"
          // Beat ticks (bright green = moved tempo, dim grey = lock-tier
          // only, red = predicted but REJECTED by the level gate).
          "for(var i=0;i<n;i++){var b=mHist[i].b;if(b){"
            "var xt=xOf(i-mHist[i].f);"
            "var col=b===2?'rgba(46,204,113,0.9)':b===3?'rgba(255,90,90,0.8)':'rgba(136,136,136,0.55)';"
            "ctx.strokeStyle=col;ctx.lineWidth=b===2?2:1;"
            "ctx.beginPath();ctx.moveTo(xt,CT);ctx.lineTo(xt,H);ctx.stroke();"
          "}}"
          // Threshold lines, read live from the sliders
          "var lk=(+lockEl.value||0)/100,mv=(+moveEl.value||0)/100;"
          "ctx.setLineDash([4,3]);ctx.lineWidth=1;"
          "ctx.strokeStyle='#888';ctx.beginPath();ctx.moveTo(0,yC(lk));ctx.lineTo(W,yC(lk));ctx.stroke();"
          "ctx.strokeStyle='#2ecc71';ctx.beginPath();ctx.moveTo(0,yC(mv));ctx.lineTo(W,yC(mv));ctx.stroke();"
          "ctx.setLineDash([]);"
          // Confidence trace
          "ctx.strokeStyle='#e90';ctx.lineWidth=2;ctx.beginPath();"
          "for(i=0;i<n;i++){var x=xOf(i),y=yC(mHist[i].c);"
            "if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}"
          "ctx.stroke();"
        "}"
        "mConnect();"
        "(function mAnim(){"
          "mDraw(mLastMsg?Math.min((performance.now()-mLastMsg)/50,1):0);"
          "requestAnimationFrame(mAnim);"
        "})();"
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
    if (form_field(body, "mode", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 3)          g_mode     = v; }
    if (form_field(body, "brit", tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 3)           g_brit     = v; }
    if (form_field(body, "mfloor",tmp,sizeof(tmp))) { int v = atoi(tmp); if (v >= -100 && v <= -10)       g_micFloor = v; }
    if (form_field(body, "mcsil",tmp, sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 100)          g_micConfSil  = v; }
    if (form_field(body, "mcmove",tmp,sizeof(tmp))) { int v = atoi(tmp); if (v >= 0 && v <= 100)          g_micConfMove = v; }
    if (form_field(body, "mrange",tmp,sizeof(tmp))) { int v = atoi(tmp); if (v >= 1 && v <= 20)           g_micRange = v; }
    free(body);

    nvs_save_settings();
    apply_settings();

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// Same tap the physical button drives — lets the web page act as a remote
// tap button. No redirect (unlike /save, /apply): the page doesn't need to
// re-render anything, so the JS just fires-and-forgets a fetch.
static esp_err_t http_post_tap(httpd_req_t *req) {
    if (!http_check_auth(req)) return ESP_OK;
    do_tap();
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

// Pushes a short human-readable event line to the web page's Log tab, over
// the same /ws channel the 20 Hz telemetry CSV uses. Prefixed 'L' so the
// client JS can tell the two apart (the CSV always starts with a digit).
// For notable one-off events only (taps, lock transitions) — not a firehose;
// the periodic status line stays on serial only.
static void ws_log(const char *fmt, ...) {
    if (!ws_client_connected()) return;
    WsPushArg *a = (WsPushArg *)malloc(sizeof(WsPushArg));
    if (!a) return;
    int off = snprintf(a->text, sizeof(a->text), "L%.1f ", now_ms() / 1000.0);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->text + off, sizeof(a->text) - off, fmt, ap);
    va_end(ap);
    if (httpd_queue_work(s_httpd, ws_send_async, a) != ESP_OK) free(a);
}

// Pushes energy/baseline/threshold/gate/onset at 20 Hz to whichever browser
// has the audio-tuning chart open — cheap to compute, so runs unconditionally
// and just does nothing while no client is connected.
static void ws_push_task(void *) {
    abl_link_session_state ws_sess = abl_link_create_session_state();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (!ws_client_connected()) continue;
        WsPushArg *a = (WsPushArg *)malloc(sizeof(WsPushArg));
        if (!a) continue;
        // levelDb,confidence,beat(0/1/2/3),rawBPM,linkBPM,state,seen,moved,tapBPM,beatAgeMs
        // beat: 0=none 1=lock-tier 2=move-tier 3=predicted-but-REJECTED (floor gate)
        // linkBPM is the real session tempo (any mode), not g_mic_tracked —
        // the page labels it "Link BPM" and Manual/CDJ taps move Link too.
        // tapBPM: audio mode shows the range-gate anchor; other modes show
        // the classic tap tempo once a 4-tap session has set it.
        double linkBpm = 0.0;
        if (linkEnabled) {
            abl_link_capture_app_session_state(s_link, ws_sess);
            linkBpm = abl_link_tempo(ws_sess);
        }
        double tapBpm = mode_is_audio(g_mode) ? (double)g_mic_tapAnchor
                      : (g_manual_locked ? tapTempo.bpm() : 0.0);
        uint32_t age = g_tele_onset ? (now_ms() - g_tele_onsetMs) : 0;
        if (age > 99) age = 99;
        snprintf(a->text, sizeof(a->text), "%.1f,%.2f,%d,%.1f,%.1f,%d,%u,%u,%.1f,%u",
                 (double)g_tele_level, (double)g_tele_conf,
                 (int)g_tele_onset,
                 (double)g_mic_bpm, linkBpm,
                 g_mic_locked ? 2 : (g_mic_armed ? 1 : 0),
                 (unsigned)(g_tele_det % 100000), (unsigned)(g_tele_acc % 100000),
                 tapBpm, (unsigned)age);
        g_tele_onset = 0;  // consume the latched beat (0=none 1=lock-tier 2=move-tier 3=rejected)
        if (httpd_queue_work(s_httpd, ws_send_async, a) != ESP_OK) free(a);
    }
}

static void http_server_start() {
    if (s_httpd) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 5;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) { s_httpd = nullptr; return; }
    httpd_uri_t get_h   = { .uri = "/",      .method = HTTP_GET,  .handler = http_get_root,   .user_ctx = nullptr };
    httpd_uri_t save_h  = { .uri = "/save",  .method = HTTP_POST, .handler = http_post_save,  .user_ctx = nullptr };
    httpd_uri_t apply_h = { .uri = "/apply", .method = HTTP_POST, .handler = http_post_apply, .user_ctx = nullptr };
    httpd_uri_t tap_h   = { .uri = "/tap",   .method = HTTP_POST, .handler = http_post_tap,   .user_ctx = nullptr };
    httpd_uri_t ws_h    = { .uri = "/ws",    .method = HTTP_GET,  .handler = ws_handler,      .user_ctx = nullptr,
                            .is_websocket = true };
    httpd_register_uri_handler(s_httpd, &get_h);
    httpd_register_uri_handler(s_httpd, &save_h);
    httpd_register_uri_handler(s_httpd, &apply_h);
    httpd_register_uri_handler(s_httpd, &tap_h);
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

    if (mode_is_audio(g_mode)) {
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
            ws_log("Audio: tap override %.1f BPM", tapTempo.bpm());
        } else if (g_mic_armed) {
            // Already tracking → a lone tap only re-asserts the downbeat at the
            // current tempo. The mic owns BPM, so do NOT change it.
            if (linkEnabled) reset_downbeat();
            printf("Audio: downbeat re-synced at %.1f BPM\n", (double)g_mic_tracked);
            ws_log("Audio: downbeat re-synced at %.1f BPM", (double)g_mic_tracked);
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
            ws_log("Audio: armed at %.1f BPM (mic %.1f)", seed, (double)g_mic_bpm);
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
        do_tap();  // same tap the physical button and the web Tap button drive
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
    static const char *kModeStr[] = { "CDJ", "Mic", "Tap", "Line" };
    printf("Link: %.2f BPM  sig: %.0f  peers: %llu  eth: %s  mode: %s%s\n",
           abl_link_tempo(s_session), linkQuantum,
           (unsigned long long)abl_link_num_peers(s_link),
           ethConnected ? ethIPStr : "disconnected",
           kModeStr[g_mode],
           (g_mode == MODE_CDJ && g_cdj_active) ? " (cdj active)"
           : (mode_is_audio(g_mode) && g_mic_locked) ? " (audio locked)" : "");
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

// ── PCM1808 line-in, Phase 1: master clock only (see PCM1808_PLAN.md) ─────────
// 8MHz SCKI for the PCM1808 via LEDC: 80MHz APB ÷ (divider 5 × 2¹) — an exact
// integer divider, so zero cycle-to-cycle jitter (a fractional LEDC divider
// alternates two periods, and SCKI jitter degrades the ADC per its datasheet).
// The PCM1808 is strapped Master/256fs (MD0=H, MD1=H) and derives BCK (2MHz)
// and LRCK (fs = 31.25kHz) from this clock itself; until LEDC starts, a static
// SCKI simply holds the chip in its clock-halt power-down state.
static void line_clock_start() {
    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode      = LEDC_HIGH_SPEED_MODE;
    tcfg.duty_resolution = LEDC_TIMER_1_BIT;
    tcfg.timer_num       = LEDC_TIMER_1;
    tcfg.freq_hz         = 8000000;
    tcfg.clk_cfg         = LEDC_USE_APB_CLK;
    if (ledc_timer_config(&tcfg) != ESP_OK) {
        printf("Line-in: LEDC timer config failed\n");
        return;
    }
    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num   = PIN_LINE_SCKI;
    ccfg.speed_mode = LEDC_HIGH_SPEED_MODE;
    ccfg.channel    = LEDC_CHANNEL_0;
    ccfg.timer_sel  = LEDC_TIMER_1;
    ccfg.duty       = 1;  // 50% at 1-bit resolution
    ccfg.hpoint     = 0;
    if (ledc_channel_config(&ccfg) != ESP_OK) {
        printf("Line-in: LEDC channel config failed\n");
        return;
    }
    printf("Line-in: 8MHz SCKI running on IO5\n");
}

// ── PCM1808 line-in, Phase 3: I2S slave RX + level diagnostics ────────────────
// The PCM1808 masters the bus (BCK/LRCK are ITS outputs), so the ESP32's
// second I2S port runs as a slave receiver — slave pins route through the
// GPIO matrix, any GPIO works. Same 24-in-32-bit Philips framing as the
// PCM1808 emits (64 BCK/frame = two 32-bit slots; the 24 data bits sit at
// the top of each slot, which the /2^31 normalization absorbs).
// mic_task is the sole reader: it consumes whichever channel (mic I2S0 /
// line I2S1) the Source setting selects — see the srcLine switch there.
// (Phase 3's separate line_task diagnostic was folded into mic_task when
// Phase 4 routed line-in into the beat pipeline.)
static void init_i2s_line() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
    if (i2s_new_channel(&chan_cfg, nullptr, &s_i2s_line) != ESP_OK) {
        printf("Line-in: I2S channel alloc failed\n");
        s_i2s_line = nullptr;
        return;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(LINE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_LINE_BCK,
            .ws   = PIN_LINE_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_LINE_DIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    if (i2s_channel_init_std_mode(s_i2s_line, &std_cfg) != ESP_OK ||
        i2s_channel_enable(s_i2s_line) != ESP_OK) {
        printf("Line-in: I2S init/enable failed\n");
        s_i2s_line = nullptr;
        return;
    }
    printf("Line-in: I2S slave RX on BCK=IO17 WS=IO32 DIN=IO33\n");
}

// FFT-based spectral-flux onset detector + autocorrelation BPM tracker, with
// tap-anchored, slew-limited BPM application onto Ableton Link. DSP core
// ported (basic tier) from absent42/esphome-audio-reactive (MIT). See
// [[project-inmp441-plan]].
static void mic_task(void *) {
    if (!s_i2s_rx) { vTaskDelete(nullptr); return; }
    abl_link_session_state mic_sess = abl_link_create_session_state();

    static constexpr size_t FFT_SIZE = 1024;
    static constexpr size_t HOP_SIZE = 512;
    // Gates on the real tempo path, both sliders on the tuning page because
    // neither is derivable analytically — they depend on the mic, the room,
    // and how loud the music is:
    //  - g_micFloor (dB): absolute level floor. SuperFlux measures RELATIVE
    //    spectral change, so quiet-but-impulsive sounds (typing!) produce
    //    confident periodic beats out of a silent room. Below the floor a
    //    beat doesn't count for ANY tier — tempo holds, lock decays.
    //  - g_micConfSil / g_micConfMove (×100): two-tier confidence. Lock tier
    //    keeps lock+PLL fed (beat TIMING is useful even when the tempo
    //    estimate is smeared); move tier gates actually CHANGING the tempo
    //    (split after a beat-free passage at conf ~0.3 walked 139 BPM to a
    //    164 alias inside the ±20% clamp).

    static int32_t samples[256];
    static float norm[128];
    static float fftBuf[FFT_SIZE];
    static float magsSq[FFT_SIZE / 2];
    static float melFrame[32];
    static audio_dsp::RingBuffer<float, 2048> ring;
    static audio_dsp::FFTProcessor<FFT_SIZE> fft(MIC_SAMPLE_RATE);

    // Pro-tier tempo pipeline — runs every FFT hop (62.5Hz), NOT decoupled to
    // the slower 20Hz telemetry poll below: BTrack::process() is a stateful
    // per-frame machine (internal countdown timers) that must be fed every
    // hop to behave correctly, unlike the old BeatTracker it replaces.
    static audio_dsp::MelFilterbank<32, FFT_SIZE> melFb;
    static audio_dsp::SuperFluxOnset<32> superflux;
    static audio_dsp::BTrack btrack;
    static audio_dsp::BTrack::Result latestBTrackResult{};
    melFb.setup(MIC_SAMPLE_RATE, 80.0f, 16000.0f);

    uint32_t lastOnset = 0, lastDbg = 0, lastPollMs = 0;
    int      consec   = 0;
    bool     wasAudio  = mode_is_audio(g_mode);
    // Latches true if BTrack predicted a beat on ANY hop since the last 20Hz
    // poll below — the fast hop loop (62.5Hz) overwrites latestBTrackResult
    // every iteration, so reading its beat_event directly at poll time would
    // miss most beats. beatLevelDb/beatMs capture the beat hop's own frame
    // level and time, so the floor gate judges the beat's actual energy,
    // not whatever the slower 20Hz poll happens to see.
    bool     btBeatSincePoll = false;
    float    beatLevelDb     = -120.f;
    uint32_t beatMs          = 0;
    float    frameLevelDb    = -120.f;  // most recent hop's raw (unsmoothed) mean FFT power, dB — feeds levelEmaDb below and the serial debug line only
    // ~800ms EMA (0.98/0.02 blend at the 62.5Hz hop rate) of frameLevelDb —
    // a raw single 32ms-hop reading swings ~20dB on room noise alone
    // (measured 2026-07-09), so this is what the level floor gate actually
    // checks (beatLevelDb, below) and what the web page displays (g_tele_level).
    float    levelEmaDb      = -120.f;

    // Which capture source feeds the pipeline. Sampled per loop iteration so a
    // Settings change takes effect without a reboot; on a switch the whole
    // chain resets (different sample rate ⇒ different bin/mel geometry, and
    // BTrack state from one clock domain is meaningless in the other).
    // Starts false to match the mic-rate melFb.setup() above — if the device
    // boots with Line persisted, the first loop iteration takes the switch
    // path and reconfigures everything for line-in.
    bool srcLine = false;
    // BTrack/tempo_estimator hard-code kFrameHz = 62.5 (32kHz/512 hop). The
    // line-in hop rate is 31250/512 = 61.035Hz, so reported BPM must be
    // scaled by 61.035/62.5 to be true BPM. Confidence is unitless — no scale.
    float bpmScale = 1.0f;
    uint32_t lastStall = 0;

    while (true) {
        bool wantLine = (g_mode == MODE_LINE) && s_i2s_line;
        if (wantLine != srcLine) {
            srcLine  = wantLine;
            bpmScale = srcLine ? (31250.0f / 512.0f) / 62.5f : 1.0f;
            float rate = srcLine ? 31250.0f : 32000.0f;
            // Mel top band must clear the source's Nyquist (15.625kHz on line-in)
            melFb.setup(rate, 80.0f, srcLine ? 15000.0f : 16000.0f);
            superflux.reset(); btrack.reset();
            ring.advance(ring.available());  // drop other-rate samples
            g_mic_armed = false; g_mic_locked = false; consec = 0;
            btBeatSincePoll = false;
            latestBTrackResult = audio_dsp::BTrack::Result{};
            printf("Audio source: %s (fs=%.0f)\n", srcLine ? "line-in" : "mic", (double)rate);
            ws_log("Audio source: %s", srcLine ? "line-in" : "mic");
        }

        size_t br = 0;
        if (i2s_channel_read(srcLine ? s_i2s_line : s_i2s_rx,
                             samples, sizeof(samples), &br, pdMS_TO_TICKS(100)) != ESP_OK) {
            if (srcLine && (now_ms() - lastStall) >= 2000) {
                lastStall = now_ms();
                printf("line: no frames — check BCK/LRC wiring and SCKI\n");
            }
            continue;
        }
        int n = (int)(br / sizeof(int32_t));
        if (n <= 0) continue;

        int cnt = 0;
        if (srcLine) {
            // PCM1808 is stereo — average L+R into the mono pipeline.
            for (int i = 0; i + 1 < n; i += 2) {
                norm[cnt++] = (float)(((double)samples[i] + (double)samples[i + 1]) / 2.0
                                      / 2147483648.0);
            }
        } else {
            // Left-slot extraction/normalization — same stride-by-2 workaround as
            // before (INMP441 L/R tied to GND; mono I2S mode returned all-zero
            // samples in bring-up, see BEAT_DETECTION.md §3).
            for (int i = 0; i < n; i += 2) {
                norm[cnt++] = (float)((double)samples[i] / 2147483648.0);
            }
        }
        ring.write(norm, cnt);

        // Drain full FFT windows as they become available (accumulates across
        // multiple I2S reads — one read is far smaller than FFT_SIZE). Every
        // hop also drives the pro-tier Mel->SuperFlux->BTrack chain — this is
        // the FAST path, at the FFT hop rate (62.5Hz), not the 20Hz poll below.
        while (ring.available() >= FFT_SIZE) {
            ring.peek(fftBuf, FFT_SIZE);
            fft.process(fftBuf);

            const float *mags = fft.magnitudes();
            float power = 0.f;
            for (size_t i = 0; i < FFT_SIZE / 2; i++) {
                magsSq[i] = mags[i] * mags[i];
                power += magsSq[i];
            }
            power /= (float)(FFT_SIZE / 2);
            frameLevelDb = (power > 1e-12f) ? 10.0f * log10f(power) : -120.f;
            levelEmaDb = 0.98f * levelEmaDb + 0.02f * frameLevelDb;  // diagnostic, see above

            melFb.process(magsSq, melFrame);

            auto sf = superflux.process(melFrame);
            latestBTrackResult = btrack.process(sf.strength);

            if (latestBTrackResult.beat_event) {
                btBeatSincePoll = true;
                beatLevelDb = levelEmaDb;   // smoothed level at the beat hop, not the raw spike
                beatMs = now_ms();
            }

            ring.advance(HOP_SIZE);
        }

        uint32_t now = now_ms();
        bool isAudio = mode_is_audio(g_mode);
        if (!isAudio) {
            g_mic_armed = false; g_mic_locked = false; consec = 0; btBeatSincePoll = false;
            if (wasAudio) { superflux.reset(); btrack.reset(); }
        }
        wasAudio = isAudio;

        // Outer poll runs at 20 Hz — matches ws_push_task's telemetry cadence.
        if ((now - lastPollMs) < 50) continue;
        lastPollMs = now;

        // BTrack (fast path above) is the actual tempo source — read its
        // latest per-hop result, gated on the tunable lock-tier confidence.
        audio_dsp::BTrack::Result btResult = latestBTrackResult;
        btResult.bpm *= bpmScale;  // frame-rate correction for line-in (see above)
        g_tele_conf  = btResult.confidence;
        g_tele_level = levelEmaDb;  // smoothed (~800ms) — see levelEmaDb declaration
        bool btConfident = btResult.confidence > g_micConfSil * 0.01f;

        if (btConfident && btResult.bpm >= 50.0f && btResult.bpm <= 220.0f) {
            g_mic_bpm = btResult.bpm;  // raw hint for display
        }

        // Absolute level floor: a beat whose hop sat below g_micFloor does
        // not count for ANY tier — SuperFlux is level-relative, so without
        // this a quiet room's keyboard clatter makes confident "beats".
        bool btPredictedThisPoll = btBeatSincePoll;  // BTrack predicted a beat, before gating
        bool btBeatThisPoll = btBeatSincePoll && (beatLevelDb >= (float)g_micFloor);
        btBeatSincePoll = false;  // consumed this poll, regardless of outcome below

        // FOLLOW/HOLD: BTrack's confidence gates the tempo outright — no
        // acceptance window, no slew. FOLLOW (high-confidence beat): snap
        // the tracked tempo TO the candidate so it converges as fast as
        // BTrack does; the deadband only swallows sub-0.4-BPM jitter so the
        // published Link tempo doesn't flicker across the network (and since
        // every real move lands exactly ON the candidate, there is no
        // standing offset for the PLL to absorb). HOLD (anything less):
        // the tempo freezes at its last good value through silence,
        // breakdowns, and low-confidence passages — beats above the silence
        // floor still feed the lock and PLL below, they just can't MOVE it.
        if (isAudio && g_mic_armed && btBeatThisPoll && btConfident && btResult.bpm > 0.0f) {
            // Fold octave errors toward the STABLE tapped anchor, not the
            // wandering output — the anchor is ground truth for which octave
            // the operator meant (the ~85-160 BPM aliasing limit in
            // tempo_estimator.h is why this stays).
            double anchor = (g_mic_tapAnchor > 0.0) ? g_mic_tapAnchor : g_mic_tracked;
            double cand = btResult.bpm;
            while (cand > anchor * 1.4) cand *= 0.5;
            while (cand < anchor * 0.7) cand *= 2.0;

            // Counters: det = beats seen (cleared floor + lock tier),
            // acc = beats that cleared the move tier and could move tempo.
            g_tele_det = g_tele_det + 1;
            g_tele_onset = 1;         // lock-tier beat (chart: dim tick)
            g_tele_onsetMs = beatMs;  // beat hop's real time, for tick placement

            // BPM range gate: the anchor itself NEVER moves except by re-tap
            // (do_tap()) — this only decides whether a candidate is trusted
            // enough to move the TRACKED tempo around it. A candidate more
            // than g_micRange BPM from the anchor is treated the same as a
            // beat that failed the confidence floor: it still counts for
            // lock-tier (timing/PLL stays fed, above), it just can't move
            // the tempo. Re-tap to re-center the window elsewhere.
            bool inRange = fabs(cand - anchor) <= (double)g_micRange;
            if (btResult.confidence > g_micConfMove * 0.01f && inRange) {
                g_tele_acc = g_tele_acc + 1;
                g_tele_onset = 2;     // move-tier beat (chart: bright tick)
                if (fabs(cand - g_mic_tracked) > 0.4) g_mic_tracked = cand;
            }
            lastOnset = now;  // last CONFIDENT beat — drives the lock-timeout below
            if (++consec >= 4) {
                if (!g_mic_locked) ws_log("Locked at %.1f BPM", g_mic_tracked);
                g_mic_locked = true;
            }

            if (linkEnabled) {
                uint64_t t = now_us();
                abl_link_capture_app_session_state(s_link, mic_sess);
                abl_link_set_tempo(mic_sess, g_mic_tracked, t);
                // Phase-lock (only once locked): gently pull the grid so
                // the detected beat lands on the NEAREST beat. Nearest-beat
                // correction preserves the tapped bar/downbeat.
                if (g_mic_locked) {
                    double beat    = abl_link_beat_at_time(mic_sess, t, linkQuantum);
                    double nearest = floor(beat + 0.5);
                    double fixed   = beat + 0.15 * (nearest - beat);  // low gain
                    abl_link_force_beat_at_time(mic_sess, fixed, t, linkQuantum);
                }
                abl_link_commit_app_session_state(s_link, mic_sess);
            }
        } else if (isAudio && g_mic_armed && btPredictedThisPoll && !btBeatThisPoll) {
            // BTrack predicted a beat but the level floor rejected it —
            // surfaced on the chart (onset=3).
            g_tele_onset = 3;
            g_tele_onsetMs = beatMs;
        }

        // Drop the lock if no CONFIDENT beat (lastOnset, set only above) has
        // reinforced it recently. Hold = 6 beat-periods
        // (min 3.5s) so slow tempos get the same tolerance in musical time.
        uint32_t lockHold = (g_mic_tracked > 60.0)
                          ? (uint32_t)(6.0 * 60000.0 / g_mic_tracked) : 6000;
        if (lockHold < 3500) lockHold = 3500;
        if (g_mic_locked && (now - lastOnset) > lockHold) {
            ws_log("Lock lost (was %.1f BPM)", g_mic_tracked);
            g_mic_locked = false; consec = 0;
        }

        // Tuning telemetry while in Audio mode
        if (isAudio && (now - lastDbg) >= 500) {
            lastDbg = now;
            printf("%s lvl=%.1fdB ema=%.1fdB floor=%ddB raw=%.1f trk=%.1f conf=%.2f armed=%d lock=%d\n",
                   srcLine ? "line" : "mic",
                   (double)frameLevelDb, (double)levelEmaDb, g_micFloor,
                   (double)g_mic_bpm, (double)g_mic_tracked, (double)btResult.confidence,
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
    line_clock_start();  // PCM1808 SCKI — Phase 1, see PCM1808_PLAN.md
    init_i2s_line();     // PCM1808 slave RX — Phase 3
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

        // Static IP only needs link-up (~2 s). DHCP needs address assignment:
        // a lease normally lands within ~5 s, but with no DHCP server on the
        // wire (direct cable to a Denon player or laptop) lwIP auto-IP
        // self-assigns a 169.254.x.x address, which takes ~10 s. Wait that
        // long only while a cable is present — no link after 3 s means no
        // cable, so fall through to WiFi quickly.
        printf("Waiting for Ethernet");
        uint32_t start = now_ms();
        while (!ethConnected) {
            uint32_t limit = (g_net == 0 && ethLinkUp) ? 15000 : 3000;
            if (now_ms() - start >= limit) break;
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
    xTaskCreate(mic_task, "mic", 8192, nullptr, 5, nullptr);
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
        eth_check_autoip_grace();
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
