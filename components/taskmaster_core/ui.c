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
#include "async_job.h"
#include "nvs_config.h"
#include "sh1106.h"

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ui";

#define UI_BLANK_POLL_MS 1000   /* loop cadence while the panel is blanked (await input) */

typedef enum { MODE_LAUNCHER, MODE_APP } ui_mode_t;

static const device_app_t *s_initial_app;   /* boot app, NULL = Launcher */

/* Inactivity blanking (§8A step 4): blank the panel after idle_to_s of no user input;
 * the next user press wakes it (and is consumed). System events don't count as input. */
static uint32_t s_last_input_ms;
static bool     s_blanked;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool is_user_input(input_event_t ev)
{
    return ev == EV_ENCODER_CW || ev == EV_ENCODER_CCW || ev == EV_ENCODER_CLICK ||
           ev == EV_SELECT || ev == EV_HOME;
}

static void screen_wake(void)
{
    if (s_blanked) {
        sh1106_display_power(true);
        s_blanked = false;
    }
}

/* Blank the panel once idle_to_s (0 = off) has elapsed with no user input. */
static void maybe_blank(void)
{
    if (s_blanked) {
        return;
    }
    uint16_t to_s = 0;
    config_get_u16("idle_to_s", &to_s);
    if (to_s == 0) {
        return;   /* timeout Off */
    }
    if (now_ms() - s_last_input_ms >= (uint32_t)to_s * 1000) {
        sh1106_display_power(false);
        s_blanked = true;
    }
}

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
    s_last_input_ms = now_ms();
    for (;;) {
        /* Pump LVGL, then wait for an input event up to the next LVGL deadline so
         * animations/redraws keep flowing even with no input. While blanked, don't
         * pump LVGL — just wait for the wake press (§8A). */
        uint32_t next;
        if (s_blanked) {
            /* Stage 2 (§8A): if deep sleep is enabled, halt in light sleep until a
             * pin wakes us — the woken press is then read by the input task and
             * delivered below (wakes the screen). Else just idle-poll (Stage 1). */
            uint8_t deep = 0;
            config_get_u8("deep_sleep", &deep);
            if (deep) {
                ESP_LOGI(TAG, "light sleep");
                input_light_sleep();
                ESP_LOGI(TAG, "woke");
            }
            next = UI_BLANK_POLL_MS;
        } else {
            next = lvgl_disp_tick();
            if (next > UI_LVGL_MAX_IDLE_MS) next = UI_LVGL_MAX_IDLE_MS;
            if (next < UI_LVGL_MIN_MS)      next = UI_LVGL_MIN_MS;
        }

        if (xQueueReceive(q, &ev, pdMS_TO_TICKS(next)) != pdTRUE) {
            maybe_blank();               /* idle → blank the panel (§8A) */
            continue;
        }

        /* User input resets the idle timer; the first press after blanking only wakes
         * the screen (it isn't also delivered as an action). System events (net/job)
         * neither wake the screen nor count as activity. */
        if (is_user_input(ev)) {
            s_last_input_ms = now_ms();
            if (s_blanked) {
                screen_wake();
                render_current(mode);
                continue;
            }
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
         * net_status_get() and redraw (net_status.h). Skip the draw while blanked. */
        if (ev == EV_SYS_NET_CHANGED) {
            if (!s_blanked) render_current(mode);
            continue;
        }

        /* An async_job finished: run its done() on the UI task (frees ctx even when
         * blanked), then re-render — but only if the panel is on (async_job.h). */
        if (ev == EV_SYS_JOB_DONE) {
            async_job_deliver();
            if (!s_blanked) render_current(mode);
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
    /* Connectivity changes + async-job completions are posted onto this same queue
     * so the UI re-renders / delivers results on its own task. */
    net_status_attach_ui(input_events);
    async_job_init(input_events);
    xTaskCreate(ui_task, "ui", UI_TASK_STACK, (void *)input_events, UI_TASK_PRIO, NULL);
}
