/*
 * ui.h — the UI / Render task (PLAN §4.6, §5.2).
 *
 * The single owner of the active-app pointer and the Launcher, and the only place
 * app lifecycle (init/on_event/render/exit) runs — so app switches are cooperative
 * and race-free by construction (§6, §6A). Consumes the input queue; intercepts
 * Home; forwards everything else to the Launcher or the active app.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "app.h"

/* UI task: 8KB stack since it also pumps LVGL rendering (was a separate task). */
#define UI_TASK_STACK       8192
#define UI_TASK_PRIO        5
#define UI_INPUT_QUEUE_LEN  16   /* (see input.c) */

/* LVGL pump cadence: wait for input up to the next LVGL deadline, clamped so the
 * loop stays responsive but doesn't busy-spin. */
#define UI_LVGL_MAX_IDLE_MS 30
#define UI_LVGL_MIN_MS      5

/* Start the UI task. `input_events` is the queue returned by input_init().
 * `initial_app` (or NULL for the Launcher) is the app shown at boot — used to
 * auto-launch the Setup app in provisioning mode (§7A.3/§7A.4). The task owns
 * dispatch from here on and never returns. */
void ui_start(QueueHandle_t input_events, const device_app_t *initial_app);
