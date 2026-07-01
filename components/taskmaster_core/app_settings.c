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
#include "ui_frame.h"
#include "settings_menu.h"
#include "confirm.h"
#include "esp_system.h"
#include "esp_log.h"

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

typedef enum { MODE_MENU, MODE_PORTAL } settings_mode_t;

/* SEL when navigating; OK when editing a value or in a confirm dialog. */
static const control_hints_t SETTINGS_HINTS      = { .rotate = "<>", .click = "SEL", .select = "SEL" };
static const control_hints_t SETTINGS_HINTS_EDIT = { .rotate = "<>", .click = "OK",  .select = "OK" };

static settings_mode_t s_mode;
static bool            s_portal_up;
static bool            s_enter_setup;   /* boot asked us to open in portal mode */
static settings_menu_t s_menu;

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
static int wifi_get(void)
{
    uint8_t en = 1;                     /* default on if never set */
    config_get_u8("wifi_en", &en);
    return en != 0 ? 1 : 0;
}

/* Flip the Wi-Fi master switch: persist WIFI_EN + drive the radio. This is what takes
 * the device (and the task apps) offline/online (§8.3, step 12). */
static void wifi_set(int on)
{
    config_set_u8("wifi_en", on ? 1 : 0);
    if (on) {
        wifi_mgr_start_sta();           /* CONNECTING → CONNECTED */
    } else {
        wifi_mgr_stop();                /* radio down → NET_WIFI_OFF */
    }
    ESP_LOGI(TAG, "Wi-Fi %s", on ? "on" : "off");
}

static void act_wifi_setup(void)    { portal_start(); }
static void act_restart(void)       { esp_restart(); }
static void act_factory_reset(void) { config_factory_reset(); esp_restart(); }

/* The declared settings table — add a setting here, not a new screen (§8A.1 step 0).
 * ENUM/RANGE rows (startup / brightness / timeout) land in the following steps. */
static const setting_item_t SETTINGS_ITEMS[] = {
    { .label = "Wi-Fi",         .kind = SETTING_TOGGLE, .get = wifi_get, .set = wifi_set },
    { .label = "Wi-Fi setup",   .kind = SETTING_ACTION, .action = act_wifi_setup },
    { .label = "Restart",       .kind = SETTING_ACTION, .action = act_restart,
      .confirm = "Restart device?" },
    { .label = "Factory reset", .kind = SETTING_ACTION, .action = act_factory_reset,
      .confirm = "Erase all settings?" },
};
#define SETTINGS_ITEM_COUNT ((int)(sizeof(SETTINGS_ITEMS) / sizeof(SETTINGS_ITEMS[0])))

/* ── app lifecycle ── */
static void settings_init(void)
{
    s_portal_up = false;
    confirm_reset();                    /* never inherit a stale modal */
    settings_menu_init(&s_menu, SETTINGS_ITEMS, SETTINGS_ITEM_COUNT, SETTINGS_LIST_ROWS);
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
    if (confirm_active()) {             /* the modal gets first refusal */
        confirm_input(ev);
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
