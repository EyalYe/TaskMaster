#include "app_manager.h"

#define MAX_APPS 16

static const device_app_t *s_apps[MAX_APPS];
static unsigned s_count;

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
