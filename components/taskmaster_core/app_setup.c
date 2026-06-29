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
#include "net_status.h"
#include "sh1106.h"
#include "esp_log.h"

static const char *TAG = "app.setup";
static bool s_portal_up;

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
    sh1106_clear();
    sh1106_text_line(0, "SETUP / WIFI");
    sh1106_text_line(2, "1 JOIN WIFI:");
    sh1106_text_line(3, "  " SOFTAP_SSID);
    sh1106_text_line(4, "2 OPEN IN BROWSER:");
    sh1106_text_line(5, "  " SOFTAP_IP);
    sh1106_text_line(7, "PASTE CFG ON PHONE");
    sh1106_flush();
}

static void setup_exit(void)
{
    if (s_portal_up) {
        softap_portal_stop();
        s_portal_up = false;
    }
    /* Step 7 will restore a prior STA link here when re-provisioned. */
    net_status_set(NET_DISCONNECTED, 0);
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
