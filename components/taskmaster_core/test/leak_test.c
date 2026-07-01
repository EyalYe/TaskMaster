/*
 * leak_test.c — §6A.4 leak-clean teardown harness. See leak_test.h.
 *
 * For each app: one warm-up cycle (to absorb legitimate one-time driver/heap
 * allocations, e.g. the portal's Wi-Fi/httpd), then take a free-heap baseline and
 * run N launch→event→render→Home cycles. The free heap should return to baseline;
 * a per-cycle leak makes it fall by ~N×leak. Reports the delta + PASS/LEAK per app.
 */
#include "leak_test.h"
#include "sdkconfig.h"   /* must precede the CONFIG_ check below */

#if CONFIG_TM_LEAK_TEST

#include "app_manager.h"
#include "input.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "leak";

static void cycle(int i, const device_app_t *app)
{
    app_manager_switch_to(i);                 /* init  */
    if (app->on_event) {
        app->on_event(EV_ENCODER_CW);         /* mutate a little */
    }
    if (app->render) {
        app->render();
    }
    app_manager_switch_to(-1);                /* exit (= Home) */
}

void leak_test_run(void)
{
    const int iters = CONFIG_TM_LEAK_TEST_ITERS;
    unsigned n = app_manager_count();
    ESP_LOGW(TAG, "=== §6A.4 leak test: %u apps × %d cycles ===", n, iters);

    for (unsigned i = 0; i < n; i++) {
        const device_app_t *app = app_manager_get(i);

        cycle((int)i, app);                   /* warm-up: absorb one-time allocs */
        vTaskDelay(pdMS_TO_TICKS(100));

        size_t base = esp_get_free_heap_size();
        for (int c = 0; c < iters; c++) {
            cycle((int)i, app);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        size_t after = esp_get_free_heap_size();

        long delta = (long)after - (long)base;
        bool ok = delta > -128;               /* allow tiny allocator noise */
        ESP_LOGW(TAG, "app '%s': base=%u after=%u delta=%ld  -> %s",
                 app->name ? app->name : "?", (unsigned)base, (unsigned)after,
                 delta, ok ? "PASS" : "LEAK");
        if (!heap_caps_check_integrity_all(true)) {
            ESP_LOGE(TAG, "app '%s': HEAP INTEGRITY FAILED", app->name);
        }
    }

    app_manager_switch_to(-1);                /* leave in the Launcher */
    ESP_LOGW(TAG, "=== leak test done (min_free_heap=%u) ===",
             (unsigned)esp_get_minimum_free_heap_size());
}

#endif /* CONFIG_TM_LEAK_TEST */
