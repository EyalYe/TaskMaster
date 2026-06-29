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
#include "sh1106.h"
#include "net_status.h"
#include "app_store.h"
#include "esp_log.h"

#include <stdio.h>

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
    char line[24];
    sh1106_clear();
    sh1106_text_line(0, "HELLO APP");
    sh1106_text_line(2, "TURN THE KNOB:");
    snprintf(line, sizeof(line), "  COUNT = %d", s_counter);
    sh1106_text_line(3, line);

    /* Reading connectivity is one call — the platform re-renders us on change,
     * so this line stays current with no Wi-Fi event handling here (net_status.h). */
    net_status_t ns;
    net_status_get(&ns);
    snprintf(line, sizeof(line), "NET: %s", net_state_str(ns.state));
    sh1106_text_line(5, line);

    sh1106_text_line(6, "PUSH/SEL: RESET");
    sh1106_text_line(7, "HOME: BACK");
    sh1106_flush();
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
