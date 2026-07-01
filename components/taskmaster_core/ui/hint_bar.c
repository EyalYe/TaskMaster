/*
 * hint_bar.c — interim raw-sh1106 hint bar (PLAN §6). See hint_bar.h.
 *
 * Boxes (128×64): column x=107..127 (21px wide). Home y=1..16 (16px), Encoder
 * y=18..45 (28px, split at y=32 → two ~14px cells), Select y=47..62 (16px) — the
 * encoder box gets 2px from each neighbour so its two cells aren't cramped. 1px gaps.
 */
#include "hint_bar.h"
#include "sh1106.h"

#include <string.h>

#define X0 HINT_BAR_X        /* 108 */
#define X1 (OLED_W - 1)      /* 127 */

static void hline(int x0, int x1, int y) { for (int x = x0; x <= x1; x++) sh1106_pixel(x, y, 1); }
static void vline(int x, int y0, int y1) { for (int y = y0; y <= y1; y++) sh1106_pixel(x, y, 1); }

static void box(int y0, int y1)
{
    hline(X0, X1, y0);
    hline(X0, X1, y1);
    vline(X0, y0, y1);
    vline(X1, y0, y1);
}

/* A ≤3-char label, centered horizontally in the bar column and vertically in the
 * box/cell spanning y0..y1 (inclusive). Glyph is 5px wide (6px advance) × 7px tall. */
static void label(int y0, int y1, const char *s)
{
    if (!s || !*s) {
        return;
    }
    char buf[4];
    strncpy(buf, s, 3);
    buf[3] = '\0';
    int len   = (int)strlen(buf);
    int textw = len * 6 - 1;                       /* last glyph has no trailing space */
    int x     = X0 + (HINT_BAR_W - textw) / 2;
    int y     = y0 + ((y1 - y0 + 1) - 7) / 2;
    sh1106_text_at(x, y, buf);
}

void hint_bar_draw(const control_hints_t *h)
{
    /* Home — OS-fixed (box y1..16). */
    box(1, 16);
    label(1, 16, "HOM");

    /* Encoder — box y18..45, split at y32: rotate (top cell) over push (bottom cell). */
    box(18, 45);
    hline(X0, X1, 32);
    label(18, 31, (h && h->rotate) ? h->rotate : "<>");
    if (h && h->click) {
        label(33, 45, h->click);
    }

    /* Select — box y47..62. */
    box(47, 62);
    if (h && h->select) {
        label(47, 62, h->select);
    }
}
