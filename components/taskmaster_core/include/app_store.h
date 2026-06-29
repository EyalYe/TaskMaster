/*
 * app_store.h — per-app persistent storage (PLAN §9.2 "App-owned storage").
 *
 * Lets an app create and persist its OWN variables with no core edits and no
 * collision with device config or other apps: each app opens its own private NVS
 * namespace, keyed by a stable id you choose. This is the app-developer counterpart
 * to nvs_config.h (which is core-owned device config that drives the setup form).
 *
 *     static app_store_t store;
 *     app_store_open(&store, "pomodoro");                  // your unique id (≤15 chars)
 *     uint32_t mins; app_store_get_u32(&store, "work", &mins, 25);   // default 25
 *     app_store_set_u32(&store, "work", 30);               // persisted immediately
 *
 * Open once in init(); the handle is cheap to keep. There is no schema and no form
 * integration — this is app-internal state. (User-facing settings that should appear
 * in the Settings UI are a separate, future facility; see PLAN §9.2.)
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t handle;   /* opaque (an nvs_handle_t) */
    bool     open;
} app_store_t;

/* Open your app's private namespace. `ns` must be a stable, unique id of 1..15
 * chars; the core namespace is reserved and rejected. Call once (e.g. in init()). */
esp_err_t app_store_open(app_store_t *st, const char *ns);
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
