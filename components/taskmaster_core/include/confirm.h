/*
 * confirm.h — a reusable modal yes/no confirmation dialog (PLAN §8A.1 step 0).
 *
 * A single global modal for destructive actions (factory reset, restart, …). Open it
 * with a prompt + callback; while active it owns the screen. The host app must, in its
 * render()/on_event(), check confirm_active() FIRST and route to confirm_render()/
 * confirm_input() before its own UI. Rotate flips the No/Yes selection; Select/click
 * commits (invokes the callback with the choice) and closes. Home is OS-reserved and
 * still exits the app — call confirm_reset() in the app's init()/exit() so a stale
 * modal never lingers.
 */
#pragma once

#include "input.h"
#include <stdbool.h>

typedef void (*confirm_cb_t)(bool yes, void *ctx);

/* Raise the modal. `prompt` + `cb`/`ctx` must outlive the dialog (static is simplest).
 * The selection starts on "No" (safe default). */
void confirm_open(const char *prompt, confirm_cb_t cb, void *ctx);

bool confirm_active(void);
void confirm_input(input_event_t ev);   /* route input while active */
void confirm_render(void);              /* draw while active (owns the content area) */
void confirm_reset(void);               /* force-close without invoking the callback */
