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

/* ── namespace registry ─────────────────────────────────────────────────────
 * Every namespace app_store creates is recorded (a blob in the core-config
 * namespace), so its data can be listed + deleted even after the app that made it is
 * uninstalled. Kept in core config because it must outlive any single app. */
#define REG_KEY     "app_ns"   /* registry blob key in CORE_NS */
#define REG_NS_LEN  16         /* per entry: 15-char namespace + NUL */
#define REG_MAX     24         /* max tracked namespaces */

static int reg_load(char *buf)   /* buf holds REG_MAX*REG_NS_LEN; returns the count */
{
    nvs_handle_t h;
    if (nvs_open(CORE_NS, NVS_READWRITE, &h) != ESP_OK) return 0;
    size_t len = (size_t)REG_MAX * REG_NS_LEN;
    memset(buf, 0, len);
    esp_err_t e = nvs_get_blob(h, REG_KEY, buf, &len);
    nvs_close(h);
    return (e == ESP_OK) ? (int)(len / REG_NS_LEN) : 0;
}

static void reg_save(const char *buf, int count)
{
    nvs_handle_t h;
    if (nvs_open(CORE_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (count <= 0) nvs_erase_key(h, REG_KEY);
    else            nvs_set_blob(h, REG_KEY, buf, (size_t)count * REG_NS_LEN);
    nvs_commit(h);
    nvs_close(h);
}

static void reg_add(const char *ns)   /* record `ns` if not already known */
{
    char buf[REG_MAX * REG_NS_LEN];
    int n = reg_load(buf);
    for (int i = 0; i < n; i++) {
        if (strcmp(buf + i * REG_NS_LEN, ns) == 0) return;
    }
    if (n >= REG_MAX) return;
    strlcpy(buf + n * REG_NS_LEN, ns, REG_NS_LEN);
    reg_save(buf, n + 1);
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
    reg_add(ns);                 /* remember this namespace for later listing/deletion */
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

void app_store_seed_registry(void)
{
    /* Pick up namespaces that already hold data but were created before the registry
     * existed (e.g. an app since uninstalled). App namespaces are the ones that are
     * neither core (the reserved "tm" prefix) nor system (esp_wifi's dotted names). */
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        const char *ns = info.namespace_name;
        if (strncmp(ns, CORE_NS_PREFIX, strlen(CORE_NS_PREFIX)) != 0 && strchr(ns, '.') == NULL) {
            reg_add(ns);
        }
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
}

int app_store_ns_count(void)
{
    char buf[REG_MAX * REG_NS_LEN];
    return reg_load(buf);
}

esp_err_t app_store_ns_get(int idx, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) return ESP_ERR_INVALID_ARG;
    char buf[REG_MAX * REG_NS_LEN];
    int n = reg_load(buf);
    if (idx < 0 || idx >= n) return ESP_ERR_INVALID_ARG;
    strlcpy(out, buf + idx * REG_NS_LEN, out_len);
    return ESP_OK;
}

esp_err_t app_store_erase_ns(const char *ns)
{
    if (ns == NULL || ns[0] == '\0') return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;              /* wipe the data directly (not via app_store_open, which
                                   would just re-register the namespace) */
    if (nvs_open(ns, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }

    char buf[REG_MAX * REG_NS_LEN];    /* drop it from the registry */
    int n = reg_load(buf), w = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(buf + i * REG_NS_LEN, ns) != 0) {
            if (w != i) memcpy(buf + w * REG_NS_LEN, buf + i * REG_NS_LEN, REG_NS_LEN);
            w++;
        }
    }
    reg_save(buf, w);
    return ESP_OK;
}
