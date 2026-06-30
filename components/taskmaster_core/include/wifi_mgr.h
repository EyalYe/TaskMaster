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

/* One-time Wi-Fi stack init: both netifs + default event loop + esp_wifi + STA
 * event handlers. Idempotent. Call once at boot before any start_*. */
esp_err_t wifi_mgr_init(void);

/* Start Station mode and connect from NVS creds. Drives net_status; retries on
 * disconnect. Returns ESP_ERR_INVALID_STATE if no SSID is configured. */
esp_err_t wifi_mgr_start_sta(void);

/* Raise the SoftAP (always AP+STA so the STA iface stays available for scanning
 * and an existing STA link is preserved). Used by the provisioning portal. */
void wifi_mgr_ap_start(const char *ssid);

/* Drop the SoftAP, returning to STA-only (if a station link is up) or idle. */
void wifi_mgr_ap_stop(void);

/* Re-derive net_status from the current STA link (CONNECTED+rssi / DISCONNECTED).
 * Call after dropping the AP to restore the connectivity indicator. */
void wifi_mgr_refresh_status(void);

/* Stop Wi-Fi entirely and power the radio down (sets NET_WIFI_OFF). */
esp_err_t wifi_mgr_stop(void);
