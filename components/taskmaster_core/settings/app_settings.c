/*
 * app_settings.c — the Settings core app (device hub). See app_settings.h.
 *
 * Two modes:
 *   MENU   → a schema-driven settings editor (settings_menu.h) over a declared table
 *            of rows; a modal confirm dialog (confirm.h) guards destructive actions.
 *   PORTAL → the SoftAP provisioning portal is up; the screen shows the join
 *            instructions (phone-driven). Folded in from the old Setup app.
 *
 * The Wi-Fi toggle persists WIFI_EN (nvs_config) and drives wifi_mgr, so the rest of
 * the system — and the task apps — just read net_status (this is what takes an app
 * offline, §8.3 / step 12). Home is OS-reserved: it exits to the Launcher, tearing the
 * portal down cleanly (§6A) via exit(). New settings are added as table rows (§8A.1).
 */
#include "app_settings.h"

#include "softap_portal.h"
#include "wifi_mgr.h"
#include "net_status.h"
#include "nvs_config.h"
#include "app_manager.h"
#include "app_config.h"
#include "app_store.h"
#include "sh1106.h"
#include "ui_frame.h"
#include "ui_list.h"
#include "ui.h"
#include "settings_menu.h"
#include "confirm.h"
#include "async_job.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "app.settings";

/* Menu layout. */
#define SETTINGS_TITLE_ROW  0
#define SETTINGS_LIST_ROW   1                    /* list starts below the title */
#define SETTINGS_LIST_ROWS  (UI_ROWS - 1)        /* rows below the title */

/* Portal (instructional) layout — mirrors the old Setup screen. */
#define PORTAL_TITLE_ROW    0
#define PORTAL_STEP1_ROW    1
#define PORTAL_SSID_ROW     2
#define PORTAL_STEP2_ROW    3
#define PORTAL_URL_ROW      4

/* Device-info sub-screen. */
#define INFO_MAX_LINES      10
#define INFO_LINE_MAX       40   /* fits "SSID: " / "FW: " + a 32-char value; long lines scroll */
#define INFO_SSID_MAX       33

/* Delete-data sub-screen. */
#define DEL_MAX_TARGETS     16   /* "Wi-Fi" + each app config group */
#define DEL_PROMPT_MAX      32

/* OTA update (step 7). */
#define OTA_URL_MAX         97   /* fw_url schema max_len (96) + NUL */
#define OTA_HTTP_TMO_MS     20000

typedef enum { MODE_MENU, MODE_PORTAL, MODE_INFO, MODE_DELETE, MODE_OTA } settings_mode_t;
typedef enum { OTA_RUNNING, OTA_FAILED, OTA_NOURL } ota_state_t;

/* SEL when navigating; OK when editing a value or in a confirm dialog; BAK on the
 * read-only info screen. */
static const control_hints_t SETTINGS_HINTS      = { .rotate = "<>", .click = "SEL", .select = "SEL" };
static const control_hints_t SETTINGS_HINTS_EDIT = { .rotate = "<>", .click = "OK",  .select = "OK" };
static const control_hints_t SETTINGS_HINTS_INFO = { .rotate = "<>", .click = "BAK", .select = "BAK" };

static settings_mode_t s_mode;
static bool            s_portal_up;
static bool            s_enter_setup;   /* boot asked us to open in portal mode */
static settings_menu_t s_menu;

/* Device-info snapshot (built on entry, scrolled via ui_list). */
static char       s_info[INFO_MAX_LINES][INFO_LINE_MAX];
static int        s_info_n;
static ui_list_t  s_info_list;

/* Delete-data targets: "Wi-Fi" (ns == NULL) + one per app config group (ns set). */
typedef struct { const char *label; const char *ns; } del_target_t;
static del_target_t s_del[DEL_MAX_TARGETS];
static int          s_del_n;
static int          s_del_sel;                 /* target awaiting confirm */
static ui_list_t    s_del_list;
static char         s_del_prompt[DEL_PROMPT_MAX];

static ota_state_t  s_ota_state;               /* current MODE_OTA screen state */

/* ── the provisioning portal sub-mode ── */
static void portal_start(void)
{
    if (!s_portal_up && softap_portal_start() == ESP_OK) {
        s_portal_up = true;
    }
    net_status_set(NET_PORTAL, 0);
    s_mode = MODE_PORTAL;
    ESP_LOGI(TAG, "portal up — join '%s', open http://%s", SOFTAP_SSID, SOFTAP_IP);
}

static void portal_stop(void)
{
    if (s_portal_up) {
        softap_portal_stop();          /* drops the AP; any prior STA link is kept */
        s_portal_up = false;
    }
    wifi_mgr_refresh_status();          /* restore the connectivity indicator */
}

/* ── settings-table accessors / actions ── */
static int wifi_get(void *ctx)
{
    (void)ctx;
    uint8_t en = 1;                     /* default on if never set */
    config_get_u8("wifi_en", &en);
    return en != 0 ? 1 : 0;
}

/* Flip the Wi-Fi master switch: persist WIFI_EN + drive the radio. This is what takes
 * the device (and the task apps) offline/online (§8.3, step 12). */
static void wifi_set(void *ctx, int on)
{
    (void)ctx;
    config_set_u8("wifi_en", on ? 1 : 0);
    if (on) {
        wifi_mgr_start_sta();           /* CONNECTING → CONNECTED */
    } else {
        wifi_mgr_stop();                /* radio down → NET_WIFI_OFF */
    }
    ESP_LOGI(TAG, "Wi-Fi %s", on ? "on" : "off");
}

static void act_wifi_setup(void *ctx)    { (void)ctx; portal_start(); }
static void act_restart(void *ctx)       { (void)ctx; esp_restart(); }
static void act_factory_reset(void *ctx) { (void)ctx; config_factory_reset(); esp_restart(); }

/* ── device / network info sub-screen ── */
static void info_row_text(int i, char *buf, int buf_sz, void *ctx)
{
    (void)ctx;
    snprintf(buf, buf_sz, "%s", (i >= 0 && i < s_info_n) ? s_info[i] : "");
}

/* Snapshot the read-only device/network info into scrollable lines. */
static void gather_info(void)
{
    s_info_n = 0;
    char (*L)[INFO_LINE_MAX] = s_info;

    char ssid[INFO_SSID_MAX] = {0};
    config_get_str("wifi_ssid", ssid, sizeof(ssid));
    snprintf(L[s_info_n++], INFO_LINE_MAX, "SSID: %s", ssid[0] ? ssid : "-");

    net_status_t ns;
    net_status_get(&ns);
    if (ns.online) {
        snprintf(L[s_info_n++], INFO_LINE_MAX, "RSSI: %d dBm", ns.rssi);
    } else {
        snprintf(L[s_info_n++], INFO_LINE_MAX, "Net: %s", net_state_str(ns.state));
    }

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {0};
    if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr) {
        snprintf(L[s_info_n++], INFO_LINE_MAX, "IP: " IPSTR, IP2STR(&ip.ip));
    } else {
        snprintf(L[s_info_n++], INFO_LINE_MAX, "IP: -");
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(L[s_info_n++], INFO_LINE_MAX, "MAC:%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const esp_app_desc_t *d = esp_app_get_description();
    snprintf(L[s_info_n++], INFO_LINE_MAX, "FW: %s", d->version);
    snprintf(L[s_info_n++], INFO_LINE_MAX, "Built: %s", d->date);

    snprintf(L[s_info_n++], INFO_LINE_MAX, "Heap: %u KB",
             (unsigned)(esp_get_free_heap_size() / 1024));

    uint32_t up_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    snprintf(L[s_info_n++], INFO_LINE_MAX, "Up: %um %us", (unsigned)(up_s / 60),
             (unsigned)(up_s % 60));
}

static void act_device_info(void *ctx)
{
    (void)ctx;
    gather_info();
    ui_list_init(&s_info_list, UI_ROWS);
    ui_list_set_count(&s_info_list, s_info_n);
    s_mode = MODE_INFO;
}

/* ── startup target (ENUM): "Launcher" or a specific app, honored at boot ── */
#define STARTUP_TGT_MAX      16              /* matches the nvs_config startup_tgt size */
#define SETTINGS_MAX_APPS    16              /* matches app_manager's registry cap */
#define STARTUP_MAX_CHOICES  (1 + SETTINGS_MAX_APPS)  /* "Launcher" + each registered app */

static const char *s_startup_choices[STARTUP_MAX_CHOICES];
static int         s_startup_count;          /* built in init() from the app registry */

/* Build "Launcher" + the registered app names (choice 0 = Launcher = default). */
static void startup_build_choices(void)
{
    s_startup_choices[0] = "Launcher";
    s_startup_count = 1;
    unsigned n = app_manager_count();
    for (unsigned i = 0; i < n && s_startup_count < STARTUP_MAX_CHOICES; i++) {
        const device_app_t *a = app_manager_get(i);
        if (a && a->name) {
            s_startup_choices[s_startup_count++] = a->name;
        }
    }
}

static int startup_get(void *ctx)
{
    (void)ctx;
    char t[STARTUP_TGT_MAX] = {0};
    config_get_str("startup_tgt", t, sizeof(t));
    if (t[0] == '\0') {
        return 0;                            /* unset → Launcher */
    }
    for (int i = 1; i < s_startup_count; i++) {
        if (strcmp(t, s_startup_choices[i]) == 0) {
            return i;
        }
    }
    return 0;                                /* stale target → Launcher */
}

static void startup_set(void *ctx, int idx)
{
    (void)ctx;
    config_set_str("startup_tgt",
                   (idx > 0 && idx < s_startup_count) ? s_startup_choices[idx] : "");
}

/* ── OLED brightness (RANGE, stored as a 0..100% level → SH1106 contrast) ── */
#define BRIGHT_MIN   10
#define BRIGHT_MAX   100
#define BRIGHT_STEP  10
#define BRIGHT_FULL  255   /* contrast register range 0..255 */

static void bright_apply(int pct)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    sh1106_set_contrast((uint8_t)(pct * BRIGHT_FULL / 100));
}

static int bright_get(void *ctx)
{
    (void)ctx;
    uint8_t p = BRIGHT_MAX;
    config_get_u8("bright", &p);
    return p;
}

static void bright_set(void *ctx, int pct)
{
    (void)ctx;
    config_set_u8("bright", (uint8_t)pct);
    bright_apply(pct);                  /* live */
}

/* Apply the saved brightness at boot (called from main after the panel is up). */
void app_settings_apply_brightness(void)
{
    bright_apply(bright_get(NULL));
}

/* ── inactivity timeout (ENUM over idle_to_s seconds; 0 = off, honored by ui.c) ── */
static const char *const TIMEOUT_LABELS[] = { "Off", "30s", "1m", "5m", "15m" };
static const uint16_t    TIMEOUT_SECS[]   = { 0, 30, 60, 300, 900 };
#define TIMEOUT_COUNT ((int)(sizeof(TIMEOUT_SECS) / sizeof(TIMEOUT_SECS[0])))

static int timeout_get(void *ctx)
{
    (void)ctx;
    uint16_t s = 0;
    config_get_u16("idle_to_s", &s);
    for (int i = 0; i < TIMEOUT_COUNT; i++) {
        if (TIMEOUT_SECS[i] == s) {
            return i;
        }
    }
    return 0;                            /* unknown value → Off */
}

static void timeout_set(void *ctx, int idx)
{
    (void)ctx;
    if (idx < 0 || idx >= TIMEOUT_COUNT) {
        idx = 0;
    }
    config_set_u16("idle_to_s", TIMEOUT_SECS[idx]);
}

/* ── deep sleep (TOGGLE): when on, idle → light sleep (Stage 2, ui.c) ── */
static int deep_sleep_get(void *ctx)
{
    (void)ctx;
    uint8_t v = 0;
    config_get_u8("deep_sleep", &v);
    return v != 0 ? 1 : 0;
}

static void deep_sleep_set(void *ctx, int on)
{
    (void)ctx;
    config_set_u8("deep_sleep", on ? 1 : 0);
}

/* ── web config (TOGGLE): serve the config form on the station IP (§7A) ── */
static int webcfg_get(void *ctx)
{
    (void)ctx;
    return config_web_active() ? 1 : 0;
}

static void webcfg_set(void *ctx, int on)
{
    (void)ctx;
    if (on) config_web_start();   /* browse the IP shown in Device info */
    else    config_web_stop();
}

/* ── per-app ACFG_KNOB rows (§9.4): each carries its {ns, field}; values live in the
 * app's own app_store namespace, so core edits app config without naming any app. ── */
typedef struct { const char *ns; const app_cfg_field_t *f; } app_knob_ctx_t;

static int app_knob_get(void *ctx)
{
    app_knob_ctx_t *c = (app_knob_ctx_t *)ctx;
    app_store_t st;
    uint32_t v = (uint32_t)c->f->min;   /* default to the field's min */
    if (app_store_open(&st, c->ns) == ESP_OK) {
        app_store_get_u32(&st, c->f->key, &v, (uint32_t)c->f->min);
        app_store_close(&st);
    }
    return (int)v;
}

static void app_knob_set(void *ctx, int val)
{
    app_knob_ctx_t *c = (app_knob_ctx_t *)ctx;
    app_store_t st;
    if (app_store_open(&st, c->ns) == ESP_OK) {
        app_store_set_u32(&st, c->f->key, (uint32_t)val);
        app_store_close(&st);
    }
}

/* ── delete-data sub-screen: wipe one app's stored config, or Wi-Fi credentials ── */
static void del_row_text(int i, char *buf, int buf_sz, void *ctx)
{
    (void)ctx;
    snprintf(buf, buf_sz, "%s", (i >= 0 && i < s_del_n) ? s_del[i].label : "");
}

/* "Wi-Fi" + one entry per app config group (§9.4 — core names no app). */
static void del_build_targets(void)
{
    s_del_n = 0;
    s_del[s_del_n++] = (del_target_t){ .label = "Wi-Fi", .ns = NULL };
    for (unsigned g = 0; g < app_config_group_count() && s_del_n < DEL_MAX_TARGETS; g++) {
        const app_cfg_group_t *grp = app_config_group(g);
        s_del[s_del_n++] = (del_target_t){ .label = grp->name, .ns = grp->ns };
    }
}

static void del_confirm_cb(bool yes, void *ctx)
{
    (void)ctx;
    if (!yes || s_del_sel < 0 || s_del_sel >= s_del_n) {
        return;
    }
    const del_target_t *t = &s_del[s_del_sel];
    if (t->ns == NULL) {
        /* Wi-Fi: clear creds + un-provision + drop the link → next boot = Wi-Fi setup. */
        config_set_str("wifi_ssid", "");
        config_set_str("wifi_psk", "");
        config_set_provisioned(false);
        wifi_mgr_stop();
        ESP_LOGW(TAG, "deleted Wi-Fi credentials");
    } else {
        /* App: erase its whole app_store namespace (tokens/URLs/knobs). */
        app_store_t st;
        if (app_store_open(&st, t->ns) == ESP_OK) {
            app_store_erase_all(&st);
            app_store_close(&st);
        }
        ESP_LOGW(TAG, "deleted '%s' data", t->label);
    }
}

static void act_delete_creds(void *ctx)
{
    (void)ctx;
    del_build_targets();
    ui_list_init(&s_del_list, UI_ROWS - 1);    /* below the title */
    ui_list_set_count(&s_del_list, s_del_n);
    s_mode = MODE_DELETE;
}

/* ── OTA update: pull a signed image from fw_url (esp_https_ota), reboot, rollback ── */
typedef struct { char url[OTA_URL_MAX]; } ota_ctx_t;

static bool ota_work(async_job_t *job, void *ctx)
{
    (void)job;
    ota_ctx_t *o = (ota_ctx_t *)ctx;
    esp_http_client_config_t http = {
        .url               = o->url,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* HTTPS via the cert bundle */
        .timeout_ms        = OTA_HTTP_TMO_MS,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };
    esp_err_t err = esp_https_ota(&cfg);              /* download → spare slot → set boot */
    ESP_LOGW(TAG, "OTA from %s: %s", o->url, esp_err_to_name(err));
    return err == ESP_OK;
}

static void ota_done(void *ctx, bool ok)
{
    (void)ctx;
    if (ok) {
        esp_restart();                  /* boot the new (pending-verify) image (§9) */
    }
    ui_inhibit_sleep(false);
    s_ota_state = OTA_FAILED;           /* stay put; user can retry */
}

static void act_ota(void *ctx)
{
    (void)ctx;
    char url[OTA_URL_MAX] = {0};
    config_get_str("fw_url", url, sizeof(url));
    if (url[0] == '\0') {
        s_ota_state = OTA_NOURL;
        s_mode = MODE_OTA;
        return;
    }
    ota_ctx_t o = {0};
    strlcpy(o.url, url, sizeof(o.url));
    ui_inhibit_sleep(true);             /* don't blank / light-sleep mid-flash */
    if (async_job_submit(ota_work, ota_done, &o, sizeof(o))) {
        s_ota_state = OTA_RUNNING;
    } else {
        ui_inhibit_sleep(false);
        s_ota_state = OTA_FAILED;
    }
    s_mode = MODE_OTA;
}

/* The declared settings table — add a setting here, not a new screen (§8A.1 step 0).
 * Indexed so a row's runtime bits (e.g. dynamic ENUM choices) can be filled in init. */
enum {
    SI_WIFI, SI_SETUP, SI_WEBCFG, SI_DEVICE_INFO, SI_STARTUP, SI_BRIGHT, SI_TIMEOUT,
    SI_DEEPSLEEP, SI_UPDATE, SI_DELETE, SI_RESTART, SI_FACTORY, SI_COUNT
};
static setting_item_t SETTINGS_ITEMS[SI_COUNT] = {
    [SI_WIFI]        = { .label = "Wi-Fi",       .kind = SETTING_TOGGLE,
                         .get = wifi_get, .set = wifi_set },
    [SI_SETUP]       = { .label = "Setup",       .kind = SETTING_ACTION,
                         .action = act_wifi_setup },
    [SI_WEBCFG]      = { .label = "Web config",  .kind = SETTING_TOGGLE,
                         .get = webcfg_get, .set = webcfg_set },
    [SI_DEVICE_INFO] = { .label = "Device info", .kind = SETTING_ACTION,
                         .action = act_device_info },
    [SI_STARTUP]     = { .label = "Startup",     .kind = SETTING_ENUM,
                         .get = startup_get, .set = startup_set },  /* choices set in init */
    [SI_BRIGHT]      = { .label = "Brightness",  .kind = SETTING_RANGE,
                         .get = bright_get, .set = bright_set,
                         .min = BRIGHT_MIN, .max = BRIGHT_MAX, .step = BRIGHT_STEP, .unit = "%" },
    [SI_TIMEOUT]     = { .label = "Timeout",     .kind = SETTING_ENUM,
                         .get = timeout_get, .set = timeout_set,
                         .choices = TIMEOUT_LABELS, .choice_count = TIMEOUT_COUNT },
    [SI_DEEPSLEEP]   = { .label = "Deep sleep",  .kind = SETTING_TOGGLE,
                         .get = deep_sleep_get, .set = deep_sleep_set },
    [SI_UPDATE]      = { .label = "Check update", .kind = SETTING_ACTION,
                         .action = act_ota, .confirm = "Update firmware?" },
    [SI_DELETE]      = { .label = "Delete data", .kind = SETTING_ACTION,
                         .action = act_delete_creds },
    [SI_RESTART]     = { .label = "Restart",     .kind = SETTING_ACTION,
                         .action = act_restart,  .confirm = "Restart device?" },
    [SI_FACTORY]     = { .label = "Factory reset", .kind = SETTING_ACTION,
                         .action = act_factory_reset, .confirm = "Erase all settings?" },
};
/* Live rows = the core template above + one appended row per app ACFG_KNOB field. */
#define MAX_APP_KNOBS     16
#define SETTINGS_MAX_ROWS (SI_COUNT + MAX_APP_KNOBS)

static setting_item_t s_rows[SETTINGS_MAX_ROWS];
static int            s_row_count;
static app_knob_ctx_t s_knob_ctx[MAX_APP_KNOBS];

/* Build the live rows: copy the core template, wire the dynamic startup choices, then
 * append each installed app's ACFG_KNOB fields as editable rows (§9.4 — core names no
 * app). Paste config (URLs/tokens) is set via "Setup" and wiped via "Delete data". */
static void settings_build_rows(void)
{
    memcpy(s_rows, SETTINGS_ITEMS, sizeof(SETTINGS_ITEMS));
    s_row_count = SI_COUNT;
    s_rows[SI_STARTUP].choices      = s_startup_choices;
    s_rows[SI_STARTUP].choice_count = s_startup_count;

    int kc = 0;
    for (unsigned g = 0; g < app_config_group_count() && s_row_count < SETTINGS_MAX_ROWS; g++) {
        const app_cfg_group_t *grp = app_config_group(g);
        for (unsigned k = 0; k < grp->count && s_row_count < SETTINGS_MAX_ROWS && kc < MAX_APP_KNOBS; k++) {
            const app_cfg_field_t *f = &grp->fields[k];
            if (f->input != ACFG_KNOB) {
                continue;               /* paste config → Setup / Delete data, not a knob */
            }
            s_knob_ctx[kc] = (app_knob_ctx_t){ .ns = grp->ns, .f = f };
            setting_item_t *row = &s_rows[s_row_count++];
            memset(row, 0, sizeof(*row));
            row->label = f->label;
            row->get   = app_knob_get;
            row->set   = app_knob_set;
            row->ctx   = &s_knob_ctx[kc++];
            if (f->type == ACFG_BOOL) {
                row->kind = SETTING_TOGGLE;
            } else {
                row->kind = SETTING_RANGE;
                row->min  = (int)f->min;
                row->max  = (int)f->max;
                row->step = 1;
            }
        }
    }
}

/* ── app lifecycle ── */
static void settings_init(void)
{
    s_portal_up = false;
    confirm_reset();                    /* never inherit a stale modal */
    startup_build_choices();            /* dynamic ENUM choices from the app registry */
    settings_build_rows();
    settings_menu_init(&s_menu, s_rows, s_row_count, SETTINGS_LIST_ROWS);
    if (s_enter_setup) {
        s_enter_setup = false;
        portal_start();                 /* boot provisioning → straight into the portal */
    } else {
        s_mode = MODE_MENU;
    }
}

static void settings_on_event(uint8_t ev)
{
    if (s_mode == MODE_PORTAL) {
        return;                         /* phone drives provisioning; Home exits */
    }
    if (confirm_active()) {             /* the modal gets first refusal, in any mode */
        confirm_input(ev);
        return;
    }
    if (s_mode == MODE_INFO) {          /* read-only: scroll, any click → back */
        switch (ev) {
        case EV_ENCODER_CW:  ui_list_move(&s_info_list, +1); break;
        case EV_ENCODER_CCW: ui_list_move(&s_info_list, -1); break;
        case EV_ENCODER_CLICK:
        case EV_SELECT:      s_mode = MODE_MENU;             break;
        default: break;
        }
        return;
    }
    if (s_mode == MODE_DELETE) {        /* pick a target → confirm → wipe */
        switch (ev) {
        case EV_ENCODER_CW:  ui_list_move(&s_del_list, +1); break;
        case EV_ENCODER_CCW: ui_list_move(&s_del_list, -1); break;
        case EV_ENCODER_CLICK:
        case EV_SELECT:
            s_del_sel = ui_list_sel(&s_del_list);
            snprintf(s_del_prompt, sizeof(s_del_prompt), "Delete %s data?",
                     s_del[s_del_sel].label);
            confirm_open(s_del_prompt, del_confirm_cb, NULL);
            break;
        default: break;
        }
        return;
    }
    if (s_mode == MODE_OTA) {           /* input ignored while running; else click = back */
        if (s_ota_state != OTA_RUNNING && (ev == EV_ENCODER_CLICK || ev == EV_SELECT)) {
            s_mode = MODE_MENU;
        }
        return;
    }
    settings_menu_input(&s_menu, ev);
}

static void settings_render(void)
{
    if (confirm_active()) {             /* modal owns the screen */
        confirm_render();
        return;
    }

    lv_obj_clean(ui_frame_content());

    if (s_mode == MODE_INFO) {
        ui_frame_set_hints(&SETTINGS_HINTS_INFO);
        ui_list_draw(&s_info_list, 0, info_row_text, NULL);
        return;
    }

    if (s_mode == MODE_DELETE) {
        ui_frame_set_hints(&SETTINGS_HINTS);
        ui_text_row(SETTINGS_TITLE_ROW, "Delete data");
        ui_list_draw(&s_del_list, SETTINGS_LIST_ROW, del_row_text, NULL);
        return;
    }

    if (s_mode == MODE_OTA) {
        ui_frame_set_hints(NULL);       /* full width, no controls while updating */
        switch (s_ota_state) {
        case OTA_RUNNING:
            ui_text_row(0, "Updating...");
            ui_text_row(1, "do not unplug");
            break;
        case OTA_NOURL:
            ui_text_row(0, "No update URL");
            ui_text_row(1, "set it in Setup");
            break;
        case OTA_FAILED:
        default:
            ui_text_row(0, "Update failed");
            ui_text_row(1, "click to go back");
            break;
        }
        return;
    }

    if (s_mode == MODE_PORTAL) {
        /* Instructional, phone-driven: no hint bar → full 128px width (§6). */
        ui_frame_set_hints(NULL);
        ui_text_row(PORTAL_TITLE_ROW, "Wi-Fi setup");
        ui_text_row(PORTAL_STEP1_ROW, "1 join wifi:");
        ui_text_row_scroll(PORTAL_SSID_ROW, " " SOFTAP_SSID);   /* long → scrolls */
        ui_text_row(PORTAL_STEP2_ROW, "2 open browser:");
        ui_text_row_scroll(PORTAL_URL_ROW, " " SOFTAP_IP);
        return;
    }

    ui_frame_set_hints(settings_menu_editing(&s_menu) ? &SETTINGS_HINTS_EDIT
                                                      : &SETTINGS_HINTS);
    ui_text_row(SETTINGS_TITLE_ROW, "Settings");
    settings_menu_render(&s_menu, SETTINGS_LIST_ROW);
}

static void settings_exit(void)
{
    portal_stop();                      /* idempotent — safe when the portal is down */
    confirm_reset();
    ESP_LOGI(TAG, "exit");
}

static const device_app_t settings_app = {
    .name     = "Settings",
    .init     = settings_init,
    .on_event = settings_on_event,
    .render   = settings_render,
    .exit     = settings_exit,
};

const device_app_t *app_settings_get(void) { return &settings_app; }

void app_settings_enter_setup(void) { s_enter_setup = true; }
