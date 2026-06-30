/*
 * lvgl_disp.c — LVGL on the SH1106 via our sh1106 framebuffer. See lvgl_disp.h.
 */
#include "lvgl_disp.h"
#include "sh1106.h"

#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "lvgl";

#define LV_I1_PALETTE_BYTES 8    /* LVGL prepends a 2-color palette to I1 buffers */
#define BITS_PER_BYTE       8

/* I1 draw buffer: palette header + 1 bit/pixel for the whole panel. */
static uint8_t s_buf[LV_I1_PALETTE_BYTES + (OLED_W * OLED_H) / BITS_PER_BYTE];

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* LVGL renders 1-bit, row-major, MSB = leftmost pixel; first 8 bytes are the I1
 * palette (skip them). Convert per-pixel into the sh1106 (vertical-page) fb. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    const uint8_t *bm = px_map + LV_I1_PALETTE_BYTES;     /* skip palette */
    const int stride = OLED_W / BITS_PER_BYTE;            /* bytes per row */
    for (int y = 0; y < OLED_H; y++) {
        for (int x = 0; x < OLED_W; x++) {
            int byte = bm[y * stride + (x / BITS_PER_BYTE)];
            int on   = (byte >> ((BITS_PER_BYTE - 1) - (x % BITS_PER_BYTE))) & 1;
            sh1106_pixel(x, y, on);
        }
    }
    sh1106_flush();
    lv_display_flush_ready(disp);
}

void lvgl_disp_init(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *d = lv_display_create(OLED_W, OLED_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_I1);
    lv_display_set_buffers(d, s_buf, NULL, sizeof(s_buf), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    ESP_LOGI(TAG, "LVGL %d.%d display up (%dx%d I1)", lv_version_major(),
             lv_version_minor(), OLED_W, OLED_H);
}

uint32_t lvgl_disp_tick(void)
{
    return lv_timer_handler();
}
