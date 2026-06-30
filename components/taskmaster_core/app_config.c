/*
 * app_config.c — registry of app-declared config groups (PLAN §9.4). See app_config.h.
 * A fixed array filled by boot constructors, mirroring app_manager.
 */
#include "app_config.h"

#define MAX_CFG_GROUPS 16

static const app_cfg_group_t *s_groups[MAX_CFG_GROUPS];
static unsigned               s_count;

void app_config_register(const app_cfg_group_t *group)
{
    if (group == NULL || s_count >= MAX_CFG_GROUPS) {
        return;
    }
    s_groups[s_count++] = group;
}

unsigned app_config_group_count(void)
{
    return s_count;
}

const app_cfg_group_t *app_config_group(unsigned i)
{
    return (i < s_count) ? s_groups[i] : NULL;
}
