/*
 * app.h — the public app interface (PLAN §6 / §6.1).
 *
 * An app is a self-contained component that defines a device_app_t and registers
 * itself with TASKMASTER_REGISTER_APP(). taskmaster_core never references an app by
 * name, so apps can live in separate repos and be added/removed via apps.yaml (the
 * app list) with no core edits.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ── App-API version (semver) ───────────────────────────────────────────────
 * The version of THIS contract (the public headers below + the sibling app_* / ui_*
 * / net_status / async_job headers). Apps are **statically linked** against these
 * headers, so compatibility is enforced at BUILD time — not runtime: an app states
 * the version it needs with TASKMASTER_REQUIRE_API() and the build fails with a clear
 * message if the core pinned in apps.yaml is incompatible (you can't even flash a
 * mismatched image). The running version is also logged at boot and shown in
 * Settings → Device info. Bump MAJOR for a breaking change, MINOR for a
 * backward-compatible addition. Core and apps thus version independently. */
#define TM_API_VERSION_MAJOR 1
#define TM_API_VERSION_MINOR 2

/* Drop this at file scope in your app to require an app-API version. Compatible =
 * same MAJOR and at least the requested MINOR. A mismatch is a compile error. */
#define TASKMASTER_REQUIRE_API(maj, min)                                            \
    _Static_assert((maj) == TM_API_VERSION_MAJOR && (min) <= TM_API_VERSION_MINOR,  \
        "This app requires a different TaskMaster app-API (need " #maj "." #min ") " \
        "than the core pinned in apps.yaml. Update the app, or pin a compatible core.")

typedef struct device_app {
    const char *name;                 /* shown in the Launcher */
    void (*init)(void);               /* allocate / reset state */
    void (*on_event)(uint8_t ev);     /* input event (Home is OS-reserved, never delivered) */
    void (*render)(void);             /* draw current state (LVGL in a later phase) */
    void (*exit)(void);               /* teardown / free */
    /* Optional: return false to hide this app from the Launcher until it's usable
     * (e.g. a task source with no URL/token configured). NULL = always shown.
     * Called from the Launcher while the app is NOT active, so read config directly
     * (e.g. app_store) — do not assume init() has run. */
    bool (*available)(void);
    /* Optional (app-API 1.1): if > 0, the OS re-renders this app every tick_ms
     * milliseconds while it is active — for clocks, timers, animations. 0 = render
     * only on input / status change (the default). The tick pauses while the panel is
     * blanked, so compute time-based state from a timestamp, not a tick counter. */
    uint32_t tick_ms;
} device_app_t;

/* Implemented by taskmaster_core; called by an app's registration constructor. */
void app_manager_register(const device_app_t *app);

/*
 * Self-registration: drop this in an app's source. A constructor (runs before
 * app_main) adds the app to the registry. The app's component must set
 * WHOLE_ARCHIVE (or a linker.lf KEEP) so the constructor survives --gc-sections.
 */
#define TASKMASTER_REGISTER_APP(app)                                       \
    static void __attribute__((constructor)) _tm_reg_##app(void)           \
    {                                                                      \
        app_manager_register(&(app));                                      \
    }
