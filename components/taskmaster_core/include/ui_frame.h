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
#include "hint_bar.h"   /* control_hints_t */

void      ui_frame_init(void);                       /* build the frame on the active screen */
lv_obj_t *ui_frame_content(void);                    /* the content container (parent for app widgets) */
void      ui_frame_set_hints(const control_hints_t *h); /* update/show the bar; NULL = hide (full width) */
