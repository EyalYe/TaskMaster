/*
 * wx.c — weather + time service. See wx.h.
 *
 * A single background task polls: once online with a `city` set, geocode it (keyless
 * Open-Meteo) → lat/lon, then a forecast call → current temp + WMO code +
 * utc_offset_seconds. NTP (esp_sntp) supplies UTC; local = UTC + offset. Both HTTPS
 * calls reuse esp_http_client + the cert bundle; JSON parsed with cJSON. The task
 * owns the fetch (off the UI task, §6A.2); getters just read the last snapshot.
 */
#include "wx.h"

#include "net_status.h"
#include "nvs_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "wx";

#define WX_CITY_MAX      48          /* matches the `city` config field */
#define WX_BODY_MAX      4096        /* Open-Meteo responses are small */
#define WX_URL_MAX       256
#define WX_HTTP_TMO_MS   10000
#define WX_TASK_STACK    6144
#define WX_TASK_PRIO     4
#define WX_REFRESH_MS    (15 * 60 * 1000)  /* between successful weather refreshes */
#define WX_RETRY_MS      (30 * 1000)       /* when offline / a fetch failed / no city */
#define WX_EPOCH_VALID   1700000000        /* time(NULL) past this ⇒ NTP has synced */
#define WX_NTP_SERVER    "pool.ntp.org"

static char   s_city[WX_CITY_MAX];   /* the city we geocoded (task-only) */
static double s_lat, s_lon;          /* geocoded coords (task-only) */
static bool   s_have_loc;
static int    s_offset;              /* utc_offset_seconds (read by UI) */
static int    s_temp;                /* °C, rounded (read by UI) */
static int    s_code;                /* WMO weather code (read by UI) */
static bool   s_have_wx;             /* a forecast has been fetched (read by UI) */
static TaskHandle_t s_task;          /* the fetch task (for wx_refresh wakeups) */

/* Minimal percent-encode for a query value (city names have spaces/accents). */
static void url_encode(const char *in, char *out, size_t out_len)
{
    static const char *hex = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < out_len; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0x0F];
        }
    }
    out[o] = '\0';
}

/* Format a coordinate with 4 decimals without relying on %f (newlib-nano safe). */
static void fmt_coord(double v, char *out, size_t out_len)
{
    int neg = v < 0;
    if (neg) v = -v;
    long ip = (long)v;
    long fp = (long)((v - (double)ip) * 10000 + 0.5);
    if (fp >= 10000) { ip++; fp -= 10000; }
    snprintf(out, out_len, "%s%ld.%04ld", neg ? "-" : "", ip, fp);
}

/* Blocking HTTPS GET → malloc'd NUL-terminated body (caller frees), or NULL. */
static char *http_get(const char *url)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = WX_HTTP_TMO_MS,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char *body = malloc(WX_BODY_MAX);
    int total = 0;
    if (body && esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        int r;
        while (total < WX_BODY_MAX - 1 &&
               (r = esp_http_client_read(c, body + total, WX_BODY_MAX - 1 - total)) > 0) {
            total += r;
        }
        body[total] = '\0';
        esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);
    if (total <= 0) {
        free(body);
        return NULL;
    }
    return body;
}

/* city → lat/lon via Open-Meteo geocoding (keyless). */
static bool geocode(const char *city)
{
    char enc[WX_CITY_MAX * 3];
    url_encode(city, enc, sizeof(enc));
    char url[WX_URL_MAX];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1", enc);
    char *body = http_get(url);
    if (!body) return false;

    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *res = cJSON_GetObjectItem(root, "results");
    cJSON *r0  = cJSON_IsArray(res) ? cJSON_GetArrayItem(res, 0) : NULL;
    bool ok = false;
    if (r0) {
        cJSON *lat = cJSON_GetObjectItem(r0, "latitude");
        cJSON *lon = cJSON_GetObjectItem(r0, "longitude");
        if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
            s_lat = lat->valuedouble;
            s_lon = lon->valuedouble;
            s_have_loc = true;
            ok = true;
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "geocode '%s': %s (%.4f, %.4f)", city, ok ? "ok" : "FAIL", s_lat, s_lon);
    return ok;
}

/* lat/lon → current weather + tz offset via Open-Meteo forecast (keyless). */
static bool fetch_weather(void)
{
    char lat[16], lon[16];
    fmt_coord(s_lat, lat, sizeof(lat));
    fmt_coord(s_lon, lon, sizeof(lon));
    char url[WX_URL_MAX];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,weather_code&timezone=auto", lat, lon);
    char *body = http_get(url);
    if (!body) return false;

    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    cJSON *off = cJSON_GetObjectItem(root, "utc_offset_seconds");
    bool ok = false;
    if (cJSON_IsObject(cur)) {
        cJSON *t  = cJSON_GetObjectItem(cur, "temperature_2m");
        cJSON *wc = cJSON_GetObjectItem(cur, "weather_code");
        if (cJSON_IsNumber(t)) {
            double v = t->valuedouble;
            s_temp = (int)(v < 0 ? v - 0.5 : v + 0.5);
        }
        if (cJSON_IsNumber(wc)) s_code = wc->valueint;
        if (cJSON_IsNumber(off)) s_offset = off->valueint;
        s_have_wx = true;
        ok = true;
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "weather: %s %d C, code %d, offset %ds", ok ? "ok" : "FAIL",
             s_temp, s_code, s_offset);
    return ok;
}

static void wx_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t wait_ms = WX_RETRY_MS;
        char city[WX_CITY_MAX] = {0};
        config_get_str("city", city, sizeof(city));

        if (net_is_online() && city[0]) {
            if (strcmp(city, s_city) != 0) {           /* city changed → re-geocode */
                strlcpy(s_city, city, sizeof(s_city));
                s_have_loc = false;
            }
            bool ok = true;
            if (!s_have_loc) ok = geocode(city);
            if (s_have_loc)  ok = fetch_weather();
            if (ok && s_have_wx) {
                char hhmm[8];
                if (wx_time_str(hhmm, sizeof(hhmm))) ESP_LOGI(TAG, "local time %s", hhmm);
                net_status_notify();                   /* re-render the status bar now */
                wait_ms = WX_REFRESH_MS;               /* success → long refresh */
            }
        }
        /* Sleep until the next poll — but wake early if wx_refresh() pokes us
         * (e.g. the city was just changed via the config page). */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

void wx_init(void)
{
    char city[WX_CITY_MAX] = {0};
    config_get_str("city", city, sizeof(city));
    ESP_LOGI(TAG, "init: city='%s' (empty = set it in Setup)", city);

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, WX_NTP_SERVER);
    esp_sntp_init();
    xTaskCreate(wx_task, "wx", WX_TASK_STACK, NULL, WX_TASK_PRIO, &s_task);
}

void wx_refresh(void)
{
    if (s_task) xTaskNotifyGive(s_task);   /* wake the fetch task now (e.g. city changed) */
}

bool wx_time_str(char *out, size_t out_len)
{
    time_t now = time(NULL);
    if (now < WX_EPOCH_VALID || !s_have_wx) {
        return false;                 /* NTP not synced yet, or no tz offset */
    }
    time_t local = now + s_offset;
    struct tm tm;
    gmtime_r(&local, &tm);
    snprintf(out, out_len, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return true;
}

bool wx_weather(int *temp_c, int *weather_code)
{
    if (!s_have_wx) return false;
    if (temp_c)      *temp_c = s_temp;
    if (weather_code) *weather_code = s_code;
    return true;
}

/* WMO weather-code → short word (open-meteo.com/en/docs, WW interpretation). */
const char *wx_weather_desc(int c)
{
    if (c == 0)                return "Clear";
    if (c == 1 || c == 2)      return "Cloudy";
    if (c == 3)                return "Overcast";
    if (c == 45 || c == 48)    return "Fog";
    if (c >= 51 && c <= 57)    return "Drizzle";
    if (c >= 61 && c <= 67)    return "Rain";
    if (c >= 71 && c <= 77)    return "Snow";
    if (c >= 80 && c <= 82)    return "Showers";
    if (c >= 85 && c <= 86)    return "Snow";
    if (c >= 95)               return "Storm";
    return "?";
}
