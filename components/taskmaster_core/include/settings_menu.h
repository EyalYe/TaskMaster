/*
 * settings_menu.h — a schema-driven settings editor (PLAN §8A.1 step 0).
 *
 * Settings are declared as a table of rows; one generic editor renders + edits any of
 * them onto the core ui_list, so adding a setting is a row, not a screen. Core device
 * settings and per-app ACFG_KNOB fields (§9.4) share this editor, so core hardcodes no
 * app. Row kinds:
 *   TOGGLE  — 0/1, shown Off/On; Select flips it in place.
 *   ENUM    — value = index into choices[]; Select enters edit, rotate cycles (wraps).
 *   RANGE   — value in [min,max] by step; Select enters edit, rotate bumps (clamped).
 *   ACTION  — no value; Select invokes action() (behind a confirm dialog if `confirm`).
 *
 * Values are read/written through get()/set() so the editor stays decoupled from NVS;
 * set() is called live on every change (persist + apply immediately, e.g. brightness).
 * The host app routes input/render here (after checking confirm_active(), confirm.h).
 */
#pragma once

#include "ui_list.h"
#include "input.h"
#include <stdbool.h>

typedef enum {
    SETTING_TOGGLE,
    SETTING_ENUM,
    SETTING_RANGE,
    SETTING_ACTION,
} setting_kind_t;

typedef struct setting_item {
    const char        *label;
    setting_kind_t     kind;
    /* value accessors (TOGGLE / ENUM / RANGE) */
    int   (*get)(void);
    void  (*set)(int value);
    /* ENUM */
    const char *const *choices;
    int                choice_count;
    /* RANGE */
    int                min;
    int                max;
    int                step;
    const char        *unit;      /* optional value suffix, e.g. "%" (NULL = none) */
    /* ACTION */
    void  (*action)(void);
    const char        *confirm;   /* NULL = act immediately; else confirm-dialog prompt */
} setting_item_t;

typedef struct {
    ui_list_t             list;
    const setting_item_t *items;
    int                   count;
    int                   editing;   /* -1 = navigating; else the index being edited */
} settings_menu_t;

void settings_menu_init(settings_menu_t *m, const setting_item_t *items, int count,
                        int visible_rows);
void settings_menu_input(settings_menu_t *m, input_event_t ev);
void settings_menu_render(settings_menu_t *m, int y0_row);   /* draw from text row y0_row */
bool settings_menu_editing(const settings_menu_t *m);        /* true while editing a value */
