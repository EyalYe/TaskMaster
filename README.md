# TaskMaster-C3 — Firmware

ESP-IDF / FreeRTOS firmware for a XIAO ESP32-C3 desktop task appliance.
See [`PLAN.md`](PLAN.md) for the full spec. This README covers the current build.

## Status: Phase 0 — bring-up + SoftAP spike

One firmware that exercises all four Phase-0 exit criteria:

1. **Boots** — ESP-IDF app + dual-slot OTA partition table.
2. **OLED draws** — SH1106 128×64 over I²C (self-contained driver, no external component).
3. **Encoder + buttons register** — 1 ms polling task (Ben-Buxton quadrature decode + debounce);
   every input is logged to serial and shown live on the OLED's bottom line.
4. **Phone loads a page over SoftAP** — open AP `TaskMaster-Setup` + HTTP server + captive-portal DNS.

> Phase 0 uses **core ESP-IDF drivers only** (no managed components) to keep the first build
> dependency-light and debuggable. LVGL and the `knob`/`button` components arrive in Phase 1.
> The encoder is GPIO-polled because the **ESP32-C3 has no PCNT peripheral**.

## Wiring (XIAO ESP32-C3)

Single source of truth: [`main/board_pins.h`](main/board_pins.h).

| Signal | Pad | GPIO |
|---|---|---|
| OLED SDA | D4 | GPIO6 |
| OLED SCL | D5 | GPIO7 |
| Encoder A | D0 | GPIO2 |
| Encoder B | D1 | GPIO3 |
| Encoder switch | D2 | GPIO4 |
| Select button | D3 | GPIO5 |
| Home button | D10 | GPIO10 |

All buttons + encoder are active-low to GND using internal pull-ups. OLED I²C address `0x3C`.
(GPIO8/9 are strapping pins — intentionally unused for buttons.)

## Build & flash

First activate the ESP-IDF environment in the shell (once per session):
```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.1.sh"
```
Then:
```bash
idf.py set-target esp32c3              # first time only
idf.py build                           # build
idf.py -p <PORT> flash monitor         # flash over USB-C + open serial @ 115200
```
Find `<PORT>` with `ls /dev/cu.*` (usually `/dev/cu.usbmodem*`). Add `-C <dir>` to run from
outside the project root.

## Verify (Phase 0 done when all pass)

- [ ] Serial shows `Phase 0 up.` and no boot loop.
- [ ] OLED shows the splash (`TASKMASTER-C3 / PHASE 0 BRINGUP / AP: … / HTTP …`).
- [ ] Rotating the encoder logs `ENCODER CW`/`CCW` and updates the OLED bottom line; pressing it
      logs `ENCODER CLICK`; Select → `SELECT`; Home → `HOME`.
- [ ] Phone/laptop sees Wi-Fi `TaskMaster-Setup`; joining pops a captive portal (or browse
      `http://192.168.4.1`) showing the "Phase 0 SoftAP portal is alive" page.

## Project layout

```
partitions.csv      4MB dual-slot OTA table (PLAN §9.1)
sdkconfig.defaults  baseline Kconfig (1kHz tick, 4MB flash, http server)
CMakeLists.txt      ESP-IDF project root
main/
  main.c            app_main: NVS → OLED → input → SoftAP, live event loop
  board_pins.h      GPIO map (only place wiring is described)
  sh1106.[ch]       SH1106 I²C driver (framebuffer + 5x7 text), 2-px col offset
  font5x7.h         bring-up font (ASCII 0x20–0x5A)
  input.[ch]        encoder + button polling task → event queue
  softap_portal.[ch] SoftAP + esp_http_server + captive DNS spike
```

## Build status

**Compiles clean on ESP-IDF v6.0.1** for `esp32c3` (`idf.py build`). App image ≈ 875 KB — 56% free
in the 1.9 MB OTA slot. Not yet flashed to hardware.

Expect first-on-hardware tweaks — most likely SH1106 init tuning (contrast/VCOMH) or the I²C address,
and confirming the XIAO pad→GPIO mapping against your encoder/button harness.

> ESP-IDF v6.0 note: the GPIO/I²C drivers live in `esp_driver_gpio` / `esp_driver_i2c` (the old
> monolithic `driver` component is gone) — reflected in `main/CMakeLists.txt`.
