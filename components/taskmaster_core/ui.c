/*
 * ui.c — the UI / Render task (PLAN §4.6, §5.2). See ui.h.
 */
#include "ui.h"

#include "input.h"
#include "app_manager.h"
#include "launcher.h"

#include "esp_log.h"

static const char *TAG = "ui";

typedef enum { MODE_LAUNCHER, MODE_APP } ui_mode_t;

/* Total, idempotent teardown of any active app (§6A), then draw the Launcher. */
static void enter_launcher(void)
{
    app_manager_switch_to(-1);
    launcher_open();
}

static void ui_task(void *arg)
{
    QueueHandle_t q    = (QueueHandle_t)arg;
    ui_mode_t     mode = MODE_LAUNCHER;

    enter_launcher();

    input_event_t ev;
    for (;;) {
        if (xQueueReceive(q, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Home is OS-reserved (§5.2): it never reaches an app — it's the escape
         * hatch back to the Launcher from anywhere, even mid-action. */
        if (ev == EV_HOME) {
            if (mode == MODE_APP) {
                ESP_LOGI(TAG, "HOME -> Launcher");
                enter_launcher();
                mode = MODE_LAUNCHER;
            }
            continue;
        }

        if (mode == MODE_LAUNCHER) {
            int pick = launcher_input(ev);
            if (pick >= 0) {
                const device_app_t *a = app_manager_switch_to(pick);
                if (a != NULL) {
                    ESP_LOGI(TAG, "enter app: %s", a->name ? a->name : "?");
                    mode = MODE_APP;
                    if (a->render) {
                        a->render();   /* first draw of the new app */
                    }
                }
            }
        } else { /* MODE_APP: mutate state, then render on demand (§4.2) */
            app_manager_dispatch(ev);
            const device_app_t *a = app_manager_active();
            if (a && a->render) {
                a->render();
            }
        }
    }
}

void ui_start(QueueHandle_t input_events)
{
    xTaskCreate(ui_task, "ui", 4096, (void *)input_events, 5, NULL);
}
