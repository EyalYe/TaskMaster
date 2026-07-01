---
name: taskmaster-c3-project
description: "What the ~/TaskMaster project is and how it relates to the user's yapp platform"
metadata: 
  node_type: memory
  type: project
  originSessionId: 1ffe722c-73fa-4024-a1d9-4de2d4a260f2
---

`~/TaskMaster` is **TaskMaster-C3** — an **ESP-IDF / FreeRTOS** firmware project for a XIAO ESP32-C3
desktop task appliance (1.3" SH1106 OLED, EC11 encoder + Select + Home buttons). **CLI-only build via
ESP-IDF `idf.py`** (the user works CLI-only — no PlatformIO/IDE). Full spec in `~/TaskMaster/PLAN.md`.
Git repo pushed to github.com/EyalYe/TaskMaster (origin git@github.com:EyalYe/TaskMaster.git, branch
main). (Dir was renamed from `zepapp` on 2026-06-29 — it had been a Zephyr-era name; framework was
switched to ESP-IDF because its SoftAP/HTTP/provisioning/OTA support is first-class.)

The device's task apps are built on the user's existing **`yapp` platform** (`~/yapp-cli`), a
terminal-first Todoist pipeline (commands: `yapptime`/`yappdate`/`yapplst`/`yappmark`; `todomark.py`
is the click-to-complete GUI the device UI mirrors). A new `yapp-server` proxy will expose `yapp`'s
Todoist logic over a fixed device REST contract; a second "Local" app hits a platform-agnostic LAN box
on the same contract.

Status (2026-06-29): **Phase 0 scaffolded** — full ESP-IDF project written (CMakeLists,
partitions.csv 4MB dual-OTA, sdkconfig.defaults, main/ with sh1106 driver+font5x7, input.c
encoder/button polling, softap_portal.c SoftAP+http+captive-DNS, main.c). NOT yet compiled/flashed —
ESP-IDF v6.0.1 now installed via eim (activate: source ~/.espressif/tools/activate_idf_v6.0.1.sh).
**Phase 0 builds clean** (`idf.py -C ~/TaskMaster build`, target esp32c3); app ~875KB, 56% free in the
1.9MB OTA slot. Not yet flashed (needs the board). v6.0 gotcha: GPIO/I2C drivers are in
esp_driver_gpio/esp_driver_i2c, not the old `driver` meta-component. Phase 0 deliberately uses core ESP-IDF
drivers only (no managed components; C3 has no PCNT so encoder is GPIO-polled Ben-Buxton). Wiring in
main/board_pins.h. README.md has build/flash/verify steps.

Resolved design decisions (all in PLAN.md §16): ESP-IDF framework, CLI-only (`idf.py`), SH1106 OLED
(esp_lcd), LVGL via esp_lvgl_port (1-bit), `knob`/`button` ESP-IDF components for input,
paste-from-phone SoftAP web provisioning (esp_http_server, no on-screen typing), ESP-IDF native
dual-slot OTA (esp_https_ota + rollback), Settings app for startup/deep-sleep/timeout (battery-ready,
esp_pm), ESP-IDF v6.0.1 pinned.
