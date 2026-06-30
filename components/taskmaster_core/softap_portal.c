#include "softap_portal.h"
#include "nvs_config.h"
#include "wifi_mgr.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "lwip/sockets.h"

static const char *TAG = "portal";

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

/* --- POST /save: parse the form, persist to NVS, then reboot to connect --- */
static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode(char *s)   /* in place: %XX → byte, '+' → space */
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2] && hexv(p[1]) >= 0 && hexv(p[2]) >= 0) {
            *o++ = (char)((hexv(p[1]) << 4) | hexv(p[2]));
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

static void reboot_cb(void *arg) { (void)arg; esp_restart(); }

static void schedule_reboot(void)
{
    static esp_timer_handle_t t;
    if (!t) {
        const esp_timer_create_args_t a = { .callback = reboot_cb, .name = "reboot" };
        esp_timer_create(&a, &t);
    }
    esp_timer_start_once(t, 1500000);   /* 1.5s — let the HTTP response flush first */
}

static esp_err_t save_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
    }
    char *body = malloc(total + 1);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
        }
        got += r;
    }
    body[got] = '\0';

    /* key=value&… — write each provisioning field straight to its NVS key. */
    int n_set = 0;
    char ssid[33] = {0};
    char *save = NULL;
    for (char *tok = strtok_r(body, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = tok, *val = eq + 1;
        url_decode(val);
        const cfg_field_t *f = config_find(key);
        if (f && f->write_path == CFG_WP_PROVISION) {
            config_set_str(key, val);
            n_set++;
            if (strcmp(key, "wifi_ssid") == 0) {
                strlcpy(ssid, val, sizeof(ssid));
            }
        }
    }
    free(body);

    if (ssid[0] == '\0') {   /* SSID is the one hard requirement */
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, "<h1>Wi-Fi SSID is required.</h1><p><a href=/>Back</a></p>");
    }

    config_set_provisioned(true);
    ESP_LOGI(TAG, "saved %d fields; provisioned; rebooting to join '%s'", n_set, ssid);

    char page[512];
    snprintf(page, sizeof(page),
        "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<body style=\"font-family:sans-serif;max-width:30em;margin:2em auto;padding:0 1em\">"
        "<h1>Saved &#10003;</h1><p>TaskMaster is restarting and connecting to <b>%s</b>. "
        "This setup network will turn off. If it reappears in ~30s, the connection failed "
        "&mdash; rejoin and re-check the password.</p></body>", ssid);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, page);

    schedule_reboot();   /* boot-mode branch then connects as a station (§7A.3) */
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
    /* /scan + /save before the GET wildcard so they aren't shadowed. */
    httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get };
    httpd_register_uri_handler(s_server, &scan);
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_register_uri_handler(s_server, &save);
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
    /* The radio + AP+STA mode is owned by wifi_mgr (so an existing STA link is kept
     * when re-provisioning from the Launcher, §7A step 7). The portal is just the
     * captive HTTP server + DNS on top. */
    wifi_mgr_ap_start(SOFTAP_SSID);
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
    wifi_mgr_ap_stop();         /* drop the AP, keep any STA link (back to STA-only) */
    ESP_LOGI(TAG, "portal stopped");
    return ESP_OK;
}
