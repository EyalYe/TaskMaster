/*
 * lv_font_tm5x7.h — declares the thin 5x7 1-bit LVGL font (ASCII 0x20..0x5A,
 * uppercase). 5px wide / 6px advance, so 3 chars fit a 21px hint box, and it's
 * lighter than the built-in UNSCII. Use for the hint bar (UNSCII_8 stays the
 * default for content, which needs lowercase).
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(lv_font_tm5x7);
