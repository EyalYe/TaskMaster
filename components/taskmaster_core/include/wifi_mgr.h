/*
 * wifi_mgr.h — the Wi-Fi link manager (PLAN §7A.3).
 *
 * The single owner of esp_wifi_* for Station mode. Connects using the NVS creds
 * (wifi_ssid / wifi_psk), retries with backoff, and drives net_status
 * (CONNECTING → CONNECTED / DISCONNECTED) so the rest of the system — and apps —
 * just read net_status.h. Mutually exclusive with the SoftAP portal per boot
 * (the boot-mode branch picks one, §7A.3).
 */
#pragma once

#include "esp_err.h"

/* One-time Wi-Fi stack init (netif + default event loop + esp_wifi). Idempotent. */
esp_err_t wifi_mgr_init(void);

/* Start Station mode and connect from NVS creds. Drives net_status; retries on
 * disconnect. Returns ESP_ERR_INVALID_STATE if no SSID is configured. */
esp_err_t wifi_mgr_start_sta(void);

/* Stop Wi-Fi and power the radio down (sets NET_WIFI_OFF). */
esp_err_t wifi_mgr_stop(void);
