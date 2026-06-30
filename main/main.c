/*
 * main.c — TaskMaster-C3 firmware composition root (PLAN §14, Phase 1).
 *
 * Thin: brings up NVS + the shared model + core platform services (display, input,
 * provisioning portal), enumerates the apps that self-registered (via constructors)
 * from their own components, then hands the screen and input queue to the UI task.
 * From there the UI task owns the Launcher, app lifecycle, and Home (PLAN §5.2/§6).
 */
#include "esp_log.h"
#include "nvs_flash.h"

#include "sh1106.h"
#include "input.h"
#include "app_manager.h"
#include "app_config.h"
#include "app_setup.h"
#include "net_status.h"
#include "nvs_config.h"
#include "wifi_mgr.h"
#include "lvgl_disp.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    /* NVS is required by the Wi-Fi stack. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    net_status_init();
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(wifi_mgr_init());   /* one Wi-Fi init shared by STA + the portal */

    if (sh1106_init() != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed — check wiring / I2C address");
    }
    lvgl_disp_init();   /* LVGL on the panel (via the sh1106 framebuffer); the UI task pumps it */

    /* Register core apps (non-removable). User apps self-registered via their
     * constructors before app_main; core apps are owned by the core. */
    app_manager_register(app_setup_get());

    ESP_LOGI(TAG, "Registered apps: %u", app_manager_count());
    for (unsigned i = 0; i < app_manager_count(); i++) {
        ESP_LOGI(TAG, "  app[%u] = %s", i, app_manager_get(i)->name);
    }

    /* App-declared config groups (§9.4) — what the form/Settings will assemble. */
    ESP_LOGI(TAG, "App config groups: %u", app_config_group_count());
    for (unsigned i = 0; i < app_config_group_count(); i++) {
        const app_cfg_group_t *g = app_config_group(i);
        ESP_LOGI(TAG, "  cfg[%s] '%s' x%u", g->ns, g->name, g->count);
    }

    /* Read Home before input_init() so a held button at reset forces setup. */
    bool home_held    = input_home_held();
    QueueHandle_t events = input_init();

    /* ── Boot-mode branch (§7A.3) ──
     * Provisioning wins if Home is held at boot or the device is unprovisioned;
     * else honor the Wi-Fi master toggle; else connect as a station. In
     * provisioning mode the UI auto-launches the Setup app, whose init() raises
     * the portal — so STA and the portal stay mutually exclusive per boot. */
    uint8_t wifi_en = 1;
    config_get_u8("wifi_en", &wifi_en);
    bool provisioned = config_is_provisioned();
    ESP_LOGI(TAG, "boot: home_held=%d provisioned=%d wifi_en=%d", home_held, provisioned, wifi_en);

    const device_app_t *initial = NULL;   /* NULL = Launcher */
    if (home_held || !provisioned) {
        ESP_LOGI(TAG, "%s -> Setup app", home_held ? "Home held at boot" : "unprovisioned");
        initial = app_setup_get();        /* auto-launch Setup; its init() raises the portal */
    } else if (!wifi_en) {
        ESP_LOGI(TAG, "Wi-Fi off -> offline");
        net_status_set(NET_WIFI_OFF, 0);
    } else {
        ESP_LOGI(TAG, "provisioned + Wi-Fi on -> STA connect");
        wifi_mgr_start_sta();
    }

    /* The UI task owns the screen, app lifecycle, and Home from here. */
    ui_start(events, initial);
}
