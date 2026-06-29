/*
 * app_store.c — per-app private NVS storage. See app_store.h.
 *
 * Thin, safe wrapper over a per-app NVS namespace. The only platform rule is that
 * an app cannot open the core device-config namespace (so app data can never
 * clobber Wi-Fi creds / tokens). Otherwise each app's keys are fully its own.
 */
#include "app_store.h"

#include "nvs.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "app_store";

/* Reserved: the core device-config namespace (nvs_config.c). Off-limits to apps. */
#define CORE_NS "tmcfg"

esp_err_t app_store_open(app_store_t *st, const char *ns)
{
    if (st == NULL || ns == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = strlen(ns);
    if (n == 0 || n > 15) {                 /* NVS namespace length limit */
        ESP_LOGE(TAG, "bad namespace '%s' (must be 1..15 chars)", ns);
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(ns, CORE_NS) == 0) {
        ESP_LOGE(TAG, "namespace '%s' is reserved for device config", ns);
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open '%s' failed: %s", ns, esp_err_to_name(err));
        st->open = false;
        return err;
    }
    st->handle = (uint32_t)h;
    st->open   = true;
    return ESP_OK;
}

void app_store_close(app_store_t *st)
{
    if (st != NULL && st->open) {
        nvs_close((nvs_handle_t)st->handle);
        st->open = false;
    }
}

static esp_err_t check(const app_store_t *st, const char *key)
{
    if (st == NULL || !st->open || key == NULL || key[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t app_store_get_str(app_store_t *st, const char *key, char *out, size_t out_len, const char *def)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK || out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = out_len;
    err = nvs_get_str((nvs_handle_t)st->handle, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strlcpy(out, def ? def : "", out_len);
        return ESP_OK;
    }
    return err;
}

esp_err_t app_store_set_str(app_store_t *st, const char *key, const char *val)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK || val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_set_str((nvs_handle_t)st->handle, key, val);
    if (err == ESP_OK) {
        err = nvs_commit((nvs_handle_t)st->handle);
    }
    return err;
}

esp_err_t app_store_get_u32(app_store_t *st, const char *key, uint32_t *out, uint32_t def)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_get_u32((nvs_handle_t)st->handle, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = def;
        return ESP_OK;
    }
    return err;
}

esp_err_t app_store_set_u32(app_store_t *st, const char *key, uint32_t val)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32((nvs_handle_t)st->handle, key, val);
    if (err == ESP_OK) {
        err = nvs_commit((nvs_handle_t)st->handle);
    }
    return err;
}

esp_err_t app_store_get_blob(app_store_t *st, const char *key, void *out, size_t *len)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK || out == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_get_blob((nvs_handle_t)st->handle, key, out, len);
}

esp_err_t app_store_set_blob(app_store_t *st, const char *key, const void *val, size_t len)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK || val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = nvs_set_blob((nvs_handle_t)st->handle, key, val, len);
    if (err == ESP_OK) {
        err = nvs_commit((nvs_handle_t)st->handle);
    }
    return err;
}

esp_err_t app_store_erase_key(app_store_t *st, const char *key)
{
    esp_err_t err = check(st, key);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key((nvs_handle_t)st->handle, key);
    if (err == ESP_OK) {
        err = nvs_commit((nvs_handle_t)st->handle);
    }
    return err;
}

esp_err_t app_store_erase_all(app_store_t *st)
{
    if (st == NULL || !st->open) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nvs_erase_all((nvs_handle_t)st->handle);
    if (err == ESP_OK) {
        err = nvs_commit((nvs_handle_t)st->handle);
    }
    return err;
}
