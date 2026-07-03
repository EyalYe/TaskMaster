/*
 * wifi_mgr.c — the single owner of esp_wifi_* (PLAN §7A.3).
 *
 * Owns Wi-Fi init and *mode coordination*: STA and AP are independent "wants"
 * (s_sta_on / s_ap_on) and apply_mode() derives the actual esp_wifi mode from
 * them. The AP path always uses AP+STA so the station interface stays available
 * for scanning and any existing STA link is preserved across raising the portal —
 * which is what makes re-provision-from-Launcher work without re-init (§7A, step 7).
 */
#include "wifi_mgr.h"

#include "net_status.h"
#include "nvs_config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "wifi";

static bool              s_inited;
static bool              s_started;
static bool              s_sta_on;       /* want a station connection */
static bool              s_ap_on;        /* want the SoftAP up        */
static esp_timer_handle_t s_retry_timer;
static int               s_retry;

/* Set the esp_wifi mode from the current wants. AP implies AP+STA (keep the STA
 * iface for scanning / an existing link). Does NOT start — see ensure_started(),
 * so callers can set_config between set_mode and start. */
static void set_mode_for_wants(void)
{
    wifi_mode_t m = s_ap_on ? WIFI_MODE_APSTA : (s_sta_on ? WIFI_MODE_STA : WIFI_MODE_NULL);
    if (m == WIFI_MODE_NULL) {
        if (s_started) {
            esp_wifi_stop();
            s_started = false;
        }
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(m));
}

static void ensure_started(void)
{
    if (!s_started) {
        ESP_ERROR_CHECK(esp_wifi_start());     /* fires STA_START → connect */
        /* Modem-sleep (§7A step 2): park the radio between DTIM beacons so the STA
         * stays associated while the CPU auto light-sleeps. Only affects the STA;
         * the SoftAP portal is unaffected. */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
        s_started = true;
    }
}

static void retry_cb(void *arg)
{
    (void)arg;
    /* Don't fight the portal: while the AP is up (provisioning), a STA connect
     * attempt would starve /scan and destabilize the SoftAP — stay idle (§7A). */
    if (s_sta_on && !s_ap_on) {
        ESP_LOGI(TAG, "reconnecting…");
        esp_wifi_connect();
    }
}

static void schedule_retry(void)
{
    int shift = s_retry < 5 ? s_retry : 5;       /* 1,2,4,8,16,32 → cap 30s */
    int secs  = 1 << shift;
    if (secs > 30) {
        secs = 30;
    }
    s_retry++;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)secs * 1000000ULL);
    ESP_LOGI(TAG, "disconnected; retry in %ds", secs);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_sta_on && !s_ap_on) {
            esp_wifi_connect();              /* connect once the STA iface is up */
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_sta_on && !s_ap_on) {              /* not while we don't want STA / portal up */
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "disconnected (reason %d)", d ? d->reason : -1);
            net_status_set(NET_DISCONNECTED, 0);
            schedule_retry();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        int rssi = 0;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }
        net_status_set(NET_CONNECTED, rssi);
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR " (rssi %d)", IP2STR(&e->ip_info.ip), rssi);
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

    const esp_timer_create_args_t ta = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &s_retry_timer));

    s_inited = true;
    return ESP_OK;
}

esp_err_t wifi_mgr_start_sta(void)
{
    esp_err_t err = wifi_mgr_init();
    if (err != ESP_OK) {
        return err;
    }

    char ssid[33] = {0};
    char psk[65]  = {0};
    config_get_str("wifi_ssid", ssid, sizeof(ssid));
    config_get_str("wifi_psk",  psk,  sizeof(psk));
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no SSID configured — staying disconnected");
        net_status_set(NET_DISCONNECTED, 0);
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid,     ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, psk,  sizeof(wc.sta.password));
    wc.sta.threshold.authmode = psk[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    s_sta_on = true;
    s_retry  = 0;
    set_mode_for_wants();                          /* STA (or AP+STA) */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));   /* config before start */
    net_status_set(NET_CONNECTING, 0);
    bool was_started = s_started;
    ensure_started();                              /* new start → STA_START → connect */
    if (was_started) {
        esp_wifi_connect();                        /* already running → connect now */
    }
    ESP_LOGI(TAG, "STA starting, ssid '%s'", ssid);
    return ESP_OK;
}

void wifi_mgr_ap_start(const char *ssid)
{
    wifi_mgr_init();
    s_ap_on = true;
    set_mode_for_wants();                          /* → AP+STA */

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len       = strlen(ssid);
    ap.ap.channel        = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode       = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ensure_started();
    /* Halt any in-flight STA connect + its retry timer so the radio is free to host
     * the AP and run /scan (the STA iface stays up in AP+STA, just idle). */
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }
    esp_wifi_disconnect();                          /* harmless if not connected */
    ESP_LOGI(TAG, "AP up (ssid '%s', mode AP+STA, STA idle)", ssid);
}

void wifi_mgr_ap_stop(void)
{
    s_ap_on = false;
    set_mode_for_wants();                          /* → STA-only or idle */
    if (s_sta_on) {                                /* resume the STA link we paused */
        s_retry = 0;
        esp_wifi_connect();
    }
    ESP_LOGI(TAG, "AP down");
}

void wifi_mgr_refresh_status(void)
{
    wifi_ap_record_t ap;
    if (s_sta_on && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        net_status_set(NET_CONNECTED, ap.rssi);
    } else {
        net_status_set(NET_DISCONNECTED, 0);
    }
}

esp_err_t wifi_mgr_stop(void)
{
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }
    s_sta_on = false;
    s_ap_on  = false;
    set_mode_for_wants();                          /* → stop */
    net_status_set(NET_WIFI_OFF, 0);
    return ESP_OK;
}
