# Project context

## What it is

**TaskMaster-C3** is ESP-IDF firmware for a small desktop task appliance built on a **Seeed XIAO
ESP32-C3**: a 1.3" **SH1106** 128×64 mono OLED, an **EC11 rotary encoder** (rotate + push), and two
buttons (**Select**, **Home**). It shows and acts on your tasks (Todoist directly over HTTPS, or any
LAN task server) and doubles as a tiny, extensible **OS** you can write apps for.

The device is a self-contained OS: a manifest-driven app framework + Launcher on one UI task, that
provisions itself from a phone, connects to Wi-Fi, runs task apps against real backends, shows a status
bar (time · weather · connectivity), works offline, and updates over the air.

## Repo topology (all public on github.com/EyalYe)

| Repo | Role | Notes |
|---|---|---|
| **TaskMaster** (this repo) | The **sealed OS core** + the core-dev project | `components/taskmaster_core` is the reusable OS component, pulled by others as a **git dependency**. Also builds standalone for core development. |
| **TM-Template** | The **fork target** for app developers | Thin project: `apps.yaml`, compose hook, `app_skeleton`, CI, local tools. Pulls `taskmaster_core` via git. Devs fork **this**, never core. |
| **TM-YappLocal** / **TM-YappCloud** | Example task apps | Each is a self-registering app component in its own repo (device app in the `app/` subdir). |

**The immutability model:** app authors fork **TM-Template** and edit only **`apps.yaml`** (+ their own
app folder). `taskmaster_core` is a pinned git dependency they never fork or edit. The public headers in
`docs/APP_API.md` are the *entire* contract.

## Core component layout (`components/taskmaster_core/`)

Sources are grouped by domain; each module's `.c`+`.h` sit together and every subfolder is on the
include path (so `#include "foo.h"` resolves by name).

- **`platform/`** — `sh1106` OLED driver, `input` (GPIO encoder/button decode), `lvgl_disp` (LVGL→panel
  glue), `board_pins.h` (single source of pin truth).
- **`ui/`** — `ui` (the single UI/render task), `ui_frame` (OS frame + the 3-box hint bar), `ui_list`
  (generic scrollable list), `launcher` (app list + status bar), `hint_glyphs` (generated 1-bit icons),
  fonts.
- **`app/`** — `app_manager` (registry + lifecycle), `async_job` (background worker), `taskmaster`
  (`taskmaster_run()` — the OS bootstrap/entry point), `app.h` (the `device_app_t` interface).
- **`storage/`** — `nvs_config` (schema-driven device config), `app_store` (per-app private NVS),
  `app_config` (app-declared config fields for the form + Settings).
- **`net/`** — `wifi_mgr` (Wi-Fi owner), `softap_portal` (provisioning form + captive DNS + the LAN
  config web page), `net_status` (connectivity API + UI notify), `wx` (NTP time + keyless Open-Meteo
  weather/timezone).
- **`settings/`** — `app_settings` (the Settings hub app), `settings_menu` (schema-driven editor),
  `confirm` (yes/no modal).
- **`test/`** — `leak_test` (§6A.4 harness, debug builds).

**Bootstrap:** `app_main()` (in the project's `main/main.c`, a 3-line stub) calls `taskmaster_run()` in
core. All init lives in core, so the fork template contains no OS logic.

## Key architecture facts (that bite if you don't know them)

- **One UI task owns everything.** `init`/`on_event`/`render`/`exit` run cooperatively on a single task
  that is the *only* thread touching LVGL — no mutexes. Background I/O goes through the core `async_job`
  worker; results are delivered back **on the UI task**.
- **async_job cancel is cooperative (flag only).** Never tear down a non-thread-safe handle (e.g.
  `esp_http_client`) from another thread — it crashed before. The worker owns its handle and polls
  `async_job_cancelled()`.
- **Home is OS-reserved.** It always returns to the Launcher; apps never see it. Teardown is total +
  idempotent (§6A).
- **Apps hide until configured.** An app's `available()` returning false keeps it off the Launcher.
- **Composition:** `apps.yaml` → `tools/compose_apps.py` (run by a hook in the root `CMakeLists.txt`
  before the component manager) → generated, git-ignored `main/idf_component.yml`. Editing `apps.yaml`
  reconfigures (via `CMAKE_CONFIGURE_DEPENDS`).
- **Todoist API:** `/api/v1/tasks` (the old `rest/v2` is 410 Gone); returns `{"results":[...]}`.
- **Weather/time:** `wx` service — `esp_sntp` for UTC + keyless Open-Meteo (geocode the `city` config →
  lat/lon, forecast → temp + WMO code + `utc_offset_seconds`); local = UTC + offset.

## Hardware pinout (`platform/board_pins.h` is authoritative)

| Signal | Pad | GPIO |  | Signal | Pad | GPIO |
|---|---|---|---|---|---|---|
| OLED SDA | D4 | 6 |  | Encoder switch | D2 | 4 |
| OLED SCL | D5 | 7 |  | Select button | D3 | 5 |
| Encoder A | D0 | 2 |  | Home button | D10 | 10 |
| Encoder B | D1 | 3 |  | | | |

Active-low to GND via internal pull-ups; OLED I²C `0x3C`. D6–D9 are **free for app hardware** (D8/D9 are
strapping pins). ESP32-C3 is 2.4 GHz-only and has **no PCNT** (the encoder is 1 ms GPIO-polled).
Partitions: 4 MB flash, dual-slot OTA (`ota_0` @ 0x20000, `ota_1` @ 0x200000).

## Status (2026-07-02)

- **Phases 0–5 complete + verified on hardware:** bring-up, core OS, LVGL UI + tasks/offline, Settings
  hub + power/OTA, and core-UX (glyph hint bar, weather/time, status bar, LAN config page).
- **Phase 6 in progress (§6E build order):** ✅ app composition (`apps.yaml`), ✅ §6D onboarding
  (TM-Template + CI + local flash/OTA tools, LAN OTA verified). Remaining: app-API versioning (semver),
  per-app NVS budgets + namespace hardening, a documented "GPIO is the app's risk" note, a Pomodoro
  example app.

See `PLAN.md` §14 for the phase table and §6C.1/§6E for the stepped build logs.
