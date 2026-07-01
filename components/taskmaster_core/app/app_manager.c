#include "app_manager.h"
#include "ui_frame.h"

#define MAX_APPS 16

static const device_app_t *s_apps[MAX_APPS];
static unsigned s_count;
static const device_app_t *s_active;   /* NULL = Launcher / no app */

void app_manager_register(const device_app_t *app)
{
    if (app == NULL || s_count >= MAX_APPS) {
        return;
    }
    s_apps[s_count++] = app;
}

unsigned app_manager_count(void)
{
    return s_count;
}

const device_app_t *app_manager_get(unsigned i)
{
    return (i < s_count) ? s_apps[i] : NULL;
}

const device_app_t *app_manager_switch_to(int index)
{
    /* Total, idempotent teardown of the outgoing app first (PLAN §6A). */
    if (s_active != NULL && s_active->exit != NULL) {
        s_active->exit();
    }
    s_active = NULL;

    /* Free the outgoing app's widget tree + reset the frame to a blank slate, so
     * the incoming init() builds onto a clean content area (the §6A "owned-by-
     * screen" guarantee, via lv_obj_clean of the content container). */
    ui_frame_reset_content();

    if (index >= 0 && (unsigned)index < s_count) {
        s_active = s_apps[index];
        if (s_active->init != NULL) {
            s_active->init();
        }
    }
    return s_active;
}

const device_app_t *app_manager_active(void)
{
    return s_active;
}

int app_manager_index_of(const device_app_t *app)
{
    for (unsigned i = 0; i < s_count; i++) {
        if (s_apps[i] == app) {
            return (int)i;
        }
    }
    return -1;
}

void app_manager_dispatch(uint8_t ev)
{
    if (s_active != NULL && s_active->on_event != NULL) {
        s_active->on_event(ev);
    }
}
