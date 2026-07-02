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

/* Geometry — shared by the LVGL OS frame (ui_frame.c). The column is HINT_BAR_W
 * wide at the right edge; three boxes (Home / Encoder / Select) with 1px gaps. */
#define HINT_BAR_X        107   /* left edge of the bar column */
#define HINT_BAR_W        21    /* column width */
#define CONTENT_W         107   /* usable content width when the bar is shown */

#define HINT_BOX_GAP      1     /* gap above/below/between boxes */
#define HINT_HOME_Y       1     /* Home box: top, height */
#define HINT_HOME_H       16
#define HINT_ENC_Y        18    /* Encoder box: top, height (split into two cells) */
#define HINT_ENC_H        28
#define HINT_ENC_SPLIT_DY 12    /* divider offset from the encoder box top */
#define HINT_SEL_Y        47    /* Select box: top, height */
#define HINT_SEL_H        16

#define HINT_LBL_DX       1     /* per-label horizontal nudge (right) */
#define HINT_GLYPH_DX     (HINT_LBL_DX - 1)  /* action glyphs sit 1px left of the text */
#define HINT_GLYPH_DY     1     /* ...and 1px lower */
#define HINT_ROT_DY       2     /* rotate label offset from cell top */
#define HINT_ROT_GLYPH_DY 0     /* rotate glyph offset from cell top (flush) */
#define HINT_CLICK_DY     2     /* click label offset from cell bottom (negated) */
