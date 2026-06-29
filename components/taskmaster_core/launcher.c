/*
 * launcher.c — raw-rendered app list (PLAN §6.1). See launcher.h.
 *
 * Layout (8 text rows on the 128x64 panel):
 *   row 0      title
 *   rows 1..5  app list with a '>' cursor (scrolls when count > 5)
 *   row 6      status bar (Wi-Fi + sync, read from the shared model)
 *   row 7      contextual control hint (CrossPoint-style, §6)
 */
#include "launcher.h"

#include "app_manager.h"
#include "task_model.h"
#include "net_status.h"
#include "sh1106.h"

#include <stdio.h>

#define LIST_FIRST_ROW 1
#define LIST_ROWS      5   /* rows 1..5 show apps */

static int s_sel;   /* selected app index           */
static int s_top;   /* app index at the first list row (scroll window) */

static void clamp_window(void)
{
    int count = (int)app_manager_count();
    if (s_sel < 0)      s_sel = 0;
    if (s_sel >= count) s_sel = count > 0 ? count - 1 : 0;
    if (s_sel < s_top)              s_top = s_sel;
    if (s_sel >= s_top + LIST_ROWS) s_top = s_sel - LIST_ROWS + 1;
    if (s_top < 0)      s_top = 0;
}

void launcher_render(void)
{
    int count = (int)app_manager_count();

    sh1106_clear();
    sh1106_text_line(0, "TASKMASTER");

    if (count == 0) {
        sh1106_text_line(2, "  (NO APPS)");
    } else {
        for (int r = 0; r < LIST_ROWS; r++) {
            int idx = s_top + r;
            if (idx >= count) {
                break;
            }
            const device_app_t *a = app_manager_get((unsigned)idx);
            char line[24];
            snprintf(line, sizeof(line), "%c %s",
                     idx == s_sel ? '>' : ' ',
                     (a && a->name) ? a->name : "?");
            sh1106_text_line(LIST_FIRST_ROW + r, line);
        }
    }

    net_status_t ns;
    net_status_get(&ns);
    task_status_t st;
    task_model_get(&st);
    char bar[24];
    snprintf(bar, sizeof(bar), "NET:%s SYNC:%s",
             net_state_str(ns.state), sync_state_label(st.sync));
    sh1106_text_line(6, bar);

    sh1106_text_line(7, "TURN:MOVE PUSH:OPEN");
    sh1106_flush();
}

void launcher_open(void)
{
    s_sel = 0;
    s_top = 0;
    launcher_render();
}

int launcher_input(input_event_t ev)
{
    switch (ev) {
    case EV_ENCODER_CW:
        s_sel++;
        clamp_window();
        launcher_render();
        break;
    case EV_ENCODER_CCW:
        s_sel--;
        clamp_window();
        launcher_render();
        break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:
        if (app_manager_count() > 0) {
            return s_sel;
        }
        break;
    default:
        break;   /* EV_HOME never arrives here (§5.2) */
    }
    return -1;
}
