/*
 * nvs_config.c — schema-driven NVS configuration (PLAN §7A.2 / §9.2). See nvs_config.h.
 */
#include "nvs_config.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "config";

#define CONFIG_NS "tmcfg"

/* The schema — single source of truth (mirrors §9.2). Order = form field order. */
static const cfg_field_t s_schema[] = {
    /* key            type      write-path         secret  max  label                  defN  defS */
    { "wifi_ssid",   CFG_STR,  CFG_WP_PROVISION,  false,   32, "Wi-Fi SSID",            0, ""    },
    { "wifi_psk",    CFG_STR,  CFG_WP_PROVISION,  true,    64, "Wi-Fi password",        0, ""    },
    { "yapp_url",    CFG_STR,  CFG_WP_PROVISION,  false,   96, "Yapp source URL",       0, ""    },
    { "yapp_token",  CFG_STR,  CFG_WP_PROVISION,  true,   128, "Yapp token",            0, ""    },
    { "local_url",   CFG_STR,  CFG_WP_PROVISION,  false,   96, "Local source URL",      0, ""    },
    { "local_token", CFG_STR,  CFG_WP_PROVISION,  true,   128, "Local token",           0, ""    },
    { "fw_url",      CFG_STR,  CFG_WP_PROVISION,  false,   96, "OTA firmware URL",      0, ""    },
    { "startup_tgt", CFG_STR,  CFG_WP_SETTINGS,   false,   16, "Startup target",        0, ""    },
    { "wifi_en",     CFG_U8,   CFG_WP_SETTINGS,   false,    0, "Wi-Fi enabled",         1, NULL  },
    { "deep_sleep",  CFG_U8,   CFG_WP_SETTINGS,   false,    0, "Deep sleep",            0, NULL  },
    { "idle_to_s",   CFG_U16,  CFG_WP_SETTINGS,   false,    0, "Inactivity timeout s",  0, NULL  },
    { "provisioned", CFG_U8,   CFG_WP_SYSTEM,     false,    0, "Provisioned flag",      0, NULL  },
};
#define SCHEMA_COUNT (sizeof(s_schema) / sizeof(s_schema[0]))

static nvs_handle_t s_nvs;
static bool         s_open;

esp_err_t config_init(void)
{
    esp_err_t err = nvs_open(CONFIG_NS, NVS_READWRITE, &s_nvs);
    if (err == ESP_OK) {
        s_open = true;
    } else {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", CONFIG_NS, esp_err_to_name(err));
    }
    return err;
}

unsigned config_field_count(void)
{
    return SCHEMA_COUNT;
}

const cfg_field_t *config_field(unsigned i)
{
    return (i < SCHEMA_COUNT) ? &s_schema[i] : NULL;
}

const cfg_field_t *config_find(const char *key)
{
    if (key == NULL) {
        return NULL;
    }
    for (unsigned i = 0; i < SCHEMA_COUNT; i++) {
        if (strcmp(s_schema[i].key, key) == 0) {
            return &s_schema[i];
        }
    }
    return NULL;
}

esp_err_t config_get_str(const char *key, char *out, size_t out_len)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_STR || out == NULL || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = out_len;
    esp_err_t err = nvs_get_str(s_nvs, key, out, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        const char *def = f->def_str ? f->def_str : "";
        strlcpy(out, def, out_len);
        return ESP_OK;
    }
    return err;
}

esp_err_t config_set_str(const char *key, const char *val)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_STR || val == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    if (strlen(val) > f->max_len) {
        ESP_LOGW(TAG, "set %s rejected: %u > max %u", key,
                 (unsigned)strlen(val), f->max_len);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = nvs_set_str(s_nvs, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t config_get_u8(const char *key, uint8_t *out)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_U8 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_get_u8(s_nvs, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = (uint8_t)f->def_num;
        return ESP_OK;
    }
    return err;
}

esp_err_t config_set_u8(const char *key, uint8_t val)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_U8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u8(s_nvs, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

esp_err_t config_get_u16(const char *key, uint16_t *out)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_U16 || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_get_u16(s_nvs, key, out);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = (uint16_t)f->def_num;
        return ESP_OK;
    }
    return err;
}

esp_err_t config_set_u16(const char *key, uint16_t val)
{
    const cfg_field_t *f = config_find(key);
    if (f == NULL || f->type != CFG_U16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_set_u16(s_nvs, key, val);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

bool config_is_provisioned(void)
{
    uint8_t v = 0;
    config_get_u8("provisioned", &v);
    return v != 0;
}

esp_err_t config_set_provisioned(bool yes)
{
    return config_set_u8("provisioned", yes ? 1 : 0);
}

esp_err_t config_factory_reset(void)
{
    if (!s_open) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_erase_all(s_nvs);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    ESP_LOGW(TAG, "factory reset: %s", esp_err_to_name(err));
    return err;
}

bool config_selftest(void)
{
    bool ok = true;

    /* Save originals for the two keys we exercise, restore at the end. */
    char saved_url[97];
    uint16_t saved_to = 0;
    config_get_str("yapp_url", saved_url, sizeof(saved_url));
    config_get_u16("idle_to_s", &saved_to);

    /* 1) string round-trip */
    const char *probe = "http://selftest:8000";
    char back[97] = {0};
    ok &= (config_set_str("yapp_url", probe) == ESP_OK);
    ok &= (config_get_str("yapp_url", back, sizeof(back)) == ESP_OK);
    ok &= (strcmp(back, probe) == 0);

    /* 2) numeric round-trip */
    uint16_t n = 0;
    ok &= (config_set_u16("idle_to_s", 1234) == ESP_OK);
    ok &= (config_get_u16("idle_to_s", &n) == ESP_OK);
    ok &= (n == 1234);

    /* 3) max-len enforcement (wifi_ssid max = 32) */
    char toolong[40];
    memset(toolong, 'x', sizeof(toolong));
    toolong[39] = '\0';
    ok &= (config_set_str("wifi_ssid", toolong) == ESP_ERR_INVALID_SIZE);

    /* 4) default fallback for an unset key */
    uint8_t en = 0;
    ok &= (config_get_u8("wifi_en", &en) == ESP_OK);   /* default 1 if never set */

    /* restore */
    config_set_str("yapp_url", saved_url);
    config_set_u16("idle_to_s", saved_to);

    ESP_LOGI(TAG, "selftest: %s", ok ? "PASS" : "FAIL");
    return ok;
}
