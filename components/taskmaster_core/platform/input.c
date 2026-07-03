#include "input.h"
#include "board_pins.h"

#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"

static const char *TAG = "input";

#define POLL_MS         1
#define DEBOUNCE_SAMPLES 20      /* 20 * 1ms = 20ms stable before a button commits */
#define LONG_PRESS_MS   700      /* hold this long → the button's long-press event */

static QueueHandle_t s_queue;

/* --- EC11 quadrature decoder: Ben Buxton full-step state table --- */
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20

static const uint8_t ttable[7][4] = {
    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
    {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
    {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
    {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
    {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
    {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

/* --- buttons: active-low, internal pull-up --- */
typedef struct {
    gpio_num_t   pin;
    input_event_t ev;       /* short-press event */
    input_event_t long_ev;  /* long-press event, or 0 = no long-press (emit on press edge) */
    int          stable;    /* last committed level (1 = released) */
    int          cnt;       /* consecutive samples disagreeing with `stable` */
    int          held_ms;   /* time held while pressed (long-press buttons) */
    bool         long_sent; /* the long-press was already emitted this hold */
} button_t;

static button_t s_buttons[] = {
    { PIN_ENC_SW,     EV_ENCODER_CLICK, 0,              1, 0, 0, false },
    { PIN_BTN_SELECT, EV_SELECT,        EV_SELECT_LONG, 1, 0, 0, false },
    { PIN_BTN_HOME,   EV_HOME,          EV_HOME_LONG,   1, 0, 0, false },
};
#define NUM_BUTTONS (sizeof(s_buttons) / sizeof(s_buttons[0]))

static void post(input_event_t ev)
{
    xQueueSend(s_queue, &ev, 0);
}

static void input_task(void *arg)
{
    uint8_t enc_state = R_START;
    for (;;) {
        /* encoder */
        uint8_t pins = (gpio_get_level(PIN_ENC_A) << 1) | gpio_get_level(PIN_ENC_B);
        enc_state = ttable[enc_state & 0x0F][pins];
        switch (enc_state & 0x30) {
            case DIR_CW:  post(EV_ENCODER_CW);  break;
            case DIR_CCW: post(EV_ENCODER_CCW); break;
            default: break;
        }

        /* buttons: plain buttons emit on the press edge. A long-press-capable button
         * (Select) defers — it emits the long event once held past LONG_PRESS_MS, or
         * the short event on release if let go before then. */
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            button_t *b = &s_buttons[i];
            int raw = gpio_get_level(b->pin);
            if (raw == b->stable) {
                b->cnt = 0;
                if (b->long_ev && b->stable == 0) {           /* still held down */
                    b->held_ms += POLL_MS;
                    if (!b->long_sent && b->held_ms >= LONG_PRESS_MS) {
                        post(b->long_ev);
                        b->long_sent = true;
                    }
                }
            } else if (++b->cnt >= DEBOUNCE_SAMPLES) {
                b->stable = raw;
                b->cnt = 0;
                if (!b->long_ev) {
                    if (raw == 0) post(b->ev);                /* plain button: on press */
                } else if (raw == 0) {                         /* long-capable: pressed → start timing */
                    b->held_ms = 0;
                    b->long_sent = false;
                } else if (!b->long_sent) {                     /* released before threshold → short */
                    post(b->ev);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

/* All input pins: rest HIGH (pull-ups; encoder detent = A,B both high, §4.3), so a
 * press or a turn drives one LOW — a valid light-sleep wake trigger (§8A step 5). */
static const gpio_num_t s_wake_pins[] = {
    PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW, PIN_BTN_SELECT, PIN_BTN_HOME,
};
#define NUM_WAKE_PINS (sizeof(s_wake_pins) / sizeof(s_wake_pins[0]))

void input_light_sleep(bool home_only)
{
    if (home_only) {
        gpio_wakeup_enable(PIN_BTN_HOME, GPIO_INTR_LOW_LEVEL);   /* pocket mode: Home only */
    } else {
        for (size_t i = 0; i < NUM_WAKE_PINS; i++) {
            gpio_wakeup_enable(s_wake_pins[i], GPIO_INTR_LOW_LEVEL);
        }
    }
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();                 /* halts the CPU until a wake pin goes LOW */
    if (home_only) {
        gpio_wakeup_disable(PIN_BTN_HOME);
    } else {
        for (size_t i = 0; i < NUM_WAKE_PINS; i++) {
            gpio_wakeup_disable(s_wake_pins[i]); /* back to normal polling */
        }
    }
}

bool input_home_held(void)
{
    /* Self-contained one-shot read so it can run before input_init(). */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BTN_HOME,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int pressed = 0;                       /* active-low: 0 = pressed */
    for (int i = 0; i < 5; i++) {
        if (gpio_get_level(PIN_BTN_HOME) == 0) {
            pressed++;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return pressed >= 4;                    /* held steadily, not a glitch */
}

QueueHandle_t input_init(void)
{
    /* Encoder A/B + all buttons: input, pull-up (common pin → GND). */
    uint64_t mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B);
    for (size_t i = 0; i < NUM_BUTTONS; i++) mask |= (1ULL << s_buttons[i].pin);

    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    s_queue = xQueueCreate(16, sizeof(input_event_t));
    xTaskCreate(input_task, "input", 3072, NULL, 10, NULL);
    ESP_LOGI(TAG, "input task started (encoder + %d buttons)", (int)NUM_BUTTONS);
    return s_queue;
}

const char *input_event_name(input_event_t ev)
{
    switch (ev) {
        case EV_ENCODER_CW:    return "ENCODER CW";
        case EV_ENCODER_CCW:   return "ENCODER CCW";
        case EV_ENCODER_CLICK: return "ENCODER CLICK";
        case EV_SELECT:        return "SELECT";
        case EV_SELECT_LONG:   return "SELECT LONG";
        case EV_HOME:          return "HOME";
        case EV_HOME_LONG:     return "HOME LONG";
        default:               return "?";
    }
}
