/*
 * ui_list.c — generic scrollable/selectable list widget. See ui_list.h.
 */
#include "ui_list.h"
#include "lvgl.h"
#include "lv_font_tm_sans.h"

#include <string.h>

#define UI_LIST_TEXT_MAX 40   /* max chars a row callback may render */
#define UI_LIST_CURSOR    "> "   /* selected-row prefix */
#define UI_LIST_NOCURSOR  "  "   /* unselected: same width, keeps text aligned */

void ui_list_init(ui_list_t *l, int visible_rows)
{
    l->rows  = visible_rows > 0 ? visible_rows : UI_LIST_DEFAULT_ROWS;
    l->count = 0;
    l->sel   = 0;
    l->top   = 0;
}

/* Keep `sel` inside the visible window [top, top+rows). */
static void clamp_window(ui_list_t *l)
{
    if (l->sel < 0)            l->sel = 0;
    if (l->sel >= l->count)    l->sel = l->count > 0 ? l->count - 1 : 0;
    if (l->sel < l->top)              l->top = l->sel;
    if (l->sel >= l->top + l->rows)   l->top = l->sel - l->rows + 1;
    if (l->top < 0)            l->top = 0;
}

void ui_list_set_count(ui_list_t *l, int count)
{
    l->count = count < 0 ? 0 : count;
    clamp_window(l);
}

void ui_list_move(ui_list_t *l, int delta)
{
    if (l->count <= 0) {
        return;
    }
    l->sel = ((l->sel + delta) % l->count + l->count) % l->count;   /* wrap both ways */
    clamp_window(l);
}

int ui_list_sel(const ui_list_t *l)
{
    return l->sel;
}

void ui_list_draw(const ui_list_t *l, int y0_row, ui_list_text_fn fn, void *ctx)
{
    for (int r = 0; r < l->rows; r++) {
        int idx = l->top + r;
        if (idx >= l->count) {
            break;
        }
        char item[UI_LIST_TEXT_MAX] = {0};
        if (fn) {
            fn(idx, item, sizeof(item), ctx);
        }
        /* "> " cursor on the selected row; matching-width spaces otherwise so the
         * text stays aligned. */
        char line[sizeof(UI_LIST_CURSOR) + UI_LIST_TEXT_MAX];
        snprintf(line, sizeof(line), "%s%s",
                 idx == l->sel ? UI_LIST_CURSOR : UI_LIST_NOCURSOR, item);
        ui_text(0, (y0_row + r) * UI_ROW_H, line);
    }
}
