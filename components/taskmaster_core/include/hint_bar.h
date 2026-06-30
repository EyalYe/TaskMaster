/*
 * hint_bar.h — the contextual control hint bar (PLAN §6).
 *
 * A vertical 21px-wide column down the right edge with three boxes (1px gaps):
 *   top    = Home    (OS-fixed)            — 16px tall
 *   middle = Encoder — rotate (top cell)   — 28px tall (extra height so its two
 *            + push/click (bottom cell)      cells aren't cramped)
 *   bottom = Select                        — 16px tall
 * Apps declare only the three app-usable controls; Home is OS-fixed. An app "uses"
 * the hint bar by drawing it (NULL fields hide a cell). Content area is the left
 * 108×64 when the bar is shown.
 *
 * NOTE: this is the interim raw-`sh1106` renderer; in Phase 3 Part A (§8.5) the bar
 * is re-implemented as LVGL widgets — this struct + geometry carry over.
 */
#pragma once

typedef struct {
    const char *rotate;   /* encoder rotate → encoder box top cell    (NULL = ↻ default) */
    const char *click;    /* encoder push   → encoder box bottom cell (NULL = hide)       */
    const char *select;   /* Select button  → bottom box              (NULL = hide)       */
} control_hints_t;

/* Geometry (for apps that constrain content to the left of the bar). */
#define HINT_BAR_X      107   /* left edge of the bar column (x 107..127 = 21px) */
#define HINT_BAR_W      21
#define CONTENT_W       107   /* usable width when the bar is shown (0..106) */

/* Draw the hint bar into the sh1106 framebuffer (call before sh1106_flush). */
void hint_bar_draw(const control_hints_t *h);
