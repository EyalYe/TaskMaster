/*
 * app_store.h — per-app persistent storage (PLAN §9.2 "App-owned storage").
 *
 * Lets an app create and persist its OWN variables with no core edits and no
 * collision with device config or other apps: each app opens its own private NVS
 * namespace, keyed by a stable id you choose. This is the app-developer counterpart
 * to nvs_config.h (which is core-owned device config that drives the setup form).
 *
 *     static app_store_t store;
 *     app_store_open(&store, "pomodoro");                  // your id (any length)
 *     uint32_t mins; app_store_get_u32(&store, "work", &mins, 25);   // default 25
 *     app_store_set_u32(&store, "work", 30);               // persisted immediately
 *
 * Open once in init(); the handle is cheap to keep. There is no schema and no form
 * integration — this is app-internal state. (User-facing settings that should appear
 * in the Settings UI are a separate, future facility; see PLAN §9.3.)
 *
 * Namespace ids: NVS caps a namespace at 15 chars. Ids of 1..15 chars are used
 * verbatim (human-readable); longer ids (e.g. a repo slug) are hashed down to a
 * 15-char namespace automatically — so you can pass any length. Keys are always
 * limited to 15 chars by NVS. The core reserves the "tm" prefix (it owns the tmcfg
 * device-config namespace) — pick an id that does not start with "tm".
 *
 * Collision note: distinct ids almost never map to the same namespace, but it is
 * *theoretically* possible — a literal short id could equal another id's hash, or
 * two long ids could hash alike (a 64-bit hash; astronomically unlikely). If it ever
 * happened, the two apps would share one namespace and could read/overwrite each
 * other's keys. Pick a distinctive id to make this a non-issue.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Per-app budget (PLAN §6E step 3) ───────────────────────────────────────
 * All apps + device config share one ~24 KB NVS pool, so an app can't be allowed to
 * grow without bound and crowd out other apps (or provisioning). Two limits, enforced
 * by set_*:
 *   - a single value may be at most APP_STORE_MAX_VALUE_BYTES;
 *   - once a namespace reaches APP_STORE_MAX_ENTRIES used NVS entries, **new** keys are
 *     rejected (ESP_ERR_NO_MEM) — but **updates to existing keys still succeed**, so an
 *     app that re-saves the same keys (e.g. a cache blob) never breaks and never loses
 *     data. To free space, erase keys.
 * Generous enough for real apps (a 50-item task cache ≈ 6 KB fits); tight enough that a
 * runaway is bounded to roughly a third of the pool. */
#define APP_STORE_MAX_ENTRIES     320     /* used NVS entries per namespace (~10 KB) */
#define APP_STORE_MAX_VALUE_BYTES 8192    /* max bytes in one str/blob value */

typedef struct {
    uint32_t handle;   /* opaque (an nvs_handle_t) */
    bool     open;
} app_store_t;

/* Open your app's private namespace. `id` is a stable, unique id of any length
 * (1..15 chars used verbatim; longer ids are hashed to a 15-char namespace). The
 * core namespace is reserved and rejected. Call once (e.g. in init()). */
esp_err_t app_store_open(app_store_t *st, const char *id);
void      app_store_close(app_store_t *st);

/* Typed get/set. set_* persist immediately. get_* return `def` when the key is
 * unset, so you never special-case "first run". */
esp_err_t app_store_get_str (app_store_t *st, const char *key, char *out, size_t out_len, const char *def);
esp_err_t app_store_set_str (app_store_t *st, const char *key, const char *val);
esp_err_t app_store_get_u32 (app_store_t *st, const char *key, uint32_t *out, uint32_t def);
esp_err_t app_store_set_u32 (app_store_t *st, const char *key, uint32_t val);
esp_err_t app_store_get_blob(app_store_t *st, const char *key, void *out, size_t *len);
esp_err_t app_store_set_blob(app_store_t *st, const char *key, const void *val, size_t len);

/* Housekeeping for your namespace. */
esp_err_t app_store_erase_key(app_store_t *st, const char *key);
esp_err_t app_store_erase_all(app_store_t *st);

/* ── namespace registry (core use) ──────────────────────────────────────────
 * app_store records every namespace it creates, so stored data can be listed and
 * deleted even after the app that created it is uninstalled (Settings → Delete data).
 * Not for app use. */
void      app_store_seed_registry(void);                   /* add existing NVS app namespaces */
int       app_store_ns_count(void);                        /* number of known namespaces */
esp_err_t app_store_ns_get(int idx, char *out, size_t out_len);  /* the idx-th namespace */
esp_err_t app_store_erase_ns(const char *ns);              /* wipe its data + forget it */
