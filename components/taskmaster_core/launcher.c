/*
 * launcher.c — the app list, built on the generic ui_list widget (PLAN §6.1, §8.5).
 *
 * Proves ui_list is task-agnostic: the Launcher is just a title + a ui_list of app
 * names + an inline connectivity line. The right hint bar is OS-drawn (ui_frame).
 */
#include "launcher.h"

#include "app_manager.h"
#include "net_status.h"
#include "ui_frame.h"
#include "ui_list.h"

#include <stdio.h>

/* Launcher control hints (right bar): rotate scrolls, click/Select opens. */
static const control_hints_t LAUNCHER_HINTS = { .rotate = "<>", .click = "OPN", .select = "OPN" };

#define LAUNCHER_TITLE_ROW 0
#define LIST_FIRST_ROW     1
#define LAUNCHER_NET_ROW   (UI_ROWS - 1)             /* inline net on the last row */
#define LIST_ROWS          (UI_ROWS - 2)             /* rows between title and net */
#define LAUNCHER_LINE_MAX  20  /* max chars per rendered line */
#define LAUNCHER_MAX_APPS  16  /* matches app_manager's registry cap */

static ui_list_t s_list;

/* Visible (available) app registry indices — apps whose available() returns false
 * (e.g. an unconfigured task source) are filtered out so they don't show/open. */
static int s_vis[LAUNCHER_MAX_APPS];
static int s_vis_n;

static void rebuild_visible(void)
{
    s_vis_n = 0;
    unsigned n = app_manager_count();
    for (unsigned i = 0; i < n && s_vis_n < LAUNCHER_MAX_APPS; i++) {
        const device_app_t *a = app_manager_get(i);
        if (a && (a->available == NULL || a->available())) {
            s_vis[s_vis_n++] = (int)i;
        }
    }
}

/* ui_list row text: the name of the visible app at list position `index`. */
static void launcher_row_text(int index, char *buf, int buf_sz, void *ctx)
{
    (void)ctx;
    const device_app_t *a = (index >= 0 && index < s_vis_n)
                          ? app_manager_get((unsigned)s_vis[index]) : NULL;
    snprintf(buf, buf_sz, "%s", (a && a->name) ? a->name : "?");
}

void launcher_render(void)
{
    rebuild_visible();                         /* an app may have just become (un)available */
    ui_list_set_count(&s_list, s_vis_n);

    lv_obj_clean(ui_frame_content());          /* blank slate, then rebuild */
    ui_frame_set_hints(&LAUNCHER_HINTS);       /* size content + show the bar FIRST */
    ui_text_row(LAUNCHER_TITLE_ROW, "TaskMaster");

    if (s_vis_n == 0) {
        ui_text_row(LIST_FIRST_ROW, " (no apps)");
    } else {
        ui_list_draw(&s_list, LIST_FIRST_ROW, launcher_row_text, NULL);
    }

    /* Inline connectivity indicator (no OS status bar, §6). No task/sync state —
     * tasks are userspace (§8); core shows only net status. */
    net_status_t ns;
    net_status_get(&ns);
    char bar[LAUNCHER_LINE_MAX];
    snprintf(bar, sizeof(bar), "net: %s", net_state_str(ns.state));
    ui_text_row(LAUNCHER_NET_ROW, bar);
}

void launcher_open(void)
{
    ui_list_init(&s_list, LIST_ROWS);
    rebuild_visible();
    ui_list_set_count(&s_list, s_vis_n);
    launcher_render();
}

int launcher_input(input_event_t ev)
{
    switch (ev) {
    case EV_ENCODER_CW:
        ui_list_move(&s_list, +1);
        launcher_render();
        break;
    case EV_ENCODER_CCW:
        ui_list_move(&s_list, -1);
        launcher_render();
        break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:
        if (s_vis_n > 0) {
            int sel = ui_list_sel(&s_list);       /* position in the visible list */
            if (sel >= 0 && sel < s_vis_n) {
                return s_vis[sel];                /* → real app registry index */
            }
        }
        break;
    default:
        break;   /* EV_HOME never arrives here (§5.2) */
    }
    return -1;
}
