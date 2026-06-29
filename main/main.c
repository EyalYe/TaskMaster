/*
 * main.c — TaskMaster-C3 firmware composition root (PLAN §14, Phase 1).
 *
 * Thin: brings up NVS + core platform services, then lists the apps that
 * self-registered (via constructors) from their own components before app_main.
 * Proves the manifest-driven, self-registering app model across the component
 * boundary (taskmaster_core ← app_hello, listed in main/idf_component.yml).
 * The Phase-0 bring-up (OLED / input / SoftAP) now lives in taskmaster_core.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "sh1106.h"
#include "input.h"
#include "softap_portal.h"
#include "app_manager.h"

static const char *TAG = "main";

static void draw_splash(void)
{
    char apps[24];
    sh1106_clear();
    sh1106_text_line(0, "TASKMASTER-C3");
    sh1106_text_line(1, "PHASE 1 CORE");
    snprintf(apps, sizeof(apps), "APPS: %u", app_manager_count());
    sh1106_text_line(2, apps);
    sh1106_text_line(3, "AP: TASKMASTER-SETUP");
    sh1106_text_line(4, "HTTP 192.168.4.1");
    sh1106_text_line(6, "LAST EVENT:");
    sh1106_text_line(7, "(turn / press)");
    sh1106_flush();
}

void app_main(void)
{
    /* NVS is required by the Wi-Fi stack. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    if (sh1106_init() != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed — check wiring / I2C address");
    }

    /* Apps self-registered before app_main — enumerate the registry. */
    ESP_LOGI(TAG, "Registered apps: %u", app_manager_count());
    for (unsigned i = 0; i < app_manager_count(); i++) {
        ESP_LOGI(TAG, "  app[%u] = %s", i, app_manager_get(i)->name);
    }

    draw_splash();

    QueueHandle_t events = input_init();
    ESP_ERROR_CHECK(softap_portal_start());

    ESP_LOGI(TAG, "Phase 1 up. Join '%s' and browse http://%s", SOFTAP_SSID, SOFTAP_IP);

    /* Live input loop (Launcher UI replaces this later in Phase 1). */
    uint32_t count = 0;
    input_event_t ev;
    for (;;) {
        if (xQueueReceive(events, &ev, portMAX_DELAY) == pdTRUE) {
            const char *name = input_event_name(ev);
            ESP_LOGI(TAG, "input #%lu: %s", (unsigned long)++count, name);

            char line[24];
            snprintf(line, sizeof(line), "%lu %s", (unsigned long)count, name);
            sh1106_text_line(7, line);
            sh1106_flush();
        }
    }
}
