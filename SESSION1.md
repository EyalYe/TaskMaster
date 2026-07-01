# TaskMaster-C3 — Session 1 handoff

A compacted handoff so work can resume on another machine **exactly from this point**.
Pair this with [`PLAN.md`](PLAN.md) (the authoritative spec) and
[`docs/agent-memory/`](docs/agent-memory/) (the assistant's persistent memory).

Written after **Phase 4 completed** (2026-07-01).

---

## 1. What this is

**TaskMaster-C3** — ESP-IDF / FreeRTOS firmware for a **XIAO ESP32-C3** desktop task
appliance: a **1.3" SH1106 128×64 mono OLED**, an **EC11 rotary encoder** (+ push), and
two buttons (**Select**, **Home**). It shows and acts on your tasks — **Todoist directly
over HTTPS** (the "Yapp" app) or any **LAN task server** (the "Local" app). It's a small
**OS**: a manifest-driven, self-registering app framework with a Launcher, run by one UI
task, that provisions itself from a phone.

Built CLI-only via **ESP-IDF v6.0.1** (`idf.py`) — no PlatformIO/IDE.

## 2. The three repos

| Repo | Remote | Role |
|---|---|---|
| **TaskMaster** (this) | `git@github.com:EyalYe/TaskMaster.git` | Core OS + `apps/app_hello` (in-tree demo) |
| **TM-YappLocal** | `git@github.com:EyalYe/TM-YappLocal.git` | "Local" task app (LAN contract) + its Python host server |
| **TM-YappCloud** | `git@github.com:EyalYe/TM-YappCloud.git` | "Yapp" task app (direct Todoist) + host |

On this machine the app repos are checked out at `~/yapplocal` and `~/yappcloud`. They
are **pulled into a firmware build automatically** via `git:` lines in
[`main/idf_component.yml`](main/idf_component.yml) (component manager fetches them into
`managed_components/`). So a clean build only needs **git/SSH access** to the two app
repos — you don't have to clone them manually.

**Exact state at handoff (all pushed):**
- TaskMaster `81b05b6`
- TM-YappLocal `2f3de2c`
- TM-YappCloud `5d992f0`

Core⟂userspace is a hard rule: core knows only the **stable app API** (`device_app_t` +
helper headers) and the **REST contract** (PLAN §8.1); it never names a specific app.

## 3. Status — Phases 0–4 complete, Phase 5 (core UX completion) next

| Phase | Milestone | State |
|---|---|---|
| 0–1 | Bring-up: OLED, encoder/buttons, app framework + Launcher on one UI task | ✅ |
| 2 | Paste-from-phone Wi-Fi provisioning (SoftAP captive form → NVS) | ✅ |
| 3 | LVGL UI + **tasks in userspace** (Yapp direct-Todoist, Local LAN), offline cache + replay, detail submenu, hide-when-unconfigured | ✅ |
| 4 | **Settings hub** (schema-driven editor + confirm dialog), device info, startup, brightness, timeout+blank, deep/light sleep, delete-per-app-data, restart, factory reset, per-app knobs, **OTA** | ✅ |
| 5 | **Core UX completion** — Launcher status bar (time/weather/connectivity) + glyph hint bar + core cohesion (§6C / §6C.1) | ◀ next |
| 6 | **External developers + platform** — `app_gpio` arbitration, per-app NVS budgets, sandboxing, Pomodoro example app, API versioning | planned |
| — | **Parked:** BLE provisioning (needs a phone app + Wi-Fi-only; a nicety over the no-app SoftAP form) | future |

Everything above is **verified on hardware**. Details + the stepped build orders are in
PLAN.md (Phase 3 = §8.5; Phase 4 = §8A.1).

## 4. Architecture (core, task-agnostic)

```
main/main.c                     composition root: NVS → Wi-Fi init → OLED → input →
                                boot-mode branch → startup target → UI task; OTA rollback-valid
components/taskmaster_core/     grouped by domain; each module's .c + .h co-located,
                                every subfolder on the include path (#include by name)
  platform/  sh1106 input lvgl_disp   OLED driver; GPIO encoder+button decode; LVGL→panel; board_pins.h
  ui/        ui launcher ui_frame ui_list hint_bar fonts   render task; Launcher (filters via
                                available()); OS frame + hint bar; generic scroll/select list
  app/       app_manager async_job    app registry (lifecycle + Home); background worker (off UI); app.h
  storage/   nvs_config app_store app_config   device config schema / per-app NVS ns / app-declared config
  net/       wifi_mgr softap_portal net_status   Wi-Fi owner; captive form + DNS + SSID scan; conn API
  settings/  app_settings settings_menu confirm   Settings hub; schema editor (TOGGLE/ENUM/RANGE/ACTION
                                w/ ctx); reusable yes/no modal
  test/      leak_test         §6A.4 launch→Home→relaunch harness (CONFIG_TM_LEAK_TEST)
apps/app_hello/                 in-tree demo app (also the leak/smoke-test canary; has an ACFG_KNOB "step")
```

**App API** (`app.h`): a `device_app_t { name, init, on_event, render, exit, available }`
self-registers via `TASKMASTER_REGISTER_APP`. `available()` (optional) hides an app from
the Launcher until it's configured. Background I/O goes through `async_job` (results
delivered on the UI task → app models need no mutex). **Home** is OS-reserved.

**Task apps** (in the app repos) share a header-only `tasks.h` (identical copy in each):
`task_t[]` model, `ui_list`-based render, offline cache (NVS blob), bounded write queue
with poison guard, and the detail submenu (Details/Postpone/Sync now/Back).

## 5. Key design decisions (why things are the way they are)

- **Tasks are userspace.** Core has no task concept — only the generic `ui_list` +
  `async_job`. The two task apps live in their own repos (§2).
- **No magic numbers.** Every numeric literal is a named `#define`/enum in a header. See
  `docs/agent-memory/no-magic-numbers.md` + PLAN §11.1. (Enforced throughout.)
- **Plan before code.** New subsystems get written into PLAN.md first. See
  `docs/agent-memory/workflow-plan-before-code.md`.
- **Schema-driven Settings.** One editor (`settings_menu`) renders core settings *and*
  per-app `ACFG_KNOB` from a declared table — adding a setting is a table row, not a
  screen. `get/set/action` carry a `void *ctx` (per-app rows point it at `{ns,field}`).
- **Yapp = direct Todoist** (no proxy): `GET/POST https://api.todoist.com/api/v1/tasks…`
  (the old `/rest/v2/` is **410 Gone**). `{"results":[...]}` is unwrapped. cJSON
  (`espressif/cjson`, not bundled in IDF v6).
- **Cooperative async cancel — never cross-thread client teardown.** The HTTP client
  handle is **worker-local**; cancel is a flag the worker polls. (An earlier
  abort-hook that closed the client from the UI thread **crashed** on Home-mid-fetch —
  esp_http_client isn't thread-safe. Fixed. Don't reintroduce it — see `async_job.h`.)
- **Deep/light sleep is opt-in (default off).** Manual `esp_light_sleep` on idle with
  GPIO wake. Full Wi-Fi-retained *automatic* light sleep needs the 1 ms input poll
  converted to interrupt/GPIO-wake (deferred — see §8A / §4.6).

## 6. The Settings menu (Phase 4 result)

`Wi-Fi` (toggle) · `Setup` (full provisioning form) · `Device info` · `Startup` (ENUM) ·
`Brightness` (RANGE %) · `Timeout` (ENUM: Off/30s/1m/5m/15m) · `Deep sleep` (toggle) ·
`Check update` (OTA, confirm) · `Delete data` (pick Wi-Fi or an app → confirm → wipe) ·
`Restart` (confirm) · `Factory reset` (confirm) · then each app's `ACFG_KNOB` rows
(e.g. `Hello step`). Sub-screens: MODE_INFO / MODE_PORTAL / MODE_DELETE / MODE_OTA.

## 7. Build / flash / test workflow

```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.1.sh"   # once per shell
idf.py set-target esp32c3          # first time only (or after deleting sdkconfig)
idf.py build
idf.py -p <PORT> flash monitor     # <PORT> = ls /dev/cu.usbmodem*
```

Leak harness (debug build, heap poisoning):
```bash
idf.py -B build_leak -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" \
       -D SDKCONFIG=sdkconfig.leak build flash monitor
```

**Gotchas learned this session (important):**
- **LVGL CMakePresets wedge:** the managed LVGL component drops a `CMakePresets.json`
  that breaks `idf.py` configure. Before building: `rm -f
  managed_components/lvgl__lvgl/CMakePresets.json`.
- **Re-fetch app repos** after changing them: `rm -rf managed_components/tm_yapp
  managed_components/tm_local dependencies.lock` then build.
- **pyserial** isn't on the default `python3`; use the IDF venv:
  `/Users/yeminie/.espressif/tools/python/v6.0.1/venv/bin/python`.
- **Deep sleep ⇒ flashing needs download mode.** Native USB drops during light sleep, so
  esptool's auto-reset fails ("No serial data received"). Put the board in download mode:
  **hold BOOT, tap RESET, release BOOT**, then flash. (Or just keep Deep sleep off.)
- **Deleting `sdkconfig` resets the target to esp32** — re-run `idf.py set-target esp32c3`
  (which re-applies `sdkconfig.defaults`, incl. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`).
- **ESP32-C3 is 2.4 GHz-only** — 5 GHz SSIDs won't appear in the provisioning scan.
- **Provisioning password field is blank every load** (never pre-filled); on re-provision
  you must re-type it or you save an empty password.
- **Every successful build auto-backs-up** to gitignored `build_backups/<timestamp>/` via
  a CMake POST_BUILD hook (`tools/backup_build.sh`) — don't remove it.
- **Commit conventions:** core commits end with
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`; app-repo commits use
  `🤖 Generated with [Claude Code](https://claude.com/claude-code)`. Commit/push only when
  asked; branch off `main` for PRs.

**OTA test recipe:** serve the build over HTTP from a machine on the device's LAN
(`python -m http.server 8000 --directory build`), set **Settings → Setup → OTA firmware
URL** to `http://<host-ip>:8000/taskmaster_c3.bin`, then **Settings → Check update →
Yes**. It writes the spare slot, reboots into it, and self-confirms (rollback on failure).

## 8. Device NVS state at handoff (this physical board)

- **Provisioned** and joined the home Wi-Fi (`192.168.1.x`; the board got `192.168.1.172`).
- **`fw_url`** currently points at a now-stopped temp OTA server — "Check update" will fail
  until it's re-pointed or cleared (harmless).
- Running from the **OTA'd slot `ota_1`** (next USB flash overwrites `ota_0` and boots it).
- **Todoist token** was displayed in terminal earlier in the session — consider rotating it.

## 9. Resuming on a new computer

1. `git clone git@github.com:EyalYe/TaskMaster.git` (SSH access to the two app repos too).
2. Install **ESP-IDF v6.0.1** (the project pins it; activate script path in §7).
3. Restore the assistant memory: see [`docs/agent-memory/README.md`](docs/agent-memory/README.md).
   Fastest bootstrap: paste [`docs/agent-memory/CONT_PROMPT.md`](docs/agent-memory/CONT_PROMPT.md)
   as the first message to a fresh Claude Code session.
4. `idf.py set-target esp32c3 && idf.py build`, then flash the board.
5. Read this file + `PLAN.md` (§8A.1 for Phase 4 detail, roadmap table near the end).
6. **Next up: Phase 5 — core UX completion** (PLAN §6C / §6C.1): the **Launcher status
   bar** (connectivity glyph + NTP time + weather, online + city set), the **glyph hint
   bar** (the `icons/` assets replace the ≤3-char text labels), and a **cohesion pass**
   tying core together. BLE provisioning is **parked** (needs a dedicated phone app +
   carries only Wi-Fi creds — a nicety over the no-app SoftAP form).

## 10. Pointers

- **Spec / source of truth:** [`PLAN.md`](PLAN.md)
- **User-facing overview:** [`README.md`](README.md) (milestone-level; updated per phase)
- **App-author guide:** [`docs/APP_API.md`](docs/APP_API.md)
- **Assistant memory:** [`docs/agent-memory/`](docs/agent-memory/)
