# TaskMaster-C3 — Firmware

ESP-IDF / FreeRTOS firmware for a XIAO ESP32-C3 desktop task appliance.
See [`PLAN.md`](PLAN.md) for the full spec. This README covers the current build.

## Status: Phase 2 — Provisioning ✅ (verified on hardware)

A self-contained **OS** for the device — a manifest-driven app framework with a Launcher, run by one
UI task — that now **provisions itself from a phone** and connects to Wi-Fi. Phases 0–2 are complete
and confirmed end to end on a XIAO ESP32-C3.

- **Paste-from-phone provisioning** — boot the device unprovisioned (or hold **Home** at reset) and it
  raises SoftAP `TaskMaster-Setup` and auto-launches the **Setup** app. Join from a phone, open
  `192.168.4.1`, and paste the whole config (Wi-Fi SSID/password, source URLs, tokens) into **one
  form** generated from the config schema, with an SSID picker from a live scan. Save → the device
  writes NVS, reboots, and connects. Nothing is typed on the knob.
- **Manifest-driven, self-registering apps** — each user app is its own component that registers
  itself (`TASKMASTER_REGISTER_APP`); the core never references it by name. The enabled set is one
  editable list, [`main/idf_component.yml`](main/idf_component.yml). **Core apps** (Launcher, Setup)
  are built in and non-removable.
- **App lifecycle on one task** — the UI task owns the active-app pointer and runs
  `init`/`on_event`/`render`/`exit` cooperatively, so app switches are race-free by construction.
  **Home** is OS-reserved: it always returns to the Launcher and teardown is total/idempotent (§6A).
- **Single Wi-Fi owner** (`wifi_mgr`) — coordinates STA / AP modes from one init; the Setup app can
  raise the AP **beside a live station link** (re-provision from the Launcher without dropping Wi-Fi).
- **Schema-driven config** (`nvs_config`) — one declarative table drives both the setup form and NVS
  read/write. Plus **per-app private storage** (`app_store`) so any app can persist its own variables
  with no core edits (see [`docs/APP_API.md`](docs/APP_API.md)).
- **Connectivity as an app API** (`net_status.h`) — apps read Wi-Fi state from one place
  (`net_status_get()` / `net_is_online()`) and the platform re-renders them on change; no app touches
  the radio.
- **Leak-clean teardown** — a Kconfig-gated harness (`CONFIG_TM_LEAK_TEST`) runs launch→Home→relaunch
  cycles under comprehensive heap poisoning; both apps pass on hardware (§6A.4 / §7A.7).

> The encoder is GPIO-polled (1 ms Ben-Buxton quadrature decode + button debounce) because the
> **ESP32-C3 has no PCNT peripheral**. The 1 ms poll moves to interrupt/GPIO-wake at Phase 4 (battery)
> — see the ⚠ note in [`PLAN.md`](PLAN.md) §14.

## Boot modes (§7A.3)

On boot the device branches on the `provisioned` flag, **Home-held-at-reset**, and the Wi-Fi toggle:

| Condition | Result |
|---|---|
| Home held at reset, **or** unprovisioned | Auto-launch **Setup** (SoftAP portal up) |
| Provisioned + Wi-Fi on | Connect as a **station**, boot to the Launcher |
| Provisioned + Wi-Fi off | Offline, boot to the Launcher |

Home-held-at-reset is the escape hatch to re-provision even with bad/stale credentials.

## Architecture

```
main/                          thin composition root: NVS → model → Wi-Fi init →
  main.c                       OLED → input → boot-mode branch → UI task
  idf_component.yml            APP MANIFEST — the editable list of user apps
components/taskmaster_core/    the OS: app framework + platform services
  app_manager / ui / launcher  registry, the UI task, the raw-rendered launcher
  app_setup                    Setup/Wi-Fi core app (provisioning portal lifecycle)
  wifi_mgr / softap_portal     Wi-Fi owner; captive HTTP form + DNS
  nvs_config / app_store       schema-driven device config; per-app private storage
  net_status / task_model      connectivity API; the shared task model (stub)
  sh1106 / input               OLED driver; GPIO encoder + button decode
  leak_test                    §6A.4 harness (CONFIG_TM_LEAK_TEST)
apps/app_hello/                example removable user app (demo)
```

User apps depend only on the public API + display and can live in their own repos. Adding/removing one
needs **no core edits**. App-author guide: [`docs/APP_API.md`](docs/APP_API.md).

## Coding conventions

- **No magic numbers.** Every numeric literal (pixel geometry, sizes, stack depths, timeouts, counts,
  buffer lengths) is a **named `#define`/enum in a header**, never an inline number in a `.c` file —
  only self-evident sentinels (`0`, `1`, `-1`) are exempt. Intent stays self-documenting and values
  are single-sourced/tunable in one place. See [`PLAN.md`](PLAN.md) §11.1.
- **One source of truth** per concern: wiring in `board_pins.h`, config in `nvs_config`, UI geometry in
  the UI headers — never duplicated.

## Wiring (XIAO ESP32-C3)

Single source of truth: [`components/taskmaster_core/include/board_pins.h`](components/taskmaster_core/include/board_pins.h).

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
Find `<PORT>` with `ls /dev/cu.*` (the XIAO's native USB shows as `/dev/cu.usbmodem*`).

### Provision the device

1. Power on unprovisioned (or hold **Home** at reset). The OLED shows the Setup screen.
2. Join Wi-Fi **`TaskMaster-Setup`** from a phone/laptop; open **http://192.168.4.1**.
3. Fill the form (at least Wi-Fi SSID + password) and **Save & Connect**. The device reboots and joins
   your network; the status bar shows `NET:OK`.

To re-provision later, open **Setup** from the Launcher (or hold **Home** at reset). A `factory reset`
is available via `config_factory_reset()`.

### Managing apps

Edit [`main/idf_component.yml`](main/idf_component.yml):
```yaml
dependencies:
  app_hello:
    path: ../apps/app_hello        # in-tree app
  # app_clock:
  #   git: https://github.com/somedev/tm-clock.git   # external app, own repo
```
Comment an entry out to disable that app (it won't compile). To disable **every** user app, write
`dependencies: {}` — an empty `dependencies:` key (all entries commented) is invalid YAML.

## Verify (Phase 2)

- [x] Unprovisioned / Home-held boot auto-launches **Setup**; SoftAP `TaskMaster-Setup` + `192.168.4.1`.
- [x] The form renders from the schema; the SSID field suggests scanned networks.
- [x] Saving writes NVS, reboots, and the device **associates with the real AP** (`got IP`, `NET:OK`).
- [x] Provisioned boot connects as a station; survives power cycle.
- [x] **Setup is re-enterable** from the Launcher and raises the AP without dropping a live STA link.
- [x] **Home** returns to the Launcher from any app; Launcher cursor wraps around the list.
- [x] **Leak-clean teardown** (§6A.4): launch→Home→relaunch cycles return the heap to baseline with
      heap poisoning on (Hello + Setup pass).

## Build status

**Compiles clean on ESP-IDF v6.0.1** for `esp32c3` (`idf.py build`) and **runs on hardware**. App image
≈ 871 KB — 54% free in the 1.9 MB OTA slot. Every successful build is auto-backed-up (timestamped) to
the gitignored `build_backups/`.

To run the leak harness (debug build, off by default):
```bash
idf.py -B build_leak -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" \
       -D SDKCONFIG=sdkconfig.leak build flash monitor
```

> ESP-IDF v6.0 note: the GPIO/I²C drivers live in `esp_driver_gpio` / `esp_driver_i2c` (the old
> monolithic `driver` component is gone) — reflected in the component `CMakeLists.txt` files.

## Next: Phase 3 — UI foundation + sync + Task Manager

First **adopt LVGL** as the UI foundation (port the Launcher/Setup/Hello screens to it,
screen-owned-widget lifecycle), then stand up the `yapp-server` proxy and the two source apps
("Yapp" / "Local") over one device REST contract: fetch tasks, render priority-sorted with nesting
(mirroring `todomark`), complete/postpone, and offline rendering when Wi-Fi is off. Full stepped plan
in [`PLAN.md`](PLAN.md) §8.5.
