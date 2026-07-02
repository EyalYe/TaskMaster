# TaskMaster-C3 — Firmware

ESP-IDF / FreeRTOS firmware for a XIAO ESP32-C3 desktop task appliance — a 1.3" OLED, a rotary
encoder, and two buttons that show and act on your tasks (Todoist directly, or any LAN task server).
See [`PLAN.md`](PLAN.md) for the full spec. Resuming work / new machine? Start with
[`SESSION1.md`](SESSION1.md) (handoff) and [`docs/agent-memory/`](docs/agent-memory/).

## Milestones

The project advances in phases; this README tracks the **latest completed phase**, not day-to-day
progress. The current stepped plan lives in [`PLAN.md`](PLAN.md).

| Phase | Milestone | Status |
|---|---|---|
| **0–1** | Bring-up: SH1106 OLED, EC11 encoder + buttons, and a manifest-driven app framework + Launcher on one UI task | ✅ |
| **2** | **Paste-from-phone provisioning** — SoftAP captive form writes Wi-Fi + app config to NVS | ✅ |
| **3** | **LVGL UI + tasks in userspace** — Todoist (direct HTTPS) and LAN task apps; offline cache + write replay; the Settings hub | ✅ |
| **4** | **Settings hub** (schema-driven) + power/idle (blank, light sleep) + **OTA update** | ✅ |
| **5** | **Core UX completion** — Launcher status bar (time · weather · connectivity, °C/°F) + glyph hint bar + keyless weather/time + in-browser LAN config + cohesion | ✅ |
| **6** | **External developers + platform** — GPIO arbitration, per-app NVS budgets, sandboxing, example app, API versioning, zero-toolchain onboarding | ◀ next |

*(BLE provisioning is parked — a nicety over the no-app SoftAP form; revisit if iOS setup becomes a pain.)*

## What the device does today (Phase 5)

A self-contained **OS** for the device: a manifest-driven app framework with a Launcher, run by one UI
task, that provisions itself from a phone, connects to Wi-Fi, and runs task apps against a real
backend — all verified on a XIAO ESP32-C3.

- **At-a-glance status bar + weather/time.** The Launcher's bottom strip shows a **connectivity glyph**
  (Wi-Fi / no-Wi-Fi), **local time**, **temperature** (°C/°F, Settings toggle), and a **weather glyph**
  (sun / partly-cloudy day·night / rain / snow). Time is NTP; weather + timezone come from a **keyless**
  Open-Meteo lookup keyed by a `city` field — no API key or signup. The **hint bar** renders 1-bit
  glyphs for conventional actions (done ✓, open, menu, reset) with text as the fallback.
- **Edit config in a browser (no re-provisioning).** Toggle **Settings → Web config** and open the
  device's LAN IP (shown in Device info) to edit any field — city, tokens, URLs — in a pre-filled form;
  most edits apply **live** (only a Wi-Fi change reboots).

- **Real tasks, two sources.** A **Yapp** app talks to **Todoist directly over HTTPS** (no proxy); a
  **Local** app speaks a small LAN REST contract. Both render priority-sorted, nested tasks; **Select**
  completes one-tap; **encoder-click** opens a per-task detail submenu (**due date + on-demand
  description / Postpone / Sync now**). Tasks are **userspace** — core stays task-agnostic.
- **Works offline.** Wi-Fi off or a dropped link → the last tasks render from an on-device cache with an
  **OFFLINE** banner; completions are **queued and replayed** on reconnect (with a poison-entry guard).
- **The Settings hub.** A non-removable core app driven by one **schema-driven editor** (core settings
  and per-app knobs share it): Wi-Fi on/off, Setup (re-provision), device info, startup app, OLED
  brightness, inactivity timeout (screen blank) + deep/light sleep, delete-per-app data, restart,
  factory reset, and **OTA update** (`esp_https_ota` from `fw_url`, with bootloader rollback).
- **Paste-from-phone provisioning.** Boot unprovisioned (or hold **Home** at reset) → SoftAP
  `TaskMaster-Setup`; open `192.168.4.1` and paste the whole config into **one** schema-generated form
  (Wi-Fi + each app's declared fields). Nothing is typed on the knob.
- **Manifest-driven, self-registering apps.** Each user app is its own component (often its own repo)
  that registers itself; core never names it. Apps **hide from the Launcher until configured**. The
  enabled set is one editable list, [`main/idf_component.yml`](main/idf_component.yml).
- **One UI task owns everything.** `init`/`on_event`/`render`/`exit` run cooperatively, so app switches
  are race-free; background I/O goes through a core `async_job` worker (results delivered back on the UI
  task — app models need no mutex). **Home** is OS-reserved and teardown is total/idempotent (§6A).
- **Leak-clean + crash-hardened.** The launch→Home→relaunch harness passes on all apps under
  comprehensive heap poisoning; the Home-mid-fetch cancel path is cooperative (no cross-thread client
  teardown).

> The encoder is GPIO-polled (1 ms Ben-Buxton quadrature decode + button debounce) because the
> **ESP32-C3 has no PCNT peripheral**. That 1 ms poll converts to interrupt / GPIO-wake in Phase 4 to
> enable light sleep — see [`PLAN.md`](PLAN.md) §8A.

## Boot modes (§7A.3)

On boot the device branches on the `provisioned` flag, **Home-held-at-reset**, and the Wi-Fi toggle:

| Condition | Result |
|---|---|
| Home held at reset, **or** unprovisioned | Auto-open **Settings → Wi-Fi setup** (SoftAP portal up) |
| Provisioned + Wi-Fi on | Connect as a **station**, boot to the Launcher |
| Provisioned + Wi-Fi off | Offline (cached tasks), boot to the Launcher |

Home-held-at-reset is the escape hatch to re-provision even with bad/stale credentials.

## Architecture

```
main/                          thin composition root: NVS → Wi-Fi init → OLED →
  main.c                       input → boot-mode branch → UI task
  idf_component.yml            APP MANIFEST — the editable list of user apps
components/taskmaster_core/    the OS: app framework + platform services (task-agnostic).
                               Sources grouped by domain; each module's .c + .h sit together,
                               every subfolder on the include path (#include by name).
  platform/  sh1106 input      OLED driver; GPIO encoder + button decode; LVGL→panel glue;
             lvgl_disp         board_pins.h (single source of pin truth)
  ui/        ui launcher       the UI/render task; the Launcher (filters unconfigured apps);
             ui_frame ui_list  OS frame + hint bar; generic scrollable/selectable list; fonts
  app/       app_manager       app registry; async_job background worker (off the UI task); app.h
             async_job
  storage/   nvs_config        schema-driven device config; per-app private storage;
             app_store app_config   app-declared config fields (form + Settings), core names no app
  net/       wifi_mgr          Wi-Fi owner; provisioning form + DNS (SoftAP) + LAN config
             softap_portal net_status   page (station IP); connectivity API; wx = NTP time +
             wx                keyless Open-Meteo weather/timezone
  settings/  app_settings      the Settings hub app; its schema editor + confirm dialog
             settings_menu confirm
  test/      leak_test         §6A.4 leak harness (CONFIG_TM_LEAK_TEST)
apps/app_hello/                in-tree example user app (demo / leak-test canary)
```

Task apps live in their **own repos** (`TM-YappLocal`, `TM-YappCloud`) and depend only on the public
app API + display — core and apps version independently. Adding/removing an app needs **no core edits**.
App-author guide: [`docs/APP_API.md`](docs/APP_API.md).

## Coding conventions

- **No magic numbers.** Every numeric literal (pixel geometry, sizes, stack depths, timeouts, counts,
  buffer lengths) is a **named `#define`/enum in a header**, never an inline number in a `.c` file —
  only self-evident sentinels (`0`, `1`, `-1`) are exempt. See [`PLAN.md`](PLAN.md) §11.1.
- **One source of truth** per concern: wiring in `board_pins.h`, config in `nvs_config`, UI geometry in
  the UI headers — never duplicated.
- **Core ⟂ userspace.** Core knows the app API + REST contract, never a specific app; task apps and
  their config live entirely in their own repos.

## Wiring (XIAO ESP32-C3)

Single source of truth: [`components/taskmaster_core/platform/board_pins.h`](components/taskmaster_core/platform/board_pins.h).

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
rm -f managed_components/lvgl__lvgl/CMakePresets.json   # see gotcha below
idf.py build                           # build
idf.py -p <PORT> flash monitor         # flash over USB-C + open serial @ 115200
```
Find `<PORT>` with `ls /dev/cu.*` (the XIAO's native USB shows as `/dev/cu.usbmodem*`).

**Gotchas (all hit in practice):**
- **LVGL build error** — the managed `lvgl/lvgl` component drops a stray `CMakePresets.json`
  that breaks the build; delete it before each `idf.py build` (the line above). Harmless if absent.
- **Port doesn't appear / `flash` can't connect** — the C3's **native USB drops when the device is
  in light/deep sleep** (only `/dev/cu.Bluetooth*` etc. show). Put it in **download mode**: hold
  **BOOT**, tap **RESET**, release **BOOT**, then flash. (While iterating, `Settings → Deep sleep → Off`
  avoids this.) Re-check with `ls /dev/cu.*`.
- **Wrong target after deleting `sdkconfig`** — it resets to `esp32`; re-run `idf.py set-target esp32c3`.
- **Changed an app repo's code** — re-fetch it so the build isn't stale:
  `rm -rf managed_components/tm_* dependencies.lock` then rebuild.
- **`pyserial` for ad-hoc scripts** lives in the IDF venv:
  `/Users/yeminie/.espressif/tools/python/v6.0.1/venv/bin/python`.

### Provision the device

1. Power on unprovisioned (or hold **Home** at reset). The OLED opens **Settings → Wi-Fi setup**.
2. Join Wi-Fi **`TaskMaster-Setup`** from a phone/laptop; open **http://192.168.4.1**.
3. Fill the form (at least Wi-Fi SSID + password, plus any app's token/URL) and **Save & Connect**. The
   device reboots and joins your network.

To re-provision later, open **Settings → Wi-Fi setup** from the Launcher (or hold **Home** at reset).

### Managing apps

Apps are pulled into a build via the manifest [`main/idf_component.yml`](main/idf_component.yml) — core
and apps version independently (PLAN §11.2):

```yaml
dependencies:
  app_hello:                          # in-tree example (this repo)
    path: ../apps/app_hello
  tm_local:                           # external app from its own repo, app/ subdir
    git: git@github.com:EyalYe/TM-YappLocal.git
    path: app
```

The component manager fetches the remote into `managed_components/`, and the app **self-registers** —
**no core edits**. Comment an entry out to remove that app; to disable **every** app, write
`dependencies: {}` (an empty `dependencies:` key is invalid YAML). Each app declares its own config
(URLs/tokens) via `TASKMASTER_REGISTER_APP_CONFIG`, so core never hardcodes app fields (PLAN §9.4).

> A remote/private app repo is fetched over SSH at configure time, so a clean build needs git access to
> it (cached in `managed_components/` + pinned in `dependencies.lock` afterward).

## Build status

**Compiles clean on ESP-IDF v6.0.1** for `esp32c3` and **runs on hardware**. App image ≈ **1.3 MB** —
~31% free in the 1.9 MB OTA slot. Every successful build is auto-backed-up (timestamped) to the
gitignored `build_backups/`.

To run the §6A.4 leak harness (debug build, off by default; heap poisoning on):
```bash
idf.py -B build_leak -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" \
       -D SDKCONFIG=sdkconfig.leak build flash monitor
```

> ESP-IDF v6.0 note: the GPIO/I²C drivers live in `esp_driver_gpio` / `esp_driver_i2c` (the old
> monolithic `driver` component is gone) — reflected in the component `CMakeLists.txt` files.
