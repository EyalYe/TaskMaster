/*
 * app_config.h — app-declared configuration (PLAN §9.4).
 *
 * Lets an app declare the config it needs (a source URL, a token, a preference)
 * so the OS assembles the provisioning form + Settings from Wi-Fi/OTA (core) plus
 * each installed app's fields — core never names a specific app. Values live in
 * the app's own app_store namespace (the app reads them with app_store_get_*).
 *
 * Two input methods (respects "no typing on the knob", §7):
 *   ACFG_PASTE — strings/secrets (URLs, tokens): filled via the paste-from-phone
 *                form only.
 *   ACFG_KNOB  — scalars (bool/number): knob-editable in Settings, with bounds.
 *
 * Declare at boot, mirroring app registration (the component must use
 * WHOLE_ARCHIVE so the constructor survives --gc-sections):
 *
 *   static const app_cfg_field_t MYAPP_CFG[] = {
 *       { .key="url",   .label="Server URL", .type=ACFG_STR, .input=ACFG_PASTE, .max_len=96  },
 *       { .key="token", .label="Token",      .type=ACFG_STR, .input=ACFG_PASTE, .secret=true, .max_len=128 },
 *   };
 *   TASKMASTER_REGISTER_APP_CONFIG("myapp", "MyApp", MYAPP_CFG);
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum { ACFG_STR, ACFG_U8, ACFG_U16, ACFG_BOOL } acfg_type_t;
typedef enum { ACFG_PASTE, ACFG_KNOB }                  acfg_input_t;

typedef struct {
    const char  *key;        /* key within the app's app_store namespace (≤15 chars) */
    const char  *label;      /* shown in the form / Settings */
    acfg_type_t  type;
    acfg_input_t input;      /* ACFG_PASTE (form) or ACFG_KNOB (Settings) */
    bool         secret;     /* mask in the form + logs */
    uint16_t     max_len;    /* ACFG_STR */
    int32_t      min;        /* ACFG_KNOB scalar bounds */
    int32_t      max;
} app_cfg_field_t;

typedef struct {
    const char            *ns;     /* app_store namespace these fields live in */
    const char            *name;   /* display name (form section / Settings group) */
    const app_cfg_field_t *fields;
    unsigned               count;
} app_cfg_group_t;

/* Form field-name prefix: inputs are named "<APP_CFG_FORM_PREFIX><ns>.<key>".
 * `ns` and `key` must not contain '.'. */
#define APP_CFG_FORM_PREFIX "cfg."

/* Called by the registration constructor; not for direct use. */
void app_config_register(const app_cfg_group_t *group);

/* Iterate the registered groups (form + Settings). */
unsigned               app_config_group_count(void);
const app_cfg_group_t *app_config_group(unsigned i);

#define TASKMASTER_REGISTER_APP_CONFIG(ns_, name_, fields_)                  \
    static const app_cfg_group_t _tm_cfg_##fields_ = {                       \
        .ns = (ns_), .name = (name_), .fields = (fields_),                   \
        .count = sizeof(fields_) / sizeof((fields_)[0]),                     \
    };                                                                       \
    static void __attribute__((constructor)) _tm_cfgreg_##fields_(void)      \
    {                                                                        \
        app_config_register(&_tm_cfg_##fields_);                             \
    }
