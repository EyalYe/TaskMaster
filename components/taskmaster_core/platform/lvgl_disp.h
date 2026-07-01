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

#include <stdint.h>

void lvgl_disp_init(void);    /* lv_init + 1-bit display + tick (after sh1106_init) */

/* Pump LVGL once; returns ms until the next due timer. Called from the UI task
 * loop — LVGL is single-threaded, so only that task touches lv_* (no mutex). */
uint32_t lvgl_disp_tick(void);
