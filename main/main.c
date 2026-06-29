/*
 * main.c — TaskMaster-C3 Phase 0 bring-up (PLAN §14, Phase 0).
 *
 * Exercises all four Phase-0 exit criteria in one firmware:
 *   1. App boots (ESP-IDF + partition table).
 *   2. OLED draws (SH1106 over I2C).
 *   3. knob/buttons register (encoder + 3 buttons → event queue, shown live).
 *   4. A phone loads a page served by the device over SoftAP (+ captive DNS).
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "sh1106.h"
#include "input.h"
#include "softap_portal.h"

static const char *TAG = "main";

static void draw_splash(void)
{
    sh1106_clear();
    sh1106_text_line(0, "TASKMASTER-C3");
    sh1106_text_line(1, "PHASE 0 BRINGUP");
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

    /* (2) OLED */
    if (sh1106_init() != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed — check wiring / I2C address");
    }
    draw_splash();

    /* (3) input */
    QueueHandle_t events = input_init();

    /* (4) SoftAP + HTTP + captive DNS */
    ESP_ERROR_CHECK(softap_portal_start());

    ESP_LOGI(TAG, "Phase 0 up. Join '%s' and browse http://%s", SOFTAP_SSID, SOFTAP_IP);

    /* Live event loop: log + show the last input on the OLED. */
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
