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
#include "softap_portal.h"
#include "app_manager.h"
#include "task_model.h"
#include "net_status.h"
#include "nvs_config.h"
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

    task_model_init();
    net_status_init();
    ESP_ERROR_CHECK(config_init());
    ESP_LOGI(TAG, "provisioned: %d", config_is_provisioned());   /* boot-mode branch lands in §7A.3 */

    if (sh1106_init() != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed — check wiring / I2C address");
    }

    /* Apps self-registered before app_main — enumerate the registry. */
    ESP_LOGI(TAG, "Registered apps: %u", app_manager_count());
    for (unsigned i = 0; i < app_manager_count(); i++) {
        ESP_LOGI(TAG, "  app[%u] = %s", i, app_manager_get(i)->name);
    }

    QueueHandle_t events = input_init();
    ESP_ERROR_CHECK(softap_portal_start());
    net_status_set(NET_PORTAL, 0);   /* SoftAP provisioning portal is up (§7) */
    ESP_LOGI(TAG, "Phase 1 up. Join '%s' and browse http://%s", SOFTAP_SSID, SOFTAP_IP);

    /* The UI task owns the screen, the Launcher, and app lifecycle from here. */
    ui_start(events);
}
