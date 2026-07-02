/*
 * ui_frame.c — OS LVGL frame: content container + right hint bar. See ui_frame.h.
 *
 * Geometry mirrors the raw prototype (hint_bar.h): bar column x=107, 21px wide;
 * Home y1..16 (16px), Encoder y18..45 (28px, split), Select y47..62 (16px).
 */
#include "ui_frame.h"
#include "sh1106.h"          /* OLED_W / OLED_H */
#include "lv_font_tm5x7.h"   /* thin 5x7 font for the hint labels */
#include "hint_glyphs.h"     /* 1-bit hint-bar glyphs (§6C.1 step 1) */

#include <string.h>

static bool      s_inited;
static lv_obj_t *s_content;
static lv_obj_t *s_bar;          /* transparent full-screen layer holding the boxes */
static lv_obj_t *s_rotate_lbl;
static lv_obj_t *s_rotate_img;   /* scroll glyph shown for the default rotate hint */
static lv_obj_t *s_click_lbl;
static lv_obj_t *s_click_img;    /* glyph shown for a known click token (e.g. DON) */
static lv_obj_t *s_select_lbl;
static lv_obj_t *s_select_img;   /* glyph shown for a known Select token (e.g. OPN) */

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

/* A 1-bit glyph (A1 image) drawn in white — for the hint boxes (§6C.1 step 1). */
static lv_obj_t *make_glyph(lv_obj_t *parent, const lv_image_dsc_t *dsc)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, lv_color_white(), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}

/* Map a click/Select hint token to a glyph, or NULL to keep the ≤3-char text
 * (§6C.1: glyph where one exists, text is always the fallback). Core recognizes a
 * few conventional tokens; apps needn't change (an unmapped label just shows text). */
static const lv_image_dsc_t *hint_action_glyph(const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    if (strcmp(s, "DON") == 0) return &glyph_check;    /* complete / done */
    if (strcmp(s, "OPN") == 0 || strcmp(s, "SEL") == 0) return &glyph_select;
    if (strcmp(s, "MNU") == 0) return &glyph_menu;     /* menu / detail */
    if (strcmp(s, "RST") == 0) return &glyph_reset;    /* reset / back */
    return NULL;
}

/* Show either the token's glyph (if mapped) or its text in a hint cell. */
static void set_action_hint(lv_obj_t *lbl, lv_obj_t *img, const char *s)
{
    const lv_image_dsc_t *g = hint_action_glyph(s);
    if (g != NULL) {
        lv_image_set_src(img, g);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(lbl, s ? s : "");
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
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

    /* Home (OS-fixed) — the house glyph. */
    lv_obj_t *home = make_box(s_bar, HINT_BAR_X, HINT_HOME_Y, HINT_BAR_W, HINT_HOME_H);
    lv_obj_align(make_glyph(home, &glyph_home), LV_ALIGN_CENTER, HINT_LBL_DX, 0);

    /* Encoder — split into rotate (top) / click (bottom) by a divider at mid.
     * Rotate cell shows the scroll glyph by default, or the app's text label.
     * Nudge the two toward the divider (rotate down, click up). */
    lv_obj_t *enc = make_box(s_bar, HINT_BAR_X, HINT_ENC_Y, HINT_BAR_W, HINT_ENC_H);
    s_rotate_lbl = make_label(enc, "<>");
    lv_obj_align(s_rotate_lbl, LV_ALIGN_TOP_MID, HINT_LBL_DX, HINT_ROT_DY);
    s_rotate_img = make_glyph(enc, &glyph_scroll);
    lv_obj_align(s_rotate_img, LV_ALIGN_TOP_MID, HINT_LBL_DX, HINT_ROT_GLYPH_DY);
    lv_obj_t *divln = lv_obj_create(enc);
    lv_obj_remove_style_all(divln);
    lv_obj_set_size(divln, HINT_BAR_W - 2 * HINT_BOX_GAP, HINT_BOX_GAP);
    lv_obj_align(divln, LV_ALIGN_TOP_MID, 0, HINT_ENC_SPLIT_DY);
    lv_obj_set_style_bg_color(divln, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(divln, LV_OPA_COVER, 0);
    s_click_lbl = make_label(enc, "");
    lv_obj_align(s_click_lbl, LV_ALIGN_BOTTOM_MID, HINT_LBL_DX, -HINT_CLICK_DY);
    s_click_img = make_glyph(enc, &glyph_check);
    lv_obj_align(s_click_img, LV_ALIGN_BOTTOM_MID, HINT_GLYPH_DX, -HINT_CLICK_DY + HINT_GLYPH_DY);
    lv_obj_add_flag(s_click_img, LV_OBJ_FLAG_HIDDEN);

    /* Select. */
    lv_obj_t *sel = make_box(s_bar, HINT_BAR_X, HINT_SEL_Y, HINT_BAR_W, HINT_SEL_H);
    s_select_lbl = make_label(sel, "");
    lv_obj_align(s_select_lbl, LV_ALIGN_CENTER, HINT_LBL_DX, 0);
    s_select_img = make_glyph(sel, &glyph_select);
    lv_obj_align(s_select_img, LV_ALIGN_CENTER, HINT_GLYPH_DX, HINT_GLYPH_DY);
    lv_obj_add_flag(s_select_img, LV_OBJ_FLAG_HIDDEN);

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

lv_obj_t *ui_image(int x, int y, const lv_image_dsc_t *dsc)
{
    lv_obj_t *img = lv_image_create(s_content);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, lv_color_white(), 0);   /* 1-bit A1 → white */
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x, y);
    return img;
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

lv_obj_t *ui_text_wrap(int row, const char *txt)
{
    lv_obj_t *l = ui_text(0, row * UI_ROW_H, txt);
    /* Word-wrap across the remaining rows (for a multi-line body, e.g. a task
     * description). Width = content width so LVGL breaks lines; height is left to
     * the content so overflow is simply clipped at the panel edge. */
    lv_obj_set_width(l, lv_obj_get_width(s_content));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

void ui_frame_set_hints(const control_hints_t *h)
{
    if (h == NULL) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_content, OLED_W, OLED_H);     /* full width */
        return;
    }
    /* Rotate cell: the scroll glyph for the default hint (NULL or "<>"), else the
     * app's text label (§6C.1 step 1 — text stays the fallback). */
    bool rot_glyph = (h->rotate == NULL) || (strcmp(h->rotate, "<>") == 0);
    if (rot_glyph) {
        lv_obj_add_flag(s_rotate_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_rotate_img, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_rotate_lbl, h->rotate);
        lv_obj_remove_flag(s_rotate_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_rotate_img, LV_OBJ_FLAG_HIDDEN);
    }
    set_action_hint(s_click_lbl,  s_click_img,  h->click);
    set_action_hint(s_select_lbl, s_select_img, h->select);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_content, CONTENT_W, OLED_H);      /* leave room for the bar */
}
