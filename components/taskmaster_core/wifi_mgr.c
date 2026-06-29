/*
 * wifi_mgr.c — Station-mode Wi-Fi link manager. See wifi_mgr.h.
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
static esp_timer_handle_t s_retry_timer;
static int               s_retry;

static void retry_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "reconnecting…");
    esp_wifi_connect();
}

static void schedule_retry(void)
{
    int shift = s_retry < 5 ? s_retry : 5;       /* 1,2,4,8,16,32 → cap 30s */
    int secs  = 1 << shift;
    if (secs > 30) {
        secs = 30;
    }
    s_retry++;
    esp_timer_stop(s_retry_timer);               /* harmless if not running */
    esp_timer_start_once(s_retry_timer, (uint64_t)secs * 1000000ULL);
    ESP_LOGI(TAG, "disconnected; retry in %ds", secs);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        net_status_set(NET_CONNECTING, 0);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        net_status_set(NET_DISCONNECTED, 0);
        schedule_retry();
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
        err = ESP_OK;                            /* already created elsewhere */
    }
    if (err != ESP_OK) {
        return err;
    }
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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    s_retry = 0;
    ESP_ERROR_CHECK(esp_wifi_start());           /* → STA_START → connect */
    ESP_LOGI(TAG, "STA starting, ssid '%s'", ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_stop(void)
{
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }
    esp_err_t err = s_inited ? esp_wifi_stop() : ESP_OK;
    net_status_set(NET_WIFI_OFF, 0);
    return err;
}
