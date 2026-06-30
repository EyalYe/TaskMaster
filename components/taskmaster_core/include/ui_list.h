/*
 * ui_list.h — a generic scrollable/selectable list widget (PLAN §8.5 step 7).
 *
 * TASK-AGNOSTIC: it knows nothing about tasks — it just renders N rows of text
 * with a moving selection and a scroll window, into the OS content area. The
 * Launcher, Settings, and app task lists all build on it. The caller owns the
 * items and supplies each row's text through a callback; ui_list owns only the
 * selection + scroll-window state.
 *
 *   static ui_list_t list;
 *   ui_list_init(&list, UI_LIST_DEFAULT_ROWS);
 *   ...
 *   ui_list_set_count(&list, n);              // when the item count changes
 *   ui_list_move(&list, +1);                  // on encoder CW (wraps)
 *   int sel = ui_list_sel(&list);             // act on the selected item
 *   ui_list_draw(&list, 0, row_text, ctx);    // in render(), after clearing content
 */
#pragma once

#include "ui_frame.h"   /* UI_ROWS, ui_frame_content, ui_text */

#define UI_LIST_DEFAULT_ROWS UI_ROWS   /* fill the content area by default */

/* Fill `buf` (≤ buf_sz) with the text for item `index`. */
typedef void (*ui_list_text_fn)(int index, char *buf, int buf_sz, void *ctx);

typedef struct {
    int rows;    /* visible rows */
    int count;   /* total items */
    int sel;     /* selected index (0..count-1) */
    int top;     /* index drawn on the first visible row (scroll window) */
} ui_list_t;

void ui_list_init(ui_list_t *l, int visible_rows);
void ui_list_set_count(ui_list_t *l, int count);   /* clamps sel into range */
void ui_list_move(ui_list_t *l, int delta);        /* move selection; wraps; keeps it visible */
int  ui_list_sel(const ui_list_t *l);

/* Draw the visible window into the content area, first row at text row `y0_row`.
 * The selected row is drawn as an inverted bar. Call after lv_obj_clean(content). */
void ui_list_draw(const ui_list_t *l, int y0_row, ui_list_text_fn fn, void *ctx);
