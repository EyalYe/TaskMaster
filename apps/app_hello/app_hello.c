/*
 * app_hello — a minimal demo app that proves the manifest-driven, self-registering
 * app model (PLAN §6.1). It lives outside the core, depends only on the public app
 * API, and registers itself — taskmaster_core never references it. Comment this
 * component out of main/idf_component.yml and it vanishes from the build entirely.
 */
#include "app.h"
#include "esp_log.h"

static const char *TAG = "app.hello";

static void hello_init(void)             { ESP_LOGI(TAG, "init"); }
static void hello_on_event(uint8_t ev)   { ESP_LOGI(TAG, "event %u", ev); }
static void hello_render(void)           { /* drawn by the Launcher/UI in a later phase */ }
static void hello_exit(void)             { ESP_LOGI(TAG, "exit"); }

static const device_app_t hello_app = {
    .name     = "Hello",
    .init     = hello_init,
    .on_event = hello_on_event,
    .render   = hello_render,
    .exit     = hello_exit,
};

TASKMASTER_REGISTER_APP(hello_app);
