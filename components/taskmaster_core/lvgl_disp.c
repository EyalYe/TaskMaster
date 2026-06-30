/*
 * lvgl_disp.c — LVGL on the SH1106 via our sh1106 framebuffer. See lvgl_disp.h.
 */
#include "lvgl_disp.h"
#include "sh1106.h"

#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lvgl";

/* I1 draw buffer: 8-byte palette header + 1 bit/pixel for the whole panel. */
static uint8_t s_buf[8 + (OLED_W * OLED_H) / 8];

static uint32_t tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* LVGL renders 1-bit, row-major, MSB = leftmost pixel; first 8 bytes are the I1
 * palette (skip them). Convert per-pixel into the sh1106 (vertical-page) fb. */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    const uint8_t *bm = px_map + 8;            /* skip palette */
    const int stride = OLED_W / 8;             /* bytes per row (full-refresh buffer) */
    for (int y = 0; y < OLED_H; y++) {
        for (int x = 0; x < OLED_W; x++) {
            int on = (bm[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
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

void lvgl_disp_smoke(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Solid white box — isolates the bit/byte layout from font anti-aliasing.
     * Clean rectangle ⇒ layout is right; "dots" ⇒ the dithered AA font is the issue. */
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 48, 22);
    lv_obj_set_pos(box, 6, 6);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    /* Label below the box. */
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, "LVGL ok 123");      /* upper+lower+digits */
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t next = lv_timer_handler();
        if (next > 50) next = 50;
        vTaskDelay(pdMS_TO_TICKS(next < 5 ? 5 : next));
    }
}

void lvgl_disp_start(void)
{
    xTaskCreate(lvgl_task, "lvgl", 6144, NULL, 4, NULL);
}
