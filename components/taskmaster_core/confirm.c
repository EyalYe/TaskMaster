/*
 * confirm.c — reusable modal yes/no dialog. See confirm.h.
 *
 * Prompt at the top (wraps), No/Yes on the bottom two rows with a "> " cursor; rotate
 * flips the choice, Select/click commits. State is cleared BEFORE the callback runs so
 * an action that reboots (restart / factory reset) can't re-enter a live modal.
 */
#include "confirm.h"
#include "ui_frame.h"

#define CONFIRM_PROMPT_ROW  0
#define CONFIRM_NO_ROW      (UI_ROWS - 2)
#define CONFIRM_YES_ROW     (UI_ROWS - 1)
#define CONFIRM_SEL_NO      0
#define CONFIRM_SEL_YES     1

static const control_hints_t CONFIRM_HINTS = { .rotate = "<>", .click = "OK", .select = "OK" };

static bool         s_active;
static const char  *s_prompt;
static confirm_cb_t s_cb;
static void        *s_ctx;
static int          s_sel;

void confirm_open(const char *prompt, confirm_cb_t cb, void *ctx)
{
    s_prompt = prompt;
    s_cb     = cb;
    s_ctx    = ctx;
    s_sel    = CONFIRM_SEL_NO;   /* safe default */
    s_active = true;
}

bool confirm_active(void) { return s_active; }

void confirm_reset(void)
{
    s_active = false;
    s_cb     = NULL;
}

void confirm_input(input_event_t ev)
{
    if (!s_active) {
        return;
    }
    switch (ev) {
    case EV_ENCODER_CW:
    case EV_ENCODER_CCW:
        s_sel = (s_sel == CONFIRM_SEL_NO) ? CONFIRM_SEL_YES : CONFIRM_SEL_NO;
        break;
    case EV_ENCODER_CLICK:
    case EV_SELECT: {
        bool         yes = (s_sel == CONFIRM_SEL_YES);
        confirm_cb_t cb  = s_cb;
        void        *ctx = s_ctx;
        s_active = false;           /* clear before the callback — it may reboot */
        s_cb     = NULL;
        if (cb) {
            cb(yes, ctx);
        }
        break;
    }
    default:
        break;
    }
}

void confirm_render(void)
{
    lv_obj_clean(ui_frame_content());
    ui_frame_set_hints(&CONFIRM_HINTS);
    ui_text_wrap(CONFIRM_PROMPT_ROW, s_prompt ? s_prompt : "Are you sure?");
    ui_text_row(CONFIRM_NO_ROW,  s_sel == CONFIRM_SEL_NO  ? "> No"  : "  No");
    ui_text_row(CONFIRM_YES_ROW, s_sel == CONFIRM_SEL_YES ? "> Yes" : "  Yes");
}
