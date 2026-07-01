#include "softap_portal.h"
#include "nvs_config.h"
#include "app_config.h"
#include "app_store.h"
#include "wifi_mgr.h"
#include "wx.h"

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

#define WIFI_SSID_BUF   33     /* 32-char SSID + NUL */
#define SAVE_BODY_MAX   4096   /* max POST /save body */
#define DNS_TASK_STACK  4096
#define DNS_TASK_PRIO   5

static httpd_handle_t   s_server;
static TaskHandle_t     s_dns_task;
static volatile bool    s_dns_run;

/* The form HTTP server is wanted by either the SoftAP setup portal (s_ap_up, with
 * captive DNS) OR the LAN config page (s_lan_web, on the station IP). One httpd on
 * :80 serves both — sync_httpd() starts/stops it from these two wants. */
static bool s_ap_up;
static bool s_lan_web;
static bool s_httpd_up;

/* --- Setup form: core (Wi-Fi/OTA) section + one section per installed app's
 * ACFG_PASTE fields (PLAN §7A.5/§9.4). Core fields are named by their NVS key;
 * app fields are named "cfg.<ns>.<key>" so POST /save routes them to app_store.
 * Secrets become password fields; the SSID field gets a /scan datalist. Served
 * for ANY path so captive probes land on it. --- */
static const char *INPUT_STYLE = "style=\"width:100%;box-sizing:border-box\"";

/* Escape a value for an HTML attribute (" & <). */
static void html_attr(const char *in, char *out, size_t out_len)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 7 < out_len; p++) {
        if (*p == '"')      { memcpy(out + o, "&quot;", 6); o += 6; }
        else if (*p == '&') { memcpy(out + o, "&amp;",  5); o += 5; }
        else if (*p == '<') { memcpy(out + o, "&lt;",   4); o += 4; }
        else                { out[o++] = *p; }
    }
    out[o] = '\0';
}

static esp_err_t form_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta name=viewport "
        "content=\"width=device-width,initial-scale=1\"><title>TaskMaster-C3 Setup</title></head>"
        "<body style=\"font-family:sans-serif;max-width:30em;margin:1.5em auto;padding:0 1em\">"
        "<h1>TaskMaster-C3</h1><p>Edit config, then Save. Blank secrets keep their value.</p>"
        "<form method=post action=/save>");

    char row[512];
    char esc[256];

    /* Core section: Wi-Fi + OTA (the only provisioning-path fields in nvs_config).
     * Non-secret fields are pre-filled with the current value; secrets stay blank. */
    for (unsigned i = 0; i < config_field_count(); i++) {
        const cfg_field_t *f = config_field(i);
        if (f->write_path != CFG_WP_PROVISION) {
            continue;
        }
        const char *type = f->secret ? "password" : "text";
        bool is_ssid = (strcmp(f->key, "wifi_ssid") == 0);
        const char *extra = is_ssid ? " list=aps id=ssid" : "";
        esc[0] = '\0';
        if (!f->secret) {
            char cur[128] = {0};
            config_get_str(f->key, cur, sizeof(cur));
            html_attr(cur, esc, sizeof(esc));
        }
        snprintf(row, sizeof(row),
            "<label>%s<br><input name=%s type=%s maxlength=%u value=\"%s\"%s%s %s></label><br><br>",
            f->label, f->key, type, f->max_len, esc, extra,
            f->secret ? " placeholder=\"(leave blank to keep)\"" : "", INPUT_STYLE);
        httpd_resp_sendstr_chunk(req, row);
        if (is_ssid) {
            /* A visible dropdown of scanned networks (the <datalist> above is
             * invisible on some desktop browsers, e.g. macOS). Picking one fills the
             * SSID field; the field stays editable for hidden/manual SSIDs. */
            httpd_resp_sendstr_chunk(req,
                "<label>Scanned networks (2.4GHz)<br>"
                "<select id=apsel style=\"width:100%;box-sizing:border-box\">"
                "<option value=\"\">&mdash; scanning&hellip; &mdash;</option>"
                "</select></label><br><br>");
        }
    }

    /* App sections: each installed app's ACFG_PASTE fields (core names no app). */
    for (unsigned g = 0; g < app_config_group_count(); g++) {
        const app_cfg_group_t *grp = app_config_group(g);
        bool any = false;
        for (unsigned k = 0; k < grp->count; k++) {
            if (grp->fields[k].input == ACFG_PASTE) { any = true; break; }
        }
        if (!any) {
            continue;
        }
        snprintf(row, sizeof(row), "<h3 style=\"margin:.5em 0\">%s</h3>", grp->name);
        httpd_resp_sendstr_chunk(req, row);
        for (unsigned k = 0; k < grp->count; k++) {
            const app_cfg_field_t *f = &grp->fields[k];
            if (f->input != ACFG_PASTE) {
                continue;
            }
            const char *type = f->secret ? "password" : "text";
            esc[0] = '\0';
            if (!f->secret) {
                char cur[160] = {0};
                app_store_t st;
                if (app_store_open(&st, grp->ns) == ESP_OK) {
                    app_store_get_str(&st, f->key, cur, sizeof(cur), "");
                    app_store_close(&st);
                }
                html_attr(cur, esc, sizeof(esc));
            }
            snprintf(row, sizeof(row),
                "<label>%s<br><input name=\"" APP_CFG_FORM_PREFIX "%s.%s\" type=%s maxlength=%u "
                "value=\"%s\"%s %s></label><br><br>",
                f->label, grp->ns, f->key, type, f->max_len, esc,
                f->secret ? " placeholder=\"(leave blank to keep)\"" : "", INPUT_STYLE);
            httpd_resp_sendstr_chunk(req, row);
        }
    }

    httpd_resp_sendstr_chunk(req,
        "<datalist id=aps></datalist>"
        "<button type=submit style=\"padding:.6em 1.2em\">Save &amp; Connect</button></form>"
        "<script>fetch('/scan').then(function(r){return r.json();}).then(function(a){"
        "var d=document.getElementById('aps');"
        "var sel=document.getElementById('apsel');"
        "if(sel){sel.innerHTML='';var def=document.createElement('option');def.value='';"
        "def.textContent=a.length?'\\u2014 pick a network \\u2014':'\\u2014 none found \\u2014';"
        "sel.appendChild(def);}"
        "a.forEach(function(s){"
        "if(d){var o=document.createElement('option');o.value=s;d.appendChild(o);}"
        "if(sel){var o2=document.createElement('option');o2.value=s;o2.textContent=s;sel.appendChild(o2);}"
        "});"
        "if(sel){sel.onchange=function(){var i=document.getElementById('ssid');if(i){i.value=sel.value;}};}"
        "}).catch(function(e){});</script>"
        "</body></html>");
    httpd_resp_sendstr_chunk(req, NULL);   /* end of chunks */
    return ESP_OK;
}

/* --- GET /scan: nearby APs as a JSON array of SSIDs (for the picker) --- */
static esp_err_t scan_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    wifi_scan_config_t sc = {0};
    esp_err_t serr = esp_wifi_scan_start(&sc, true);   /* blocking scan (APSTA) */
    if (serr != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(serr));
        return httpd_resp_sendstr(req, "[]");
    }
    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    ESP_LOGI(TAG, "scan: %u APs found", (unsigned)num);
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
    if (total <= 0 || total > SAVE_BODY_MAX) {
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

    /* First-provisioning (SoftAP) reboots to connect; an already-provisioned edit
     * (LAN config page) applies live — reboot only if Wi-Fi creds changed (§7A). */
    bool first = !config_is_provisioned();
    char old_ssid[WIFI_SSID_BUF] = {0};
    config_get_str("wifi_ssid", old_ssid, sizeof(old_ssid));
    bool wifi_changed = false;

    /* key=value&… — route each field: core keys → nvs_config; "cfg.<ns>.<key>"
     * (§9.4) → the app's app_store namespace. */
    int n_set = 0;
    char ssid[WIFI_SSID_BUF] = {0};
    char *save = NULL;
    const size_t prefix_len = strlen(APP_CFG_FORM_PREFIX);
    for (char *tok = strtok_r(body, "&", &save); tok; tok = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = tok, *val = eq + 1;
        url_decode(val);

        if (strncmp(key, APP_CFG_FORM_PREFIX, prefix_len) == 0) {
            /* App field "cfg.<ns>.<key>" → app_store(ns).set(key, val). Skip an
             * empty value so re-provisioning is additive (a blank field keeps the
             * previously-stored value instead of clearing it). */
            if (val[0] == '\0') {
                continue;
            }
            char *ns = key + prefix_len;
            char *dot = strchr(ns, '.');
            if (!dot) continue;
            *dot = '\0';
            char *akey = dot + 1;
            app_store_t st;
            if (app_store_open(&st, ns) == ESP_OK) {
                app_store_set_str(&st, akey, val);
                app_store_close(&st);
                n_set++;
            }
            continue;
        }

        const cfg_field_t *f = config_find(key);
        if (f && f->write_path == CFG_WP_PROVISION) {
            if (f->secret && val[0] == '\0') {
                continue;                      /* blank secret → keep the stored value */
            }
            config_set_str(key, val);
            n_set++;
            if (strcmp(key, "wifi_ssid") == 0) {
                strlcpy(ssid, val, sizeof(ssid));
                if (strcmp(val, old_ssid) != 0) wifi_changed = true;
            } else if (strcmp(key, "wifi_psk") == 0) {
                wifi_changed = true;           /* a new (non-empty) password was given */
            }
        }
    }
    free(body);

    if (ssid[0] == '\0') {   /* SSID is the one hard requirement */
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_sendstr(req, "<h1>Wi-Fi SSID is required.</h1><p><a href=/>Back</a></p>");
    }

    config_set_provisioned(true);
    char page[512];
    httpd_resp_set_type(req, "text/html");

    if (first || wifi_changed) {
        /* First setup, or Wi-Fi creds changed → reboot for a clean (re)connect. */
        ESP_LOGI(TAG, "saved %d fields; rebooting to join '%s'", n_set, ssid);
        snprintf(page, sizeof(page),
            "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<body style=\"font-family:sans-serif;max-width:30em;margin:2em auto;padding:0 1em\">"
            "<h1>Saved &#10003;</h1><p>TaskMaster is restarting and connecting to <b>%s</b>. "
            "If a setup network reappears in ~30s, the connection failed &mdash; rejoin and "
            "re-check the password.</p></body>", ssid);
        httpd_resp_sendstr(req, page);
        schedule_reboot();   /* boot-mode branch then connects as a station (§7A.3) */
    } else {
        /* LAN edit, no Wi-Fi change → apply live. The wx service re-reads the city on
         * its next poll; apps read their config on next open. No reboot. */
        ESP_LOGI(TAG, "saved %d fields (live edit, no reboot)", n_set);
        wx_refresh();       /* pick up a changed city / refresh weather now */
        httpd_resp_sendstr(req,
            "<!doctype html><meta name=viewport content=\"width=device-width,initial-scale=1\">"
            "<body style=\"font-family:sans-serif;max-width:30em;margin:2em auto;padding:0 1em\">"
            "<h1>Saved &#10003;</h1><p>Changes applied &mdash; no reboot needed.</p>"
            "<p><a href=/>Back</a></p></body>");
    }
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

/* Start/stop the single form httpd to match (s_ap_up || s_lan_web). */
static void sync_httpd(void)
{
    bool want = s_ap_up || s_lan_web;
    if (want && !s_httpd_up) {
        start_http_server();
        s_httpd_up = (s_server != NULL);
    } else if (!want && s_httpd_up) {
        httpd_stop(s_server);
        s_server = NULL;
        s_httpd_up = false;
    }
}

esp_err_t softap_portal_start(void)
{
    /* The radio + AP+STA mode is owned by wifi_mgr (so an existing STA link is kept
     * when re-provisioning from the Launcher, §7A step 7). The portal is just the
     * captive HTTP server + DNS on top. */
    wifi_mgr_ap_start(SOFTAP_SSID);
    ESP_LOGI(TAG, "SoftAP '%s' up (open), join then browse http://%s", SOFTAP_SSID, SOFTAP_IP);

    s_ap_up = true;
    sync_httpd();               /* the form server (shared with the LAN config page) */
    s_dns_run = true;
    xTaskCreate(dns_task, "dns", DNS_TASK_STACK, NULL, DNS_TASK_PRIO, &s_dns_task);
    return ESP_OK;
}

esp_err_t softap_portal_stop(void)
{
    s_dns_run = false;          /* dns_task closes its socket and self-deletes ≤0.5s */
    s_ap_up = false;
    sync_httpd();               /* stops the server unless the LAN config page holds it */
    wifi_mgr_ap_stop();         /* drop the AP, keep any STA link (back to STA-only) */
    ESP_LOGI(TAG, "portal stopped");
    return ESP_OK;
}

/* ── LAN config page: the same form served on the station IP (Settings toggle) ── */
esp_err_t config_web_start(void)
{
    s_lan_web = true;
    sync_httpd();
    ESP_LOGI(TAG, "LAN config web %s (browse the device IP shown in Device info)",
             s_httpd_up ? "up" : "FAILED");
    return s_httpd_up ? ESP_OK : ESP_FAIL;
}

void config_web_stop(void)
{
    s_lan_web = false;
    sync_httpd();
    ESP_LOGI(TAG, "LAN config web down");
}

bool config_web_active(void)
{
    return s_lan_web;
}
