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

/* Reserved: the core device-config namespace (nvs_config.c), and the "tm" prefix that
 * owns it — off-limits to apps so an app can never collide with core/platform data
 * (Wi-Fi creds, tokens, settings), now or as core grows. */
#define CORE_NS        "tmcfg"
#define CORE_NS_PREFIX "tm"

/* Hash a too-long id into a deterministic ≤15-char NVS namespace: 64-bit FNV-1a
 * encoded base36. 2^64 in base36 needs 13 digits, so this always fits. */
static void hash_namespace(const char *id, char out[16])
{
    uint64_t h = 1469598103934665603ULL;          /* FNV-1a offset basis */
    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;                     /* FNV-1a prime */
    }
    static const char d[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[16];
    int i = 0;
    do {
        tmp[i++] = d[h % 36];
        h /= 36;
    } while (h > 0 && i < 15);
    int j = 0;
    while (i > 0) {
        out[j++] = tmp[--i];                       /* reverse */
    }
    out[j] = '\0';
}

esp_err_t app_store_open(app_store_t *st, const char *id)
{
    if (st == NULL || id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t n = strlen(id);
    if (n == 0) {
        ESP_LOGE(TAG, "empty namespace id");
        return ESP_ERR_INVALID_ARG;
    }

    /* ≤15 chars: use verbatim (human-readable). Longer: hash to a 15-char ns. */
    char ns[16];
    if (n <= 15) {
        memcpy(ns, id, n + 1);
        /* Reserve the "tm" prefix for core/platform namespaces. */
        if (strncmp(ns, CORE_NS_PREFIX, strlen(CORE_NS_PREFIX)) == 0) {
            ESP_LOGE(TAG, "namespace id '%s' uses the reserved '%s' prefix", ns, CORE_NS_PREFIX);
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        hash_namespace(id, ns);
        ESP_LOGD(TAG, "id '%s' -> hashed namespace '%s'", id, ns);
        /* A hashed id colliding with core config is astronomically unlikely, but reject
         * it rather than share Wi-Fi creds' namespace. */
        if (strcmp(ns, CORE_NS) == 0) {
            ESP_LOGE(TAG, "namespace '%s' collides with reserved device config", ns);
            return ESP_ERR_INVALID_ARG;
        }
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

/* Enforce the per-app entry budget (§6E step 3): once the namespace is full, reject a
 * NEW key — but let updates to an existing key through (no data loss, cache re-saves
 * keep working). Returns ESP_OK if the write may proceed. */
static esp_err_t budget_ok(nvs_handle_t h, const char *key)
{
    size_t used = 0;
    if (nvs_get_used_entry_count(h, &used) != ESP_OK) {
        return ESP_OK;                      /* can't measure → don't block */
    }
    if (used < APP_STORE_MAX_ENTRIES) {
        return ESP_OK;                      /* room to spare */
    }
    nvs_type_t type;
    if (nvs_find_key(h, key, &type) == ESP_OK) {
        return ESP_OK;                      /* existing key → an update is fine */
    }
    ESP_LOGW(TAG, "namespace at budget (%u entries) — new key '%s' rejected",
             (unsigned)used, key);
    return ESP_ERR_NO_MEM;
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
    if (strlen(val) >= APP_STORE_MAX_VALUE_BYTES) {
        return ESP_ERR_INVALID_SIZE;                 /* value too big (§6E step 3) */
    }
    if ((err = budget_ok((nvs_handle_t)st->handle, key)) != ESP_OK) {
        return err;
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
    if ((err = budget_ok((nvs_handle_t)st->handle, key)) != ESP_OK) {
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
    if (len > APP_STORE_MAX_VALUE_BYTES) {
        return ESP_ERR_INVALID_SIZE;                 /* value too big (§6E step 3) */
    }
    if ((err = budget_ok((nvs_handle_t)st->handle, key)) != ESP_OK) {
        return err;
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
