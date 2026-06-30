/*
 * hint_bar.c — interim raw-sh1106 hint bar (PLAN §6). See hint_bar.h.
 *
 * Boxes (128×64): column x=108..127; Home y=1..20, Encoder y=22..41 (split at
 * y=31), Select y=43..62 — 1px gaps top/bottom/between.
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

/* A ≤3-char label, left-padded a little into the column, at text row `row`. */
static void label(int row, const char *s)
{
    if (!s || !*s) {
        return;
    }
    char buf[4];
    strncpy(buf, s, 3);
    buf[3] = '\0';
    sh1106_text(X0 + 2, row, buf);   /* x=110; 3×6px = 18px fits in the 20px box */
}

void hint_bar_draw(const control_hints_t *h)
{
    /* Home — OS-fixed (box y1..20, label ~row 1 = y8..15). */
    box(1, 20);
    label(1, "HOM");

    /* Encoder — box y22..41, split at y31: rotate (row 3 ≈ y24..31), push (row 4 ≈ y32..39). */
    box(22, 41);
    hline(X0, X1, 31);
    label(3, (h && h->rotate) ? h->rotate : "<>");
    if (h && h->click) {
        label(4, h->click);
    }

    /* Select — box y43..62, label ~row 6 = y48..55. */
    box(43, 62);
    if (h && h->select) {
        label(6, h->select);
    }
}
