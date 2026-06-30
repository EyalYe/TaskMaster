/*
 * input.h — Phase-0 input bring-up. A 1ms polling task decodes the EC11 encoder
 * (Ben-Buxton quadrature state machine) and debounces the three buttons, posting
 * normalized events to a FreeRTOS queue. Home is delivered like any event here;
 * the OS-reserved interception happens in app_manager (PLAN §5.2), out of scope
 * for Phase 0 — we just prove every input registers.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    /* Physical input (delivered to the active app's on_event, except Home). */
    EV_ENCODER_CW = 1,
    EV_ENCODER_CCW,
    EV_ENCODER_CLICK,   /* encoder push switch */
    EV_SELECT,          /* Select button */
    EV_HOME,            /* Home button (OS-reserved, §5.2) */

    /* System events: posted by core onto the same UI event queue, handled by the
     * UI task, and NEVER delivered to an app's on_event. Apps react by reading the
     * relevant status in render() (the UI re-renders them when these fire). */
    EV_SYS_NET_CHANGED = 0x40,   /* connectivity changed (net_status.h) */
    EV_SYS_JOB_DONE    = 0x41,   /* an async_job finished (async_job.h) */
} input_event_t;

/* Start the input task. Returns a queue of input_event_t to read from. */
QueueHandle_t input_init(void);

/* One-shot read of the Home button at boot (active-low). Call before input_init()
 * to detect "Home held at reset" → force provisioning mode (PLAN §7A.3). */
bool input_home_held(void);

/* Human-readable name for logging / on-screen display. */
const char *input_event_name(input_event_t ev);
