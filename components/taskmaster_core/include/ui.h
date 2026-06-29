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

/* Start the UI task. `input_events` is the queue returned by input_init().
 * `initial_app` (or NULL for the Launcher) is the app shown at boot — used to
 * auto-launch the Setup app in provisioning mode (§7A.3/§7A.4). The task owns
 * dispatch from here on and never returns. */
void ui_start(QueueHandle_t input_events, const device_app_t *initial_app);
