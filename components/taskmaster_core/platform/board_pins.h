/*
 * board_pins.h — the ONE place hardware wiring is described (PLAN §5.1).
 *
 * Target: Seeed XIAO ESP32-C3. Pad → GPIO mapping (silkscreen Dn):
 *   D0=GPIO2  D1=GPIO3  D2=GPIO4  D3=GPIO5  D4=GPIO6(SDA)  D5=GPIO7(SCL)
 *   D6=GPIO21 D7=GPIO20 D8=GPIO8  D9=GPIO9  D10=GPIO10
 *
 * GPIO8/GPIO9 are strapping pins (boot mode) — avoided for buttons so a press
 * at reset can't drop the chip into download mode.
 */
#pragma once

#include "driver/gpio.h"

/* --- I2C OLED (SH1106, 128x64) --- */
#define PIN_I2C_SDA      GPIO_NUM_6   /* D4 */
#define PIN_I2C_SCL      GPIO_NUM_7   /* D5 */
#define OLED_I2C_ADDR    0x3C
#define OLED_I2C_HZ      400000

/* --- EC11 rotary encoder (app-usable) --- */
#define PIN_ENC_A        GPIO_NUM_2   /* D0 */
#define PIN_ENC_B        GPIO_NUM_3   /* D1 */
#define PIN_ENC_SW       GPIO_NUM_4   /* D2  (encoder push = app-usable) */

/* --- Face buttons --- */
#define PIN_BTN_SELECT   GPIO_NUM_5   /* D3  (app-usable) */
#define PIN_BTN_HOME     GPIO_NUM_10  /* D10 (OS-reserved: return to Launcher) */

/* --- Free for app hardware (unclaimed by core) ---
 * A third-party app may drive its own peripherals on these pads. There is no
 * arbitration yet (an enforced app_gpio claim/release service lands in Phase 6,
 * §14) — it's convention: claim in init(), release in exit(), and NEVER touch a
 * core pin above. See docs/APP_API.md §9.
 *   D6 = GPIO21   free
 *   D7 = GPIO20   free
 *   D8 = GPIO8    free but STRAPPING (boot mode) — don't hold low at reset
 *   D9 = GPIO9    free but STRAPPING — same caveat
 * Plus the shared I2C bus (SDA/SCL above) for extra devices at other addresses. */
