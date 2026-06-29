/*
 * app.h — the public app interface (PLAN §6 / §6.1).
 *
 * An app is a self-contained component that defines a device_app_t and registers
 * itself with TASKMASTER_REGISTER_APP(). taskmaster_core never references an app by
 * name, so apps can live in separate repos and be added/removed via the manifest
 * (main/idf_component.yml) with no core edits.
 */
#pragma once

#include <stdint.h>

typedef struct device_app {
    const char *name;                 /* shown in the Launcher */
    void (*init)(void);               /* allocate / reset state */
    void (*on_event)(uint8_t ev);     /* input event (Home is OS-reserved, never delivered) */
    void (*render)(void);             /* draw current state (LVGL in a later phase) */
    void (*exit)(void);               /* teardown / free */
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
