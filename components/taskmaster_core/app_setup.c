/*
 * app_setup.c — the Setup/Wi-Fi core app. See app_setup.h.
 *
 * init()  → raise the SoftAP portal + mark NET_PORTAL + draw the instructions.
 * render()→ instructional only (the phone drives setup; the knob enters nothing).
 * exit()  → tear the portal down cleanly (§6A). Restoring a prior STA connection
 *           on re-provision-from-Launcher lands in step 7 (§7A.11).
 */
#include "app_setup.h"

#include "softap_portal.h"
#include "wifi_mgr.h"
#include "net_status.h"
#include "ui_frame.h"
#include "esp_log.h"

static const char *TAG = "app.setup";
static bool s_portal_up;

#define SETUP_TITLE_ROW   0
#define SETUP_STEP1_ROW   2
#define SETUP_SSID_ROW    3
#define SETUP_STEP2_ROW   4
#define SETUP_URL_ROW     5

static void setup_init(void)
{
    if (!s_portal_up && softap_portal_start() == ESP_OK) {
        s_portal_up = true;
    }
    net_status_set(NET_PORTAL, 0);
    ESP_LOGI(TAG, "init: portal up — join '%s', open http://%s", SOFTAP_SSID, SOFTAP_IP);
}

static void setup_on_event(uint8_t ev)
{
    (void)ev;   /* the phone drives provisioning; no on-device input needed yet */
}

static void setup_render(void)
{
    /* Instructional, phone-driven: no hint bar → use the full 128px width (§6). */
    lv_obj_clean(ui_frame_content());
    ui_frame_set_hints(NULL);
    ui_text_row(SETUP_TITLE_ROW, "Setup / WiFi");
    ui_text_row(SETUP_STEP1_ROW, "1 join wifi:");
    ui_text_row(SETUP_SSID_ROW,  " " SOFTAP_SSID);
    ui_text_row(SETUP_STEP2_ROW, "2 open browser:");
    ui_text_row(SETUP_URL_ROW,   " " SOFTAP_IP);
}

static void setup_exit(void)
{
    if (s_portal_up) {
        softap_portal_stop();      /* drops the AP; any prior STA link is kept */
        s_portal_up = false;
    }
    /* Restore the connectivity indicator from the (possibly still-connected) STA. */
    wifi_mgr_refresh_status();
    ESP_LOGI(TAG, "exit: portal down");
}

static const device_app_t setup_app = {
    .name     = "Setup",
    .init     = setup_init,
    .on_event = setup_on_event,
    .render   = setup_render,
    .exit     = setup_exit,
};

const device_app_t *app_setup_get(void)
{
    return &setup_app;
}
