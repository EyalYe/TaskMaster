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

/* Start the UI task. `input_events` is the queue returned by input_init().
 * The task owns dispatch from here on and never returns. */
void ui_start(QueueHandle_t input_events);
