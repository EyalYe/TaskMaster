/*
 * nvs_config.h — schema-driven device configuration in NVS (PLAN §7A.2 / §9.2).
 *
 * One declarative table is the single source of truth: it drives both the typed
 * NVS read/write below AND the generated provisioning form (§7A.5). Adding a config
 * field is one row in the schema (nvs_config.c) — no other code changes.
 *
 * Two write paths: the Setup/Wi-Fi app (provisioning fields) and the Settings app
 * (behavior fields); a few keys are system-internal. `get_*` return the schema
 * default when a key is unset, so callers never special-case "first boot".
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    CFG_STR,
    CFG_U8,
    CFG_U16,
} cfg_type_t;

typedef enum {
    CFG_WP_PROVISION,   /* set via the Setup/Wi-Fi form (§7) */
    CFG_WP_SETTINGS,    /* set via the Settings app (§8A)    */
    CFG_WP_SYSTEM,      /* internal (e.g. the provisioned flag) */
} cfg_write_path_t;

typedef struct {
    const char      *key;         /* NVS key (≤15 chars)                         */
    cfg_type_t       type;
    cfg_write_path_t write_path;
    bool             secret;      /* mask in the form + logs (PSK, tokens)       */
    uint16_t         max_len;     /* CFG_STR: max value length, excl NUL         */
    const char      *label;       /* human label for the form                    */
    uint32_t         def_num;     /* default for CFG_U8/U16                       */
    const char      *def_str;     /* default for CFG_STR (NULL → "")             */
} cfg_field_t;

/* Open the NVS namespace. Call once at boot, after nvs_flash_init(). */
esp_err_t config_init(void);

/* ── Schema introspection (drives the provisioning form, §7A.5) ── */
unsigned           config_field_count(void);
const cfg_field_t *config_field(unsigned i);
const cfg_field_t *config_find(const char *key);

/* ── Typed accessors. get_* fall back to the schema default when unset. ── */
esp_err_t config_get_str(const char *key, char *out, size_t out_len);
esp_err_t config_set_str(const char *key, const char *val);   /* enforces max_len */
esp_err_t config_get_u8 (const char *key, uint8_t  *out);
esp_err_t config_set_u8 (const char *key, uint8_t   val);
esp_err_t config_get_u16(const char *key, uint16_t *out);
esp_err_t config_set_u16(const char *key, uint16_t  val);

/* ── Convenience ── */
bool      config_is_provisioned(void);
esp_err_t config_set_provisioned(bool yes);
esp_err_t config_factory_reset(void);   /* erase all keys in the namespace */

/* Non-destructive round-trip self-test (saves/restores touched keys).
 * Returns true on pass; logs each step. Used for bring-up + the §6A test harness. */
bool      config_selftest(void);
