/*
 * app_manager.h — the app registry (PLAN §6.1).
 * Apps self-register via TASKMASTER_REGISTER_APP (app.h); the Launcher and the
 * boot code query the registry through here.
 */
#pragma once

#include "app.h"

/* Number of registered (enabled) apps. */
unsigned app_manager_count(void);

/* Registered app at index i (registration order = Launcher order), or NULL. */
const device_app_t *app_manager_get(unsigned i);

/* ── Lifecycle ── all of the following MUST be called from the UI task only
 * (PLAN §5.2/§6): that's what makes app switches cooperative and race-free. */

/* Switch the active app: exit() the current one (if any), then init() the new.
 * index < 0 (or out of range) selects "no app" — the Launcher owns the screen.
 * exit() is run unconditionally and totally (§6A). Returns the now-active app,
 * or NULL when none/Launcher. */
const device_app_t *app_manager_switch_to(int index);

/* The currently active app, or NULL when in the Launcher. */
const device_app_t *app_manager_active(void);

/* Registry index of a registered app, or -1 if not found. */
int app_manager_index_of(const device_app_t *app);

/* Forward an input event to the active app's on_event (no-op when none). */
void app_manager_dispatch(uint8_t ev);
