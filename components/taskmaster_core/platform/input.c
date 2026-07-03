#include "input.h"
#include "board_pins.h"

#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_log.h"

static const char *TAG = "input";

/* Interrupt-driven input (§7A step 2): the encoder is decoded in a GPIO ISR (no poll),
 * and the buttons use a poll-on-demand task that blocks while everything is released and
 * only polls (for debounce + long-press timing) while a button is down. With no periodic
 * poll, the CPU can enter tickless idle / auto light-sleep between inputs. */
#define BTN_POLL_MS      10      /* poll cadence WHILE a button is active (else blocked) */
#define DEBOUNCE_SAMPLES 2       /* 2 * 10ms = 20ms stable before a button commits */
#define LONG_PRESS_MS   700      /* hold this long → the button's long-press event */

static QueueHandle_t s_queue;
static TaskHandle_t  s_btn_task;

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

/* Encoder decoded in the GPIO ISR: each A/B edge advances the Ben-Buxton table; a full
 * detent posts CW/CCW. The A and B handlers are serialized by the GPIO ISR service, so
 * the shared state needs no extra locking. */
static volatile uint8_t s_enc_state = R_START;

static void enc_isr(void *arg)
{
    uint8_t pins = (gpio_get_level(PIN_ENC_A) << 1) | gpio_get_level(PIN_ENC_B);
    s_enc_state = ttable[s_enc_state & 0x0F][pins];
    input_event_t ev;
    switch (s_enc_state & 0x30) {
        case DIR_CW:  ev = EV_ENCODER_CW;  break;
        case DIR_CCW: ev = EV_ENCODER_CCW; break;
        default: return;
    }
    BaseType_t hp = pdFALSE;
    xQueueSendFromISR(s_queue, &ev, &hp);
    if (hp) portYIELD_FROM_ISR();
}

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

/* One debounce/long-press pass over the buttons. Plain buttons emit on the press edge;
 * a long-press-capable button (Select/Home) defers — it emits the long event once held
 * past LONG_PRESS_MS, or the short event on release if let go first. Returns true while
 * any button is still down or mid-debounce (→ keep polling), false when all are at rest. */
static bool poll_buttons(void)
{
    bool active = false;
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        button_t *b = &s_buttons[i];
        int raw = gpio_get_level(b->pin);
        if (raw == b->stable) {
            b->cnt = 0;
            if (b->long_ev && b->stable == 0) {           /* still held down */
                b->held_ms += BTN_POLL_MS;
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
        if (b->stable == 0 || b->cnt != 0) active = true;  /* pressed or mid-debounce */
    }
    return active;
}

/* A button edge wakes the poll task (which then debounces + times the hold). */
static void btn_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    vTaskNotifyGiveFromISR(s_btn_task, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static void btn_task(void *arg)
{
    for (;;) {
        if (poll_buttons()) {
            vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));   /* active → keep polling */
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  /* idle → block until a button edge */
        }
    }
}

/* All input pins: rest HIGH (pull-ups; encoder detent = A,B both high, §4.3), so a
 * press or a turn drives one LOW — a valid light-sleep wake trigger (§8A step 5). */
static const gpio_num_t s_wake_pins[] = {
    PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW, PIN_BTN_SELECT, PIN_BTN_HOME,
};
#define NUM_WAKE_PINS (sizeof(s_wake_pins) / sizeof(s_wake_pins[0]))

/* Arm the input pins as LOW_LEVEL light-sleep wake sources (Home only in pocket mode):
 * pins rest HIGH, so a press or a turn drives one LOW → a valid wake. This overwrites
 * the runtime edge-decode interrupt type on those pins — restore_edge_intr() must put it
 * back on wake, or the level trigger storms while a pin sits LOW. */
static void arm_level_wake(bool home_only)
{
    if (home_only) {
        gpio_wakeup_enable(PIN_BTN_HOME, GPIO_INTR_LOW_LEVEL);   /* pocket mode: Home only */
    } else {
        for (size_t i = 0; i < NUM_WAKE_PINS; i++) {
            gpio_wakeup_enable(s_wake_pins[i], GPIO_INTR_LOW_LEVEL);
        }
    }
}

/* Undo arm_level_wake(): drop the wake bits and put the edge triggers back so the
 * encoder ISR decodes again (ANYEDGE on A/B) and the buttons fire on press (NEGEDGE). */
static void restore_edge_intr(bool home_only)
{
    if (home_only) {
        gpio_wakeup_disable(PIN_BTN_HOME);
        gpio_set_intr_type(PIN_BTN_HOME, GPIO_INTR_NEGEDGE);
    } else {
        for (size_t i = 0; i < NUM_WAKE_PINS; i++) {
            gpio_wakeup_disable(s_wake_pins[i]);
        }
        gpio_set_intr_type(PIN_ENC_A, GPIO_INTR_ANYEDGE);
        gpio_set_intr_type(PIN_ENC_B, GPIO_INTR_ANYEDGE);
        for (size_t i = 0; i < NUM_BUTTONS; i++) {
            gpio_set_intr_type(s_buttons[i].pin, GPIO_INTR_NEGEDGE);
        }
    }
}

/* Manual light sleep (§8A): the UI task calls this when blanked + deep-sleep is on
 * (and home_only while locked → pocket mode). Blocks until a wake pin goes LOW. This
 * is the aggressive screen-off path; the automatic tier below runs independently. */
void input_light_sleep(bool home_only)
{
    arm_level_wake(home_only);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();                 /* halts the CPU until a wake pin goes LOW */
    restore_edge_intr(home_only);
}

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
/* Automatic light sleep (§7A step 2): with input interrupt-driven, esp_pm drops the CPU
 * whenever the scheduler is idle. These hooks run (paired) inside vApplicationSleep's
 * critical section around every sleep attempt — arm level wake on the way down, restore
 * edge decode on the way up — so any turn/press wakes the CPU and decoding resumes. They
 * always wake on ALL pins; pocket-mode's Home-only wake is the manual path above. */
static esp_err_t pm_sleep_enter_cb(int64_t sleep_time_us, void *arg)
{
    arm_level_wake(false);
    return ESP_OK;
}

static esp_err_t pm_sleep_exit_cb(int64_t slept_us, void *arg)
{
    restore_edge_intr(false);
    return ESP_OK;
}
#endif

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
    xTaskCreate(btn_task, "input_btn", 3072, NULL, 10, &s_btn_task);

    /* Encoder A/B → decode in the ISR (any edge); buttons → wake the poll task (press). */
    gpio_install_isr_service(0);
    gpio_set_intr_type(PIN_ENC_A, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(PIN_ENC_B, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(PIN_ENC_A, enc_isr, NULL);
    gpio_isr_handler_add(PIN_ENC_B, enc_isr, NULL);
    for (size_t i = 0; i < NUM_BUTTONS; i++) {
        gpio_set_intr_type(s_buttons[i].pin, GPIO_INTR_NEGEDGE);
        gpio_isr_handler_add(s_buttons[i].pin, btn_isr, NULL);
    }

    /* Automatic light-sleep wake (§7A step 2): GPIO is the sleep wake source, and the
     * enter/exit callbacks flip the pins level(wake)↔edge(decode) around each auto sleep
     * so a knob turn or button press wakes the CPU without breaking quadrature decoding. */
    esp_sleep_enable_gpio_wakeup();
#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
    esp_pm_sleep_cbs_register_config_t cbs = {
        .enter_cb = pm_sleep_enter_cb,
        .exit_cb  = pm_sleep_exit_cb,
    };
    ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&cbs));
#endif

    ESP_LOGI(TAG, "input: interrupt-driven (encoder ISR + %d-button task)", (int)NUM_BUTTONS);
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
