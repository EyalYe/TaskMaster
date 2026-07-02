/*
 * hint_bar.h — the contextual control-hint bar: struct + geometry (PLAN §6).
 *
 * A vertical 21px-wide column down the right edge with **three equal boxes** (1px
 * gaps) — one per button, each showing what that button does:
 *   top    = Home           (OS-fixed, the house glyph)
 *   middle = Encoder push    (the app's click action)
 *   bottom = Select          (the app's select action)
 * Encoder *rotation* isn't shown — it always scrolls, so it needs no hint. A NULL
 * click/select just leaves that box empty. Content area is the left 107×64.
 *
 * The bar is rendered as LVGL widgets by the OS frame (ui_frame.c), which maps a
 * label to a glyph where one exists (hint_glyphs.h) and keeps the text as fallback.
 * This header is just the shared control_hints_t + the box geometry.
 */
#pragma once

typedef struct {
    const char *rotate;   /* encoder rotate — always scrolls; not drawn (kept for compat) */
    const char *click;    /* encoder push  → middle box (NULL = empty) */
    const char *select;   /* Select button → bottom box (NULL = empty) */
} control_hints_t;

/* Geometry — shared by the LVGL OS frame (ui_frame.c). Three equal 20px boxes with
 * 1px gaps + 1px top/bottom margin exactly fill the 64px height. */
#define HINT_BAR_X        107   /* left edge of the bar column */
#define HINT_BAR_W        21    /* column width */
#define CONTENT_W         107   /* usable content width when the bar is shown */

#define HINT_BOX_GAP      1     /* gap between boxes / top+bottom margin */
#define HINT_BOX_H        20    /* each box height (3*20 + 4*1 = 64) */
#define HINT_HOME_Y       1                                   /* Home box top */
#define HINT_ENC_Y        (HINT_HOME_Y + HINT_BOX_H + HINT_BOX_GAP)   /* Encoder-push box top */
#define HINT_SEL_Y        (HINT_ENC_Y + HINT_BOX_H + HINT_BOX_GAP)    /* Select box top */
