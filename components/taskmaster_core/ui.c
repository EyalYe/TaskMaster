/*
 * ui.c — the UI / Render task (PLAN §4.6, §5.2). See ui.h.
 */
#include "ui.h"

#include "input.h"
#include "app_manager.h"
#include "launcher.h"
#include "net_status.h"
#include "leak_test.h"
#include "lvgl_disp.h"
#include "ui_frame.h"

#include "esp_log.h"

static const char *TAG = "ui";

typedef enum { MODE_LAUNCHER, MODE_APP } ui_mode_t;

static const device_app_t *s_initial_app;   /* boot app, NULL = Launcher */

/* Total, idempotent teardown of any active app (§6A), then draw the Launcher. */
static void enter_launcher(void)
{
    app_manager_switch_to(-1);
    launcher_open();
}

/* Re-render whatever's on screen now (used for system events like net change). */
static void render_current(ui_mode_t mode)
{
    if (mode == MODE_LAUNCHER) {
        launcher_render();
    } else {
        const device_app_t *a = app_manager_active();
        if (a && a->render) {
            a->render();
        }
    }
}

static void ui_task(void *arg)
{
    QueueHandle_t q    = (QueueHandle_t)arg;
    ui_mode_t     mode = MODE_LAUNCHER;

    /* The UI task is the single owner of LVGL: it builds the OS frame and is the
     * only thread that pumps lv_timer_handler / touches lv_* (no mutex, §5.2). */
    ui_frame_init();

#if CONFIG_TM_LEAK_TEST
    leak_test_run();                 /* §6A.4 harness (debug builds only) */
#endif

    /* Boot straight into the initial app (e.g. Setup in provisioning mode), else
     * the Launcher. Falls back to the Launcher if the app isn't registered. */
    int idx = s_initial_app ? app_manager_index_of(s_initial_app) : -1;
    if (idx >= 0) {
        const device_app_t *a = app_manager_switch_to(idx);
        mode = MODE_APP;
        ESP_LOGI(TAG, "boot app: %s", a && a->name ? a->name : "?");
        if (a && a->render) {
            a->render();
        }
    } else {
        enter_launcher();
    }

    input_event_t ev;
    for (;;) {
        /* Pump LVGL, then wait for an input event up to the next LVGL deadline so
         * animations/redraws keep flowing even with no input. */
        uint32_t next = lvgl_disp_tick();
        if (next > UI_LVGL_MAX_IDLE_MS) next = UI_LVGL_MAX_IDLE_MS;
        if (next < UI_LVGL_MIN_MS)      next = UI_LVGL_MIN_MS;

        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(next)) != pdTRUE) {
            continue;                    /* timeout → loop and pump LVGL again */
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

        /* System events (e.g. connectivity change): handled here, never delivered
         * to an app. The contract is just "your render() gets called" — apps read
         * net_status_get() and redraw (net_status.h). */
        if (ev == EV_SYS_NET_CHANGED) {
            render_current(mode);
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

void ui_start(QueueHandle_t input_events, const device_app_t *initial_app)
{
    s_initial_app = initial_app;
    /* Connectivity changes are posted onto this same queue so the UI re-renders. */
    net_status_attach_ui(input_events);
    xTaskCreate(ui_task, "ui", UI_TASK_STACK, (void *)input_events, UI_TASK_PRIO, NULL);
}
