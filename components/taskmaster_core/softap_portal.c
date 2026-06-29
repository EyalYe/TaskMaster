#include "softap_portal.h"
#include "nvs_config.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"

static const char *TAG = "portal";

static bool             s_inited;    /* stack/netif/wifi_init done once */
static httpd_handle_t   s_server;
static TaskHandle_t     s_dns_task;
static volatile bool    s_dns_run;

/* --- Setup form: generated from the config schema (PLAN §7A.5) --- */
/* One <input> per provisioning-path field; secrets become password fields; the
 * SSID field gets a datalist populated by /scan. Field names == NVS keys, so the
 * POST handler (step 6) maps name→key directly. Served for ANY path so captive
 * probes land on it. */
static esp_err_t form_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta name=viewport "
        "content=\"width=device-width,initial-scale=1\"><title>TaskMaster-C3 Setup</title></head>"
        "<body style=\"font-family:sans-serif;max-width:30em;margin:1.5em auto;padding:0 1em\">"
        "<h1>TaskMaster-C3</h1><p>Paste your config, then Save.</p>"
        "<form method=post action=/save>");

    char row[256];
    for (unsigned i = 0; i < config_field_count(); i++) {
        const cfg_field_t *f = config_field(i);
        if (f->write_path != CFG_WP_PROVISION) {
            continue;
        }
        const char *type = f->secret ? "password" : "text";
        const char *list = (strcmp(f->key, "wifi_ssid") == 0) ? " list=aps" : "";
        snprintf(row, sizeof(row),
            "<label>%s<br><input name=%s type=%s maxlength=%u%s "
            "style=\"width:100%%;box-sizing:border-box\"></label><br><br>",
            f->label, f->key, type, f->max_len, list);
        httpd_resp_sendstr_chunk(req, row);
    }

    httpd_resp_sendstr_chunk(req,
        "<datalist id=aps></datalist>"
        "<button type=submit style=\"padding:.6em 1.2em\">Save &amp; Connect</button></form>"
        "<script>fetch('/scan').then(r=>r.json()).then(a=>{var d=document.getElementById('aps');"
        "a.forEach(function(s){var o=document.createElement('option');o.value=s;d.appendChild(o);});})"
        ".catch(function(e){});</script>"
        "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);   /* end of chunks */
    return ESP_OK;
}

/* --- GET /scan: nearby APs as a JSON array of SSIDs (for the picker) --- */
static esp_err_t scan_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    wifi_scan_config_t sc = {0};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {    /* blocking scan (APSTA) */
        return httpd_resp_sendstr(req, "[]");
    }
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num > 20) {
        num = 20;
    }
    wifi_ap_record_t *recs = num ? calloc(num, sizeof(*recs)) : NULL;
    if (!recs) {
        return httpd_resp_sendstr(req, "[]");
    }
    esp_wifi_scan_get_ap_records(&num, recs);

    httpd_resp_sendstr_chunk(req, "[");
    for (uint16_t i = 0; i < num; i++) {
        /* minimal JSON-escape of the SSID (" and \) */
        char esc[2 * 33];
        size_t o = 0;
        for (const char *p = (const char *)recs[i].ssid; *p && o < sizeof(esc) - 2; p++) {
            if (*p == '"' || *p == '\\') {
                esc[o++] = '\\';
            }
            esc[o++] = *p;
        }
        esc[o] = '\0';
        if (esc[0] == '\0') {
            continue;                                   /* skip hidden SSIDs */
        }
        char item[80];
        snprintf(item, sizeof(item), "%s\"%s\"", i ? "," : "", esc);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);
    free(recs);
    return ESP_OK;
}

static void start_http_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        s_server = NULL;
        return;
    }
    /* /scan must be registered before the wildcard so it isn't shadowed. */
    httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get };
    httpd_register_uri_handler(s_server, &scan);
    httpd_uri_t any = { .uri = "/*", .method = HTTP_GET, .handler = form_get };
    httpd_register_uri_handler(s_server, &any);
    ESP_LOGI(TAG, "HTTP server up on http://%s", SOFTAP_IP);
}

/* --- Minimal captive-portal DNS: answer every A query with our AP IP --- */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket"); vTaskDelete(NULL); return; }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "dns bind"); close(sock); vTaskDelete(NULL); return;
    }

    /* Wake periodically so we can notice s_dns_run going false and exit cleanly. */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    uint32_t ap_ip = ipaddr_addr(SOFTAP_IP);   /* network byte order */

    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n < 12) continue;                   /* timeout or smaller than a DNS header */

        /* Turn the query into an answer in place. */
        buf[2] |= 0x80;                         /* QR = response */
        buf[3] |= 0x80;                         /* RA = recursion available */
        buf[7] = 1;                             /* ANCOUNT = 1 (QDCOUNT already 1) */

        if (n + 16 > (int)sizeof(buf)) continue;
        uint8_t *p = buf + n;
        *p++ = 0xC0; *p++ = 0x0C;               /* name pointer to the question */
        *p++ = 0x00; *p++ = 0x01;               /* type A */
        *p++ = 0x00; *p++ = 0x01;               /* class IN */
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C; /* TTL = 60s */
        *p++ = 0x00; *p++ = 0x04;               /* RDLENGTH = 4 */
        memcpy(p, &ap_ip, 4); p += 4;           /* RDATA = AP IP */

        sendto(sock, buf, p - buf, 0, (struct sockaddr *)&from, flen);
    }

    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t softap_portal_start(void)
{
    if (!s_inited) {
        ESP_ERROR_CHECK(esp_netif_init());
        esp_err_t e = esp_event_loop_create_default();
        if (e != ESP_ERR_INVALID_STATE) {       /* tolerate already-created */
            ESP_ERROR_CHECK(e);
        }
        esp_netif_create_default_wifi_ap();
        esp_netif_create_default_wifi_sta();   /* STA iface enables /scan + validate */
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        s_inited = true;
    }

    wifi_config_t ap = {
        .ap = {
            .ssid = SOFTAP_SSID,
            .ssid_len = strlen(SOFTAP_SSID),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    /* AP+STA: keep the AP up for the phone while the STA iface scans / validates. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP '%s' up (open), join then browse http://%s", SOFTAP_SSID, SOFTAP_IP);

    start_http_server();
    s_dns_run = true;
    xTaskCreate(dns_task, "dns", 4096, NULL, 5, &s_dns_task);
    return ESP_OK;
}

esp_err_t softap_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    s_dns_run = false;          /* dns_task closes its socket and self-deletes ≤0.5s */
    esp_wifi_stop();            /* AP down; stack left initialized for a restart */
    ESP_LOGI(TAG, "portal stopped");
    return ESP_OK;
}
