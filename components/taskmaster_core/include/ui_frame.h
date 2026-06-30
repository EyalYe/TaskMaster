/*
 * ui_frame.h — the OS-drawn LVGL frame (PLAN §6 / §8.5 Part A, step 2).
 *
 * Owns two things on the active LVGL screen:
 *   - a **content container** apps draw their widgets into, and
 *   - the optional **right hint bar** (21px column, 3 boxes: Home / encoder-split /
 *     Select) — the same geometry as the raw prototype, now as LVGL widgets.
 *
 * With the bar shown, the content container is 107×64; without it, 128×64. No
 * status bar (§6). Call ui_frame_init() once after lvgl_disp_init().
 */
#pragma once

#include "lvgl.h"
#include "hint_bar.h"        /* control_hints_t + bar geometry */
#include "lv_font_tm_sans.h" /* content font (TM_SANS_LINE_H) */
#include "sh1106.h"          /* OLED_H */

/* Content text layout (DejaVu Sans 11, proportional). */
#define UI_ROW_H        TM_SANS_LINE_H        /* baseline-to-baseline row height */
#define UI_ROWS         (OLED_H / UI_ROW_H)   /* rows that fit on the panel (0..UI_ROWS-1) */

void      ui_frame_init(void);                       /* build the frame on the active screen (once) */
lv_obj_t *ui_frame_content(void);                    /* the content container (parent for app widgets) */
void      ui_frame_set_hints(const control_hints_t *h); /* update/show the bar; NULL = hide (full width) */

/* Blank slate between apps (§6A): free the content's widget subtree in one call
 * and hide the hint bar (→ full-width content). Safe no-op before ui_frame_init().
 * Called by app_manager on every app switch. */
void      ui_frame_reset_content(void);

/* Convenience: a content text label (default UNSCII font) at pixel (x, y) in the
 * content area. Apps typically clear + rebuild these in render(). */
lv_obj_t *ui_text(int x, int y, const char *txt);

/* Same, positioned at text `row` (0..UI_ROWS-1), x=0 — apps avoid pixel math. */
lv_obj_t *ui_text_row(int row, const char *txt);

/* Like ui_text_row, but spans the content width and **auto-scrolls** (back/forth)
 * when the text is wider than the row — for long titles/SSIDs/URLs (§8.2). */
lv_obj_t *ui_text_row_scroll(int row, const char *txt);
