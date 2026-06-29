/*
 * sh1106.h — tiny self-contained SH1106 (128x64 mono OLED) driver over the
 * ESP-IDF i2c_master API. Phase-0 bring-up: framebuffer + 5x7 text + flush.
 * The SH1106's RAM is 132 columns wide, so the visible 128 are written with a
 * 2-pixel column offset (PLAN §5.1 — the #1 "blank screen" cause).
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

#define OLED_W 128
#define OLED_H 64

/* Bring up I2C + the panel. Returns ESP_OK on success. */
esp_err_t sh1106_init(void);

/* Framebuffer ops (call sh1106_flush() to push to the panel). */
void sh1106_clear(void);
void sh1106_pixel(int x, int y, int on);

/* Draw an (auto-uppercased) string at pixel column x, text row `row` (0..7).
 * Each glyph is 6px wide (5 + 1 spacing). */
void sh1106_text(int x, int row, const char *s);

/* Clear one 8px text row and draw `s` into it (handy for live status lines). */
void sh1106_text_line(int row, const char *s);

/* Push the framebuffer to the display. */
esp_err_t sh1106_flush(void);
