/*
 * launcher.c — LVGL app list (PLAN §6.1). See launcher.h.
 *
 * Rebuilds the content area each render: title, a scrolling app list with a '>'
 * cursor, and an inline net/sync line. The right hint bar is OS-drawn (ui_frame).
 */
#include "launcher.h"

#include "app_manager.h"
#include "net_status.h"
#include "ui_frame.h"

#include <stdio.h>

/* Launcher control hints (right bar): rotate scrolls, click/Select opens. */
static const control_hints_t LAUNCHER_HINTS = { .rotate = "<>", .click = "OPN", .select = "OPN" };

#define LAUNCHER_TITLE_ROW 0
#define LIST_FIRST_ROW     1
#define LAUNCHER_NET_ROW   (UI_ROWS - 1)             /* inline net/sync on the last row */
#define LIST_ROWS          (UI_ROWS - 2)             /* rows between title and net */
#define LAUNCHER_EMPTY_ROW 2
#define LAUNCHER_LINE_MAX  20  /* max chars per rendered line */

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

    lv_obj_clean(ui_frame_content());          /* blank slate, then rebuild */
    ui_text_row(LAUNCHER_TITLE_ROW, "TaskMaster");

    if (count == 0) {
        ui_text_row(LAUNCHER_EMPTY_ROW, " (no apps)");
    } else {
        for (int r = 0; r < LIST_ROWS; r++) {
            int idx = s_top + r;
            if (idx >= count) {
                break;
            }
            const device_app_t *a = app_manager_get((unsigned)idx);
            char line[LAUNCHER_LINE_MAX];
            snprintf(line, sizeof(line), "%c %s",
                     idx == s_sel ? '>' : ' ',
                     (a && a->name) ? a->name : "?");
            ui_text_row(LIST_FIRST_ROW + r, line);
        }
    }

    /* Inline connectivity indicator (no OS status bar, §6). No task/sync state —
     * tasks are userspace (§8); core shows only net status. */
    net_status_t ns;
    net_status_get(&ns);
    char bar[LAUNCHER_LINE_MAX];
    snprintf(bar, sizeof(bar), "net: %s", net_state_str(ns.state));
    ui_text_row(LAUNCHER_NET_ROW, bar);

    ui_frame_set_hints(&LAUNCHER_HINTS);       /* show + label the right bar (§6) */
}

void launcher_open(void)
{
    s_sel = 0;
    s_top = 0;
    launcher_render();
}

int launcher_input(input_event_t ev)
{
    int count = (int)app_manager_count();
    switch (ev) {
    case EV_ENCODER_CW:
        if (count > 0) {
            s_sel = (s_sel + 1) % count;            /* wrap past the end → top */
            clamp_window();
            launcher_render();
        }
        break;
    case EV_ENCODER_CCW:
        if (count > 0) {
            s_sel = (s_sel - 1 + count) % count;    /* wrap past the top → end */
            clamp_window();
            launcher_render();
        }
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
