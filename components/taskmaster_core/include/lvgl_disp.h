/*
 * lvgl_disp.h — LVGL display bring-up (PLAN §8.5 Part A, step 1).
 *
 * Drives LVGL onto the SH1106 through our existing sh1106 framebuffer (no esp_lcd
 * panel): a 1-bit (I1) display whose flush callback writes into sh1106 and pushes
 * the frame. Call lvgl_disp_init() once after sh1106_init(), then lvgl_disp_start()
 * to run the LVGL handler task. LVGL is single-threaded — only the LVGL task (and
 * setup before it starts) may touch lv_* APIs.
 */
#pragma once

void lvgl_disp_init(void);    /* lv_init + 1-bit display + tick (after sh1106_init) */
void lvgl_disp_start(void);   /* start the lv_timer_handler task */
void lvgl_disp_smoke(void);   /* step-1 smoke test: a centered label */
