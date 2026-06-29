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
