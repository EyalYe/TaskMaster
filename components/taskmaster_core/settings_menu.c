/*
 * settings_menu.c — schema-driven settings editor. See settings_menu.h.
 */
#include "settings_menu.h"
#include "confirm.h"
#include "ui_frame.h"

#include <stdio.h>

#define SETTING_VAL_MAX 24   /* formatted value string (choice label / number+unit) */

static void fmt_value(const setting_item_t *it, char *buf, int sz)
{
    switch (it->kind) {
    case SETTING_TOGGLE:
        snprintf(buf, sz, "%s", (it->get && it->get()) ? "On" : "Off");
        break;
    case SETTING_ENUM: {
        int v = it->get ? it->get() : 0;
        if (v < 0)                 v = 0;
        if (v >= it->choice_count) v = it->choice_count - 1;
        snprintf(buf, sz, "%s", (it->choices && v >= 0) ? it->choices[v] : "?");
        break;
    }
    case SETTING_RANGE:
        snprintf(buf, sz, "%d%s", it->get ? it->get() : 0, it->unit ? it->unit : "");
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

/* ui_list row text: "Label: value" (ACTION rows are just the label; the editing row
 * brackets its value: "Label: [value]"). */
static void row_text(int index, char *buf, int buf_sz, void *ctx)
{
    settings_menu_t *m = (settings_menu_t *)ctx;
    const setting_item_t *it = &m->items[index];
    if (it->kind == SETTING_ACTION) {
        snprintf(buf, buf_sz, "%s", it->label);
        return;
    }
    char val[SETTING_VAL_MAX];
    fmt_value(it, val, sizeof(val));
    if (m->editing == index) {
        snprintf(buf, buf_sz, "%s: [%s]", it->label, val);
    } else {
        snprintf(buf, buf_sz, "%s: %s", it->label, val);
    }
}

/* Change the value of an ENUM/RANGE row by one step (persist + apply live via set()). */
static void adjust(const setting_item_t *e, int dir)
{
    if (!e->get || !e->set) {
        return;
    }
    int v = e->get();
    if (e->kind == SETTING_ENUM) {
        int n = e->choice_count > 0 ? e->choice_count : 1;
        v = ((v + dir) % n + n) % n;            /* wrap both ways */
    } else if (e->kind == SETTING_RANGE) {
        v += dir * (e->step ? e->step : 1);
        if (v < e->min) v = e->min;
        if (v > e->max) v = e->max;
    }
    e->set(v);
}

static void confirm_cb(bool yes, void *ctx)
{
    const setting_item_t *it = (const setting_item_t *)ctx;
    if (yes && it->action) {
        it->action();
    }
}

static void activate(settings_menu_t *m)
{
    const setting_item_t *it = &m->items[ui_list_sel(&m->list)];
    switch (it->kind) {
    case SETTING_TOGGLE:
        if (it->get && it->set) {
            it->set(it->get() ? 0 : 1);
        }
        break;
    case SETTING_ENUM:
    case SETTING_RANGE:
        m->editing = ui_list_sel(&m->list);     /* enter edit mode */
        break;
    case SETTING_ACTION:
        if (it->confirm) {
            confirm_open(it->confirm, confirm_cb, (void *)it);
        } else if (it->action) {
            it->action();
        }
        break;
    }
}

void settings_menu_init(settings_menu_t *m, const setting_item_t *items, int count,
                        int visible_rows)
{
    m->items   = items;
    m->count   = count;
    m->editing = -1;
    ui_list_init(&m->list, visible_rows);
    ui_list_set_count(&m->list, count);
}

void settings_menu_input(settings_menu_t *m, input_event_t ev)
{
    if (m->editing >= 0) {                       /* editing a value */
        const setting_item_t *e = &m->items[m->editing];
        switch (ev) {
        case EV_ENCODER_CW:  adjust(e, +1);  break;
        case EV_ENCODER_CCW: adjust(e, -1);  break;
        case EV_ENCODER_CLICK:
        case EV_SELECT:      m->editing = -1; break;   /* commit + leave edit mode */
        default: break;
        }
        return;
    }
    switch (ev) {                                /* navigating */
    case EV_ENCODER_CW:  ui_list_move(&m->list, +1); break;
    case EV_ENCODER_CCW: ui_list_move(&m->list, -1); break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:      activate(m);            break;
    default: break;
    }
}

void settings_menu_render(settings_menu_t *m, int y0_row)
{
    ui_list_draw(&m->list, y0_row, row_text, m);
}

bool settings_menu_editing(const settings_menu_t *m)
{
    return m->editing >= 0;
}
