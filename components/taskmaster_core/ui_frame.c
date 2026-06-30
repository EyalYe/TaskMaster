/*
 * ui_frame.c — OS LVGL frame: content container + right hint bar. See ui_frame.h.
 *
 * Geometry mirrors the raw prototype (hint_bar.h): bar column x=107, 21px wide;
 * Home y1..16 (16px), Encoder y18..45 (28px, split), Select y47..62 (16px).
 */
#include "ui_frame.h"
#include "sh1106.h"          /* OLED_W / OLED_H */
#include "lv_font_tm5x7.h"   /* thin 5x7 font for the hint labels */

static bool      s_inited;
static lv_obj_t *s_content;
static lv_obj_t *s_bar;          /* transparent full-screen layer holding the boxes */
static lv_obj_t *s_rotate_lbl;
static lv_obj_t *s_click_lbl;
static lv_obj_t *s_select_lbl;

static lv_obj_t *make_box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_radius(b, 0, 0);
    return b;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &lv_font_tm5x7, 0);    /* thin, fits the box */
    lv_label_set_text(l, txt ? txt : "");
    return l;
}

void ui_frame_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Content container — apps add widgets here. Full width until the bar shows. */
    s_content = lv_obj_create(scr);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, OLED_W, OLED_H);

    /* Hint-bar layer (transparent; boxes positioned absolutely). */
    s_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_size(s_bar, OLED_W, OLED_H);

    /* Home (OS-fixed). */
    lv_obj_t *home = make_box(s_bar, HINT_BAR_X, HINT_HOME_Y, HINT_BAR_W, HINT_HOME_H);
    lv_obj_align(make_label(home, "HOM"), LV_ALIGN_CENTER, HINT_LBL_DX, 0);

    /* Encoder — split into rotate (top) / click (bottom) by a divider at mid.
     * Nudge the two labels toward the divider (rotate down, click up). */
    lv_obj_t *enc = make_box(s_bar, HINT_BAR_X, HINT_ENC_Y, HINT_BAR_W, HINT_ENC_H);
    s_rotate_lbl = make_label(enc, "<>");
    lv_obj_align(s_rotate_lbl, LV_ALIGN_TOP_MID, HINT_LBL_DX, HINT_ROT_DY);
    lv_obj_t *divln = lv_obj_create(enc);
    lv_obj_remove_style_all(divln);
    lv_obj_set_size(divln, HINT_BAR_W - 2 * HINT_BOX_GAP, HINT_BOX_GAP);
    lv_obj_align(divln, LV_ALIGN_TOP_MID, 0, HINT_ENC_SPLIT_DY);
    lv_obj_set_style_bg_color(divln, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(divln, LV_OPA_COVER, 0);
    s_click_lbl = make_label(enc, "");
    lv_obj_align(s_click_lbl, LV_ALIGN_BOTTOM_MID, HINT_LBL_DX, -HINT_CLICK_DY);

    /* Select. */
    lv_obj_t *sel = make_box(s_bar, HINT_BAR_X, HINT_SEL_Y, HINT_BAR_W, HINT_SEL_H);
    s_select_lbl = make_label(sel, "");
    lv_obj_align(s_select_lbl, LV_ALIGN_CENTER, HINT_LBL_DX, 0);

    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);   /* hidden until an app sets hints */
    s_inited = true;
}

lv_obj_t *ui_frame_content(void)
{
    return s_content;
}

void ui_frame_reset_content(void)
{
    if (!s_inited) {
        return;                                    /* safe before LVGL/frame is up */
    }
    lv_obj_clean(s_content);                       /* free the app's whole widget tree (§6A) */
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);    /* hide bar → full width by default */
    lv_obj_set_size(s_content, OLED_W, OLED_H);
}

lv_obj_t *ui_text(int x, int y, const char *txt)
{
    lv_obj_t *l = lv_label_create(s_content);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &lv_font_tm_sans, 0);   /* thin proportional content font */
    lv_label_set_text(l, txt ? txt : "");
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *ui_text_row(int row, const char *txt)
{
    return ui_text(0, row * UI_ROW_H, txt);
}

lv_obj_t *ui_text_row_scroll(int row, const char *txt)
{
    lv_obj_t *l = ui_text(0, row * UI_ROW_H, txt);
    /* Constrain to the content width so LVGL knows to scroll an over-long line.
     * The content container is sized by ui_frame_set_hints() (full or minus bar). */
    lv_obj_set_width(l, lv_obj_get_width(s_content));
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL);   /* back-and-forth */
    return l;
}

void ui_frame_set_hints(const control_hints_t *h)
{
    if (h == NULL) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_content, OLED_W, OLED_H);     /* full width */
        return;
    }
    lv_label_set_text(s_rotate_lbl, h->rotate ? h->rotate : "<>");
    lv_label_set_text(s_click_lbl,  h->click  ? h->click  : "");
    lv_label_set_text(s_select_lbl, h->select ? h->select : "");
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_content, CONTENT_W, OLED_H);      /* leave room for the bar */
}
