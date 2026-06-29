/*
 * app_setup.h — the Setup/Wi-Fi core app (PLAN §6, §7A.4).
 *
 * A non-removable core app (compiled into taskmaster_core). It owns the SoftAP
 * provisioning portal lifecycle and shows the instructional screen. Auto-launched
 * at boot when the device is unprovisioned (or Home held at boot, §7A.3), and
 * reachable from the Launcher anytime to re-provision.
 */
#pragma once

#include "app.h"

/* The Setup app instance — registered into the app registry at boot, and passed
 * to ui_start() as the initial app when booting into provisioning mode. */
const device_app_t *app_setup_get(void);
