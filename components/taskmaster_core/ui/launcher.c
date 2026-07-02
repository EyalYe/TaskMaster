/*
 * launcher.c — the app list, built on the generic ui_list widget (PLAN §6.1, §8.5).
 *
 * Proves ui_list is task-agnostic: the Launcher is a title + a ui_list of app names,
 * plus a bottom status bar (connectivity glyph + time + weather, when online with a
 * city set, §6C). The right hint bar is OS-drawn (ui_frame).
 */
#include "launcher.h"

#include "app_manager.h"
#include "net_status.h"
#include "nvs_config.h"
#include "wx.h"
#include "hint_glyphs.h"
#include "ui_frame.h"
#include "ui_list.h"

#include <stdio.h>

/* Launcher control hints (right bar): rotate scrolls, click/Select opens. */
static const control_hints_t LAUNCHER_HINTS = { .rotate = "<>", .click = "OPN", .select = "OPN" };

#define LAUNCHER_TITLE_ROW 0
#define LIST_FIRST_ROW     1
#define LAUNCHER_NET_ROW   (UI_ROWS - 1)             /* inline net on the last row */
#define LIST_ROWS          (UI_ROWS - 2)             /* rows between title and net */
#define LAUNCHER_LINE_MAX  20  /* max chars per rendered line */
#define LAUNCHER_MAX_APPS  16  /* matches app_manager's registry cap */

/* Bottom status bar: connectivity glyph (left) + local time + temperature, and a
 * weather glyph (right). */
#define STATUS_GLYPH_X     0
#define STATUS_GLYPH_Y     1              /* nudge down to sit on the text baseline */
#define STATUS_TIME_X      19             /* local time, after the connectivity glyph */
#define STATUS_TEMP_X      58             /* temperature (own label, independent of time) */
#define STATUS_TEXT_DY     4              /* drop time/temp onto the glyph line */
#define STATUS_WX_X        (CONTENT_W - 17)  /* weather glyph near the right edge */
#define STATUS_WX_GLYPH_Y  2              /* drop the weather glyph onto the text line */
#define WX_DAY_START_H     6              /* local hour [6,19) → day glyph, else night */
#define WX_DAY_END_H       19

/* WMO weather code → a 1-bit weather glyph (day/night picks the partly-cloudy art). */
static const lv_image_dsc_t *weather_glyph(int code, bool day)
{
    if (code == 0) {
        return &glyph_wx_sun;                                    /* clear */
    }
    if (code <= 3 || code == 45 || code == 48) {                 /* cloudy / overcast / fog */
        return day ? &glyph_wx_cloud_day : &glyph_wx_cloud_night;
    }
    return &glyph_wx_rain;                                       /* any precipitation */
}

static ui_list_t s_list;

/* Visible (available) app registry indices — apps whose available() returns false
 * (e.g. an unconfigured task source) are filtered out so they don't show/open. */
static int s_vis[LAUNCHER_MAX_APPS];
static int s_vis_n;

static void rebuild_visible(void)
{
    s_vis_n = 0;
    unsigned n = app_manager_count();
    for (unsigned i = 0; i < n && s_vis_n < LAUNCHER_MAX_APPS; i++) {
        const device_app_t *a = app_manager_get(i);
        if (a && (a->available == NULL || a->available())) {
            s_vis[s_vis_n++] = (int)i;
        }
    }
}

/* ui_list row text: the name of the visible app at list position `index`. */
static void launcher_row_text(int index, char *buf, int buf_sz, void *ctx)
{
    (void)ctx;
    const device_app_t *a = (index >= 0 && index < s_vis_n)
                          ? app_manager_get((unsigned)s_vis[index]) : NULL;
    snprintf(buf, buf_sz, "%s", (a && a->name) ? a->name : "?");
}

void launcher_render(void)
{
    rebuild_visible();                         /* an app may have just become (un)available */
    ui_list_set_count(&s_list, s_vis_n);

    lv_obj_clean(ui_frame_content());          /* blank slate, then rebuild */
    ui_frame_set_hints(&LAUNCHER_HINTS);       /* size content + show the bar FIRST */
    ui_text_row(LAUNCHER_TITLE_ROW, "TaskMaster");

    if (s_vis_n == 0) {
        ui_text_row(LIST_FIRST_ROW, " (no apps)");
    } else {
        ui_list_draw(&s_list, LIST_FIRST_ROW, launcher_row_text, NULL);
    }

    /* Status bar on the BOTTOM row (§6C): always a connectivity glyph —
     * connected / not-connected — plus local time + weather when online and they're
     * ready (a city is set). */
    int y = LAUNCHER_NET_ROW * UI_ROW_H;
    bool online = net_is_online();
    ui_image(STATUS_GLYPH_X, y + STATUS_GLYPH_Y,
             online ? &glyph_connected : &glyph_not_connected);

    char tstr[8];
    int temp = 0, code = 0;
    if (online && wx_time_str(tstr, sizeof(tstr)) && wx_weather(&temp, &code)) {
        /* Temperature in the user's unit (Settings → Units). Open-Meteo gives °C. */
        uint8_t fahrenheit = 0;
        config_get_u8("fahrenheit", &fahrenheit);
        int shown = fahrenheit ? temp * 9 / 5 + 32 : temp;

        ui_text(STATUS_TIME_X, y + STATUS_TEXT_DY, tstr);   /* local time */
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%d%c", shown, fahrenheit ? 'F' : 'C');
        ui_text(STATUS_TEMP_X, y + STATUS_TEXT_DY, tmp);    /* temperature, own position */

        int hour = (tstr[0] - '0') * 10 + (tstr[1] - '0');
        bool day = (hour >= WX_DAY_START_H && hour < WX_DAY_END_H);
        ui_image(STATUS_WX_X, y + STATUS_WX_GLYPH_Y, weather_glyph(code, day));
    }
}

void launcher_open(void)
{
    ui_list_init(&s_list, LIST_ROWS);
    rebuild_visible();
    ui_list_set_count(&s_list, s_vis_n);
    launcher_render();
}

int launcher_input(input_event_t ev)
{
    switch (ev) {
    case EV_ENCODER_CW:
        ui_list_move(&s_list, +1);
        launcher_render();
        break;
    case EV_ENCODER_CCW:
        ui_list_move(&s_list, -1);
        launcher_render();
        break;
    case EV_ENCODER_CLICK:
    case EV_SELECT:
        if (s_vis_n > 0) {
            int sel = ui_list_sel(&s_list);       /* position in the visible list */
            if (sel >= 0 && sel < s_vis_n) {
                return s_vis[sel];                /* → real app registry index */
            }
        }
        break;
    default:
        break;   /* EV_HOME never arrives here (§5.2) */
    }
    return -1;
}
