/*
 * app_settings.h — the Settings core app (PLAN §6, §7A.4, §9.3).
 *
 * A non-removable core app (compiled into taskmaster_core): the device hub. It
 * folds in the old Setup app — a menu with a Wi-Fi on/off toggle and "Wi-Fi
 * setup", which raises the SoftAP provisioning portal (the instructional,
 * phone-driven screen). Auto-launched into the portal at boot when the device is
 * unprovisioned (or Home held at boot, §7A.3), and reachable from the Launcher
 * anytime.
 */
#pragma once

#include "app.h"

/* The Settings app instance — registered into the app registry at boot, and
 * passed to ui_start() as the initial app when booting into provisioning mode. */
const device_app_t *app_settings_get(void);

/* Ask the next init() to open straight into the Wi-Fi-setup portal (used by the
 * boot-mode branch when unprovisioned or Home-held at boot). */
void app_settings_enter_setup(void);
