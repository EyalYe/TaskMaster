#include "input.h"
#include "board_pins.h"

#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "input";

#define POLL_MS         1
#define DEBOUNCE_SAMPLES 20      /* 20 * 1ms = 20ms stable before a button commits */

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
    input_event_t ev;
    int          stable;   /* last committed level (1 = released) */
    int          cnt;      /* consecutive samples disagreeing with `stable` */
} button_t;

static button_t s_buttons[] = {
    { PIN_ENC_SW,     EV_ENCODER_CLICK, 1, 0 },
    { PIN_BTN_SELECT, EV_SELECT,        1, 0 },
    { PIN_BTN_HOME,   EV_HOME,          1, 0 },
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

        /* buttons (debounce, emit on press edge) */
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            button_t *b = &s_buttons[i];
            int raw = gpio_get_level(b->pin);
            if (raw == b->stable) {
                b->cnt = 0;
            } else if (++b->cnt >= DEBOUNCE_SAMPLES) {
                b->stable = raw;
                b->cnt = 0;
                if (raw == 0) post(b->ev);   /* pressed (active-low) */
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
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
        case EV_HOME:          return "HOME";
        default:               return "?";
    }
}
