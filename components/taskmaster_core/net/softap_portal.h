/*
 * softap_portal.h — Phase-0 SoftAP + HTTP server + captive-portal DNS spike.
 * Proves the paste-from-phone provisioning path is viable on ESP-IDF (PLAN §7).
 * Phase 0 just serves a confirmation page; the real config form lands in Phase 2.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define SOFTAP_SSID "TaskMaster-Setup"
#define SOFTAP_IP   "192.168.4.1"

/* Bring up the open SoftAP, the HTTP server, and the captive DNS responder.
 * Re-entrant: the Wi-Fi stack is initialized once, so start/stop can cycle. */
esp_err_t softap_portal_start(void);

/* Tear down the HTTP server + DNS responder and stop the AP (radio left
 * initialized so a later start() — or STA — can bring it back). */
esp_err_t softap_portal_stop(void);

/* LAN config page: serve the SAME config form on the station IP (no AP / captive
 * DNS) so it can be edited from a browser on the network — gated by a Settings
 * toggle (§7A). The form is pre-filled + saves additively (blank secrets kept);
 * non-Wi-Fi edits apply live (no reboot). */
esp_err_t config_web_start(void);
void      config_web_stop(void);
bool      config_web_active(void);
