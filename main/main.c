/*
 * main.c — firmware entry point.
 *
 * Intentionally trivial: all bootstrap lives in the sealed core (taskmaster_run()),
 * so a project built on TaskMaster-C3 never contains or edits any OS logic — it just
 * declares its apps (apps.yaml) and starts the OS here (PLAN §6D/§6E).
 */
#include "taskmaster.h"

void app_main(void)
{
    taskmaster_run();
}
