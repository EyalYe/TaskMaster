/*
 * app_hello — a minimal demo app that proves the manifest-driven, self-registering
 * app model (PLAN §6.1) end to end: it self-registers, the Launcher lists it, the
 * UI task runs its lifecycle, it responds to input, and Home tears it down cleanly.
 * It lives outside the core, depends only on the public app API + display, and is
 * never referenced by taskmaster_core. Comment it out of main/idf_component.yml and
 * it vanishes from the build entirely.
 *
 * Holds no heap and (Phase 1, raw-rendered) no widgets, so exit() is trivially
 * total per §6A — there is nothing to free.
 */
#include "app.h"
#include "input.h"
#include "ui_frame.h"
#include "net_status.h"
#include "app_store.h"
#include "app_config.h"
#include "esp_log.h"

static const control_hints_t HELLO_HINTS = { .rotate = "+/-", .click = "RST", .select = "RST" };

/* Declared config (§9.4): a paste-only "name" the provisioning form will collect
 * into this app's app_store namespace ("hello"). Proves the facility end to end. */
#define HELLO_NAME_MAX 24
static const app_cfg_field_t HELLO_CFG[] = {
    { .key = "name", .label = "Your name", .type = ACFG_STR, .input = ACFG_PASTE, .max_len = HELLO_NAME_MAX },
};
TASKMASTER_REGISTER_APP_CONFIG("hello", "Hello", HELLO_CFG);

#include <stdio.h>

#define HELLO_TITLE_ROW   0
#define HELLO_PROMPT_ROW  2
#define HELLO_COUNT_ROW   3
#define HELLO_NET_ROW     5
#define HELLO_LINE_MAX    24

static const char *TAG = "app.hello";
static int          s_counter;
static app_store_t  s_store;   /* this app's own private NVS namespace */

static void hello_init(void)
{
    /* Load our own persisted variable — survives app-switch and reboot, with no
     * core edits and no collision with device config (app_store.h). */
    app_store_open(&s_store, "hello");
    uint32_t saved = 0;
    app_store_get_u32(&s_store, "count", &saved, 0);   /* default 0 on first run */
    s_counter = (int)saved;
    ESP_LOGI(TAG, "init (restored count=%d)", s_counter);
}

static void hello_on_event(uint8_t ev)
{
    switch (ev) {
    case EV_ENCODER_CW:  s_counter++; break;
    case EV_ENCODER_CCW: s_counter--; break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:      s_counter = 0; break;   /* push / Select resets */
    default: break;
    }
}

static void hello_render(void)
{
    char line[HELLO_LINE_MAX];
    lv_obj_clean(ui_frame_content());          /* blank slate, then rebuild */

    /* Greet using the name pasted via the provisioning form (§9.4), if set. */
    char name[HELLO_NAME_MAX + 1] = {0};
    char greet[sizeof("Hi, ") + HELLO_NAME_MAX];
    app_store_get_str(&s_store, "name", name, sizeof(name), "");
    if (name[0]) {
        snprintf(greet, sizeof(greet), "Hi, %s", name);
        ui_text_row(HELLO_TITLE_ROW, greet);
    } else {
        ui_text_row(HELLO_TITLE_ROW, "Hello app");
    }

    ui_text_row(HELLO_PROMPT_ROW, "Turn the knob:");
    snprintf(line, sizeof(line), " count = %d", s_counter);
    ui_text_row(HELLO_COUNT_ROW, line);

    /* Reading connectivity is one call — the platform re-renders us on change,
     * so this line stays current with no Wi-Fi event handling here (net_status.h). */
    net_status_t ns;
    net_status_get(&ns);
    snprintf(line, sizeof(line), "net: %s", net_state_str(ns.state));
    ui_text_row(HELLO_NET_ROW, line);

    ui_frame_set_hints(&HELLO_HINTS);          /* show + label the right bar (§6) */
}

static void hello_exit(void)
{
    /* Persist our variable on the way out, then release the handle. Idempotent/
     * total (§6A): no heap, no widgets — only our own NVS handle to close. */
    app_store_set_u32(&s_store, "count", (uint32_t)s_counter);
    app_store_close(&s_store);
    ESP_LOGI(TAG, "exit (saved count=%d)", s_counter);
}

static const device_app_t hello_app = {
    .name     = "Hello",
    .init     = hello_init,
    .on_event = hello_on_event,
    .render   = hello_render,
    .exit     = hello_exit,
};

TASKMASTER_REGISTER_APP(hello_app);
