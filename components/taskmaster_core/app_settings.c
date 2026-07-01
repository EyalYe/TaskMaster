/*
 * app_settings.c — the Settings core app (device hub). See app_settings.h.
 *
 * Two modes:
 *   MENU   → a ui_list of settings: Wi-Fi on/off toggle, "Wi-Fi setup".
 *   PORTAL → the SoftAP provisioning portal is up; the screen shows the join
 *            instructions (phone-driven, no on-device input). Folded in from the
 *            old Setup app.
 *
 * The Wi-Fi toggle persists WIFI_EN (nvs_config) and drives wifi_mgr, so the rest
 * of the system — and the task apps — just read net_status (this is what makes an
 * app go offline, §8.3 / step 12). Home is OS-reserved: from either mode it exits
 * to the Launcher, tearing the portal down cleanly (§6A) via exit().
 */
#include "app_settings.h"

#include "softap_portal.h"
#include "wifi_mgr.h"
#include "net_status.h"
#include "nvs_config.h"
#include "ui_frame.h"
#include "ui_list.h"
#include "input.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "app.settings";

/* Menu layout. */
#define SETTINGS_TITLE_ROW  0
#define SETTINGS_LIST_ROW   1                    /* list starts below the title */
#define SETTINGS_LIST_ROWS  (UI_ROWS - 1)        /* rows below the title */

/* Menu entries, in row order. */
enum {
    SETTINGS_ITEM_WIFI = 0,     /* Wi-Fi on/off toggle */
    SETTINGS_ITEM_SETUP,        /* raise the provisioning portal */
    SETTINGS_ITEM_COUNT
};

/* Portal (instructional) layout — mirrors the old Setup screen. */
#define PORTAL_TITLE_ROW    0
#define PORTAL_STEP1_ROW    1
#define PORTAL_SSID_ROW     2
#define PORTAL_STEP2_ROW    3
#define PORTAL_URL_ROW      4

typedef enum { MODE_MENU, MODE_PORTAL } settings_mode_t;

static const control_hints_t SETTINGS_HINTS = { .rotate = "<>", .click = "SEL", .select = "SEL" };

static settings_mode_t s_mode;
static bool            s_portal_up;
static bool            s_enter_setup;   /* boot asked us to open in portal mode */
static ui_list_t       s_list;

static bool wifi_is_on(void)
{
    uint8_t en = 1;                     /* default on if never set */
    config_get_u8("wifi_en", &en);
    return en != 0;
}

/* ui_list row text for the settings menu. */
static void settings_row_text(int index, char *buf, int buf_sz, void *ctx)
{
    (void)ctx;
    switch (index) {
    case SETTINGS_ITEM_WIFI:
        snprintf(buf, buf_sz, "Wi-Fi: %s", wifi_is_on() ? "On" : "Off");
        break;
    case SETTINGS_ITEM_SETUP:
        snprintf(buf, buf_sz, "Wi-Fi setup");
        break;
    default:
        snprintf(buf, buf_sz, "?");
        break;
    }
}

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

/* Flip the Wi-Fi master switch: persist WIFI_EN + drive the radio. This is what
 * takes the device (and the task apps) offline/online (§8.3, step 12). */
static void wifi_toggle(void)
{
    bool turn_on = !wifi_is_on();
    config_set_u8("wifi_en", turn_on ? 1 : 0);
    if (turn_on) {
        wifi_mgr_start_sta();           /* CONNECTING → CONNECTED */
    } else {
        wifi_mgr_stop();                /* radio down → NET_WIFI_OFF */
    }
    ESP_LOGI(TAG, "Wi-Fi %s", turn_on ? "on" : "off");
}

static void settings_init(void)
{
    s_portal_up = false;
    ui_list_init(&s_list, SETTINGS_LIST_ROWS);
    ui_list_set_count(&s_list, SETTINGS_ITEM_COUNT);
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
    switch (ev) {
    case EV_ENCODER_CW:  ui_list_move(&s_list, +1); break;
    case EV_ENCODER_CCW: ui_list_move(&s_list, -1); break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:
        switch (ui_list_sel(&s_list)) {
        case SETTINGS_ITEM_WIFI:  wifi_toggle();  break;
        case SETTINGS_ITEM_SETUP: portal_start(); break;
        default: break;
        }
        break;
    default:
        break;
    }
}

static void settings_render(void)
{
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

    ui_frame_set_hints(&SETTINGS_HINTS);
    ui_text_row(SETTINGS_TITLE_ROW, "Settings");
    ui_list_draw(&s_list, SETTINGS_LIST_ROW, settings_row_text, NULL);
}

static void settings_exit(void)
{
    portal_stop();                      /* idempotent — safe when the portal is down */
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
