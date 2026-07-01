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
#define UI_LIST_SCROLL_PAD "   " /* trailing pad so a scrolling row clears the edge */

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

void ui_list_set_rows(ui_list_t *l, int visible_rows)
{
    l->rows = visible_rows > 0 ? visible_rows : UI_LIST_DEFAULT_ROWS;
    clamp_window(l);   /* keep the selection inside the resized window */
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
         * text stays aligned. The selected (scrolling) row gets trailing pad so it
         * scrolls a touch further left before stopping. */
        char line[sizeof(UI_LIST_CURSOR) + UI_LIST_TEXT_MAX + sizeof(UI_LIST_SCROLL_PAD)];
        snprintf(line, sizeof(line), "%s%s%s",
                 idx == l->sel ? UI_LIST_CURSOR : UI_LIST_NOCURSOR, item,
                 idx == l->sel ? UI_LIST_SCROLL_PAD : "");
        lv_obj_t *row = ui_text(0, (y0_row + r) * UI_ROW_H, line);
        /* Pin to one row (full content width × one line height) so a long row
         * can't run under the hint bar OR wrap into the next row. The selected
         * row auto-scrolls; the others get an ellipsis. */
        lv_obj_set_size(row, lv_obj_get_width(ui_frame_content()), UI_ROW_H);
        lv_label_set_long_mode(row,
            idx == l->sel ? LV_LABEL_LONG_SCROLL : LV_LABEL_LONG_DOT);
    }
}
