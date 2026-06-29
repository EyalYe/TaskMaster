# TaskMaster-C3 — Firmware

ESP-IDF / FreeRTOS firmware for a XIAO ESP32-C3 desktop task appliance.
See [`PLAN.md`](PLAN.md) for the full spec. This README covers the current build.

## Status: Phase 1 — Core OS ✅ (verified on hardware)

A self-contained **OS** for the device: a manifest-driven app framework with a Launcher, run by a
single UI task. Phases 0–1 are complete and confirmed on a XIAO ESP32-C3.

- **Manifest-driven, self-registering apps** — each app is its own component that registers itself
  (`TASKMASTER_REGISTER_APP`); the core never references an app by name. The enabled set is one
  editable list, [`main/idf_component.yml`](main/idf_component.yml). Comment a line out and the app
  isn't built, linked, or shown.
- **Launcher** — raw-rendered (SH1106) scrollable app list with a cursor, a status bar (Wi-Fi + sync,
  read from the shared model), and a contextual control-hint line. *(LVGL is deferred to Phase 3.)*
- **App lifecycle on one task** — the UI task owns the active-app pointer and runs
  `init`/`on_event`/`render`/`exit` cooperatively, so app switches are race-free by construction.
- **Home is OS-reserved** — the dedicated Home button always returns to the Launcher and never reaches
  an app; teardown is total/idempotent (see [`PLAN.md`](PLAN.md) §6A).
- **Shared-model skeleton** — one mutex-guarded `task_model_t` (writer/reader ownership boundary built
  now; populated by the network task in Phase 3).
- **Provisioning AP still up** — SoftAP `TaskMaster-Setup` + HTTP server (the real config form lands
  in Phase 2).

> The encoder is GPIO-polled (1 ms Ben-Buxton quadrature decode + button debounce) because the
> **ESP32-C3 has no PCNT peripheral**. The 1 ms poll moves to interrupt/GPIO-wake at Phase 4 (battery)
> — see the ⚠ note in [`PLAN.md`](PLAN.md) §14.

## Architecture

```
main/                      thin composition root (app_main) + the app manifest
  main.c                   NVS → model → OLED → input → SoftAP, then hands off to the UI task
  idf_component.yml        APP MANIFEST — the editable list of enabled apps
components/taskmaster_core/ the OS: app framework + platform services
apps/app_hello/            first self-registering app component (demo)
```

Apps depend only on the public app API + display; they can live in their own repos and be pulled by a
`git:` line in the manifest. Adding/removing an app needs **no core edits**.

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

### Managing apps

Edit [`main/idf_component.yml`](main/idf_component.yml):
```yaml
dependencies:
  app_hello:
    path: ../apps/app_hello        # in-tree app
  # app_clock:
  #   git: https://github.com/somedev/tm-clock.git   # external app, own repo
```
Comment an entry out to disable that app (it won't compile). To disable **every** app, write
`dependencies: {}` — an empty `dependencies:` key (all entries commented) is invalid YAML.

## Verify (Phase 1)

- [x] Serial boot log shows `Registered apps: N` then each `app[i] = <name>`, and `Phase 1 up.`
- [x] OLED shows the **Launcher**: `TASKMASTER`, the app list with a `>` cursor, status bar, hint line.
- [x] Encoder moves the cursor; Select / encoder-push enters the highlighted app.
- [x] Inside the demo app, turning the knob changes its `COUNT`; Select/push resets it.
- [x] **Home** returns to the Launcher from inside any app (serial: `HOME -> Launcher` + app `exit`).
- [x] Commenting the app's manifest line (or `dependencies: {}`) removes it from the build.
- [ ] Leak-clean teardown cycle (PLAN §6A.4) — deferred to **Phase 2**: stood up on a heap-poisoning
      debug build, then a standing per-app gate.

## Build status

**Compiles clean on ESP-IDF v6.0.1** for `esp32c3` (`idf.py build`) and **runs on hardware**. App image
≈ 876 KB — 55% free in the 1.9 MB OTA slot. Every successful build is auto-backed-up (timestamped) to
the gitignored `build_backups/`.

> ESP-IDF v6.0 note: the GPIO/I²C drivers live in `esp_driver_gpio` / `esp_driver_i2c` (the old
> monolithic `driver` component is gone) — reflected in the component `CMakeLists.txt` files.

## Next: Phase 2 — provisioning portal

Replace the placeholder SoftAP page with the paste-from-phone setup form that writes Wi-Fi creds +
source URLs + tokens to NVS in one paste. See [`PLAN.md`](PLAN.md) §7.
