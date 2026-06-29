# TaskMaster-C3 — Full Build Plan & Technical Specification

**Codename:** TaskMaster-C3
**Category:** Single-purpose desktop productivity appliance (ESP-IDF / FreeRTOS / ESP32-C3)
**Status:** Planning — v0.4 (re-based on ESP-IDF; all key decisions resolved; build-ready)
**Last updated:** 2026-06-29

> This document supersedes the initial Gemini PRD draft and the earlier Zephyr-based revisions.
> It keeps the product vision, hardware, and every resolved decision, but targets **ESP-IDF**
> (Espressif's native C framework on FreeRTOS) instead of Zephyr — chosen because the product's
> core features (paste-from-phone SoftAP provisioning, captive portal, HTTP server, OTA) are
> first-class, battle-tested ESP-IDF components rather than the immature path they were on Zephyr.
> Build tooling is the **ESP-IDF CLI** (`idf.py`) — CLI-only workflow, no IDE wrapper.
> Sections marked **⚠** flag the remaining things to watch; all major decisions are in §16.

---

## 1. Product Vision

A fully enclosed, zero-distraction desktop appliance that pulls, displays, and manages tasks from a
productivity backend, driven entirely by a rotary encoder and two buttons on a 1.3" OLED. It is
deliberately *not* a phone widget: single purpose, always-on, instant UI. An extensible local app
framework lets developers add secondary micro-apps (Pomodoro, system monitor, macro pad) without
touching the core OS or drivers.

**Design pillars:** instant input → render latency; network work fully decoupled and invisible;
new apps added by implementing one interface struct, nothing else.

---

## 2. Scope, MVP & Non-Goals

### MVP (v1.0) — must work end to end
- Boot → OS Launcher → select app.
- **Paste-from-phone provisioning (SoftAP + web form)** — the device hosts a setup page; the user
  pastes the *entire* config (Wi-Fi SSID/password, source URLs, tokens) from a phone/laptop in one
  form. **No config is hardcoded, nothing is typed letter-by-letter on the knob** (§7).
- **Task Manager** as two source apps over one device contract: "Yapp" (`yapp-server`/Todoist) and
  "Local" (platform-agnostic LAN box) — same code, different configured endpoint (§8).
- Persist all config in NVS via the provisioning form only; survive power cycle.
- Background sync with a visible sync/Wi-Fi status indicator.
- App framework with ≥2 apps proving the interface (Launcher + Task Manager).

### Phase 2 — after MVP is stable
- **BLE provisioning** as a second transport (trivial alongside SoftAP in ESP-IDF) (§7).
- Direct Todoist integration on-device (bypass the proxy) — see §8.
- Third demo app (Pomodoro) to validate framework extensibility for outside developers.

### Non-Goals (v1)
- Multi-account / multiple simultaneous backends per source.
- Offline task editing with conflict resolution (writes go to the backend or are queued simply).
- Battery operation as *hardware* (device is USB-C powered). But the **Settings + sleep/timeout
  scaffolding is built now** (§8A) so the future battery build is a hardware change, not a rewrite;
  v1 uses the timeout for screen-blank and ships light-sleep behind the toggle.
- BLE features beyond optional provisioning.

---

## 3. Hardware Architecture & BOM

```
        5V USB-C ─► XIAO ESP32-C3 (RISC-V @160MHz, 400KB SRAM, 4MB flash, Wi-Fi b/g/n + BLE5)
                          │
            I2C (400kHz)  ├─► 1.3" OLED 128×64  (SH1106)
            GPIO + IRQ    ├─► EC11 rotary encoder (A/B quadrature + push switch → app-usable)
            GPIO          ├─► Select button   (tactile, internal pull-up → app-usable)
            GPIO          └─► Home button     (tactile, internal pull-up → dedicated: return to Launcher)
```

### Input roles
Three buttons, two roles:
- **App-usable (passed to the active app):** encoder rotate (CW/CCW), encoder push (click), **Select** button.
- **Reserved by the OS (never reaches the app):** **Home** button — always returns to the Launcher
  from anywhere, intercepted by `app_manager` before app dispatch. The one input apps cannot override.

### Core Components

| Component | Part | Notes |
|---|---|---|
| MCU | Seeed XIAO ESP32-C3 | Single-core RISC-V, 400KB SRAM, 4MB flash, ext. antenna connector |
| Display | 1.3" OLED 128×64 | **SH1106** — `esp_lcd` panel driver (or registry SH1106 component); **2-px column offset** (the #1 blank/garbled-screen cause) |
| Rotary input | EC11 encoder + switch | 20 detents/rev; A/B + SW = 3 GPIO; push = app-usable. ESP-IDF `knob` component |
| Buttons | 2× tactile momentary | **Select** (app-usable) + **Home** (OS-reserved); internal pull-ups; ESP-IDF `button` component |
| Enclosure | 3D-printed, 15–20° wedge | Exposes screen, knob, 2 buttons only |

**Pin budget:** OLED (SDA/SCL = 2), encoder (A/B/SW = 3), buttons (2) = **7 GPIO** + power. XIAO
ESP32-C3 exposes 11 GPIO — comfortable headroom.

**⚠ SRAM is the real constraint, not GPIO.** 400KB SRAM holds the Wi-Fi stack, optionally an
mbedTLS session, the JSON buffer, and LVGL. This drives several decisions below (proxy architecture,
LVGL trim, HTTP-on-LAN default). If it ever bites, the pin-compatible **XIAO ESP32-S3** (512KB SRAM
+ 8MB PSRAM) is a drop-in escape hatch with no plan changes.

---

## 4. Firmware & Software Architecture (ESP-IDF / FreeRTOS)

ESP-IDF gives us FreeRTOS, a managed-component dependency system, native Wi-Fi/SoftAP/HTTP/OTA/NVS,
and official LVGL + `knob`/`button` components. Peripherals are configured in code + `menuconfig`
(Kconfig), with a single board-pins header keeping wiring out of business logic.

### 4.1 Direct HTTPS to Todoist is the hardest part, not the display
Todoist requires TLS 1.2+ with SNI and a CA bundle, and returns large nested JSON. On 400KB SRAM an
`esp-tls` session + a full response + cJSON parsing is tight. **Recommendation:** a thin **companion
proxy** (`yapp-server`) is the primary data path (§8); the device talks to *your* small REST server
over plain HTTP on the LAN. Direct on-device Todoist becomes a stretch goal, not a v1 dependency.

### 4.2 Render on demand, not at a fixed 20 FPS
The UI is mostly static. A full 128×64 mono frame over I2C@400kHz is ~23ms; pushing that 20×/sec
wastes CPU/bus and risks OLED wear. **Render only when state changes** (input event or sync update),
plus a timed tick *only* while an animation is active (horizontal text scroll). LVGL's
`lv_timer_handler` is pumped on events, not free-run.

### 4.3 Input via ESP-IDF `knob` + `button` components
Espressif publishes managed components for exactly our hardware: **`knob`** (rotary encoder via
PCNT/GPIO) and **`button`** (debounce, click/long-press events). They emit callbacks; an input task
translates those into our `EV_*` enums and posts them to a FreeRTOS queue. No hand-rolled IRQ
debounce. (Home is tagged specially — see §4.6/§5.2.)

### 4.4 UI library: LVGL (decided)
**LVGL** via the official `esp_lvgl_port` component, on the SH1106 panel through `esp_lcd`. Cost is
real on 400KB SRAM + Wi-Fi, so budget for it:
- **1-bit (`I1`) color depth** matched to the mono panel; one **partial** draw buffer (a fraction of
  the 1KB frame), not a full double buffer.
- Pump `lv_timer_handler` on input/data-change events + while animating only (§4.2).
- Trim hard via Kconfig: disable unused widgets, fonts, the LVGL FS; compile only what the Launcher +
  Task Manager use. Watch the OTA-partition budget (§9).
- Keep the app framework's `render()` contract drawing-library-agnostic so a perf-critical screen
  could drop to raw `esp_lcd` later.

### 4.5 Provisioning is now a solved path (the big win of moving to ESP-IDF)
SoftAP + `esp_http_server` + a DNS captive portal are **mature, heavily-used ESP-IDF features**, and
the **`wifi_provisioning`** managed component supports SoftAP *and* BLE transports with reference
phone apps. What was our #1 risk on Zephyr is now standard. We still **spike it in Phase 0**, but as
verification, not de-risking. See §7.

### 4.6 Threading model (FreeRTOS) — note the inverted priority convention
Three tasks. **FreeRTOS convention: higher number = higher priority** (the opposite of Zephyr), idle
= 0. App switching is owned by one task (the UI task); the input task *posts* events, never switches
apps directly. Shared task model written only by the network task under a mutex, read by the UI task.

| Task | FreeRTOS prio | Role |
|---|---|---|
| Input handler | High (e.g. 10) | Receives `knob`/`button` callbacks → `EV_*`. **Home → posts "go home" to the UI task; never dispatched to the app.** Others post to the active app via the UI-task queue |
| Render / UI | Medium (e.g. 5) | Owns the active-app pointer; runs `render()` on demand + animation tick; pushes frame via `esp_lcd` |
| Network | Low (e.g. 3) | Wi-Fi connect, sync (~5 min) via `esp_http_client`; writes the shared task model under a mutex |

Wi-Fi/IP lifecycle runs on ESP-IDF's `esp_event` loop, feeding the network task.

---

## 5. System Internals

### 5.1 Peripheral & component bring-up
- **I2C** via the new `i2c_master` driver @ 400kHz → `esp_lcd` SH1106 panel (set the **2-px column
  offset** + segment remap; this is the #1 "blank screen" cause).
- **`knob`** component → encoder A/B (+ SW handled as a button). **`button`** component → encoder SW,
  Select, Home (internal pull-ups, native debounce, click/long-press).
- A single `board_pins.h` holds GPIO assignments — the one place wiring is described.

### 5.2 Shared state & ownership (the rule that prevents most bugs)
- One `task_model_t` (task list + sync status + Wi-Fi RSSI). **Writer:** network task only.
  **Readers:** UI task only. Guarded by a FreeRTOS **mutex**; readers copy-out under the lock and
  render outside it.
- **Active app pointer** owned by the UI task. Input events and "switch app" requests are *messages*
  (queue items), never direct pointer writes from other tasks.
- **Home button** handled at this boundary: on a "go home" message the UI task runs the current
  app's `exit()` and switches to the Launcher. Apps never see Home — a guaranteed escape hatch.
- Network → UI "data changed" wakeup via a **FreeRTOS task notification** so render reacts
  immediately (ties into §4.2 on-demand render).

---

## 6. Application Framework

Polymorphic-in-C via an explicit interface; apps register in a static table.

```c
typedef struct {
    const char *name;                 /* shown in launcher */
    void (*init)(void);               /* allocate / reset state */
    void (*on_event)(uint8_t ev);     /* EV_ENCODER_CW/CCW, EV_ENCODER_CLICK, EV_SELECT
                                         (Home is never delivered here — OS-reserved, §5.2) */
    void (*render)(void);             /* draw current state via LVGL */
    void (*exit)(void);               /* teardown / free */
} device_app_t;
```

**Lifecycle contract:** `init` → (`on_event`/`render` loop) → `exit`, all called *from the UI task
only*. `render()` must not block on network/I2C beyond the frame push. Apps read the shared task
model through a provided accessor that handles locking — apps never touch the mutex directly.
`app_manager_switch_to(i)` calls current `exit()`, then next `init()`, atomically on the UI task.

### Contextual control hints (system-drawn hint strip)
*(Inspired by CrossPoint Reader's contextual button labels.)* The OS owns a thin **hint strip** along
the bottom of the OLED that shows what the controls do **right now** — so the meaning of the
encoder/Select is always visible and changes with the app's current mode (e.g. Task Manager *list*
view vs *detail submenu*). Home is reserved and always implicitly "Home", so the system draws it
fixed and apps only declare the three app-usable controls:

```c
typedef struct {
    const char *rotate;   /* encoder rotation, e.g. "Scroll"  (NULL = hide) */
    const char *click;    /* encoder push,     e.g. "Open"    (NULL = hide) */
    const char *select;   /* Select button,    e.g. "Done"    (NULL = hide) */
} control_hints_t;

void ui_set_hints(const control_hints_t *h);   /* call from the active app, UI task */
```

Apps call `ui_set_hints()` from `init()` and whenever their mode changes; the UI task renders the
strip (compact glyphs + short labels, e.g. `↻ Scroll · ◉ Open · ✓ Done`). The Launcher sets sensible
defaults so even a bare app gets usable hints. Cost is tiny: one LVGL label row, redrawn only when
hints change (fits the on-demand render model, §4.2).

### Core apps
- **App 1 — OS Launcher (boot default):** vertical scrolling list over the registered `device_app_t`
  table. Encoder rotate moves highlight; encoder click (or Select) → `app_manager_switch_to()`.
- **App 2 & 3 — Task Manager, two instances ("Yapp" + "Local"):** one Task Manager *component* (the
  hardware embodiment of `yappmark`/`todomark`, §8) registered **twice**, each bound to a different
  task source via config. Both speak the identical device contract (§8.1); only base URL + token
  differ. Top status bar (Wi-Fi/sync); middle = 3-line paginated list (long lines auto-scroll);
  bottom = the contextual hint strip. Control map (encoder + Select usable, Home reserved):
  - *Encoder rotate* — move highlight through tasks. → hint `↻ Scroll`
  - *Select* — **complete** the highlighted task (the signature one-tap `todomark` ✓ action). → `✓ Done`
  - *Encoder click* — open detail submenu (View description / Postpone / Sync now). → `◉ Menu`
  - *Home* — back to Launcher (OS-reserved, system-drawn).

  In the detail submenu the app re-publishes hints (e.g. `↻ Choose · ◉ Select`), so the strip always
  reflects the current mode.

  Sync runs on app entry + periodically. A source with no configured URL stays hidden in the Launcher.
- **App 4 — Settings:** on-device control of device *behavior* (§8A). Booleans/enums/numbers —
  knob-editable, no typing.
  - **Startup behavior** — boot into Launcher (default) · a specific app · last-used app.
  - **Deep sleep** — on/off master toggle (forward-looking for the battery build).
  - **Inactivity timeout** — Off · 30s · 1m · 5m · 15m before dim/sleep.
  - (Room to grow: brightness, sync interval — same settings-schema pattern.)
  Each setting persists to NVS immediately on change (§9).
- **App 5 — Pomodoro (Phase 2):** proves the framework for third-party devs (no network needed).

---

## 7. Provisioning — paste everything from your phone

**Core principle:** every value that lands in NVS is entered through the provisioning app by pasting
from a phone or laptop. Nothing is hardcoded; nothing is dialed in letter-by-letter. One form, paste,
submit, done — for today's fields and any added later.

### Primary path — SoftAP setup portal (MVP)
Built on ESP-IDF's **SoftAP + `esp_http_server` + a small DNS captive-portal responder** (all
standard, well-supported components):
1. On first boot (or Home-held-at-boot / config missing), the device raises open SoftAP
   `TaskMaster-Setup` and shows on the OLED: the network name + setup URL/IP.
2. User joins from a phone/laptop; the captive-portal redirect pops the form automatically (and the
   OLED shows `192.168.4.1` as a manual fallback).
3. The form is **one page collecting the whole config** — each field a paste target:
   - Wi-Fi SSID (or pick from a scanned list) + password
   - "Yapp" source: `yapp-server` URL (e.g. `http://192.168.1.50:8000`) + token
   - "Local" source: LAN-box URL + token (optional — blank hides that app)
   - room for future fields/sources — the form is *generated* from the config-key schema (§9), so a
     new key (or source range) adds a field with no UI rework.
4. `POST /save` → parse url-encoded body → write each value to its NVS key → stop SoftAP → switch to
   Station mode → connect → launch Task Manager.
5. Validation: the device attempts association (and a source `/health` ping) before committing; on
   failure it keeps the AP up and reports the error on the form to fix and re-paste.

The OLED's role during setup is purely **instructional** (network name, URL, status) — the knob never
enters characters.

### Second transport — BLE provisioning (Phase 2, cheap to add)
ESP-IDF's `wifi_provisioning` component supports a **BLE transport** alongside SoftAP using the same
manager, so a Web Bluetooth page or the reference phone app can deliver the same config blob — still
paste-from-phone, no on-screen typing. Add it without rearchitecting.

> The on-screen character carousel from the original draft is **dropped** — it's the letter-by-letter
> entry the product explicitly avoids.

---

## 8. Data & Sync Strategy — one device contract, two source apps

**Key architectural decision:** the device speaks a single fixed **task-source REST contract** — a
small set of "known endpoints." The firmware knows *only* this contract; any server implementing it
is a valid task source. We ship **two source apps**, each a configured instance of the same Task
Manager pointed at a different base URL:

- **App: "Yapp"** → **`yapp-server`**, which implements the contract on top of **Todoist** (reusing
  the existing `~/yapp-cli` code). The `todomark`-on-hardware experience.
- **App: "Local"** → a **platform-agnostic LAN box** that implements the same contract over whatever
  it likes (local DB, file, custom system — not Todoist). The device neither knows nor cares.

This keeps the device decoupled from any vendor; future sources cost nothing on the firmware side.

### 8.1 The device-facing contract (the "known endpoints")
Every source — `yapp-server` and the LAN box alike — exposes:
```
GET  /tasks         → { "tasks":[ { id, title(≤N), priority(1-4), due, parent_id, done } ], "etag" }
POST /tasks/{id}/complete                          → mark complete
POST /tasks/{id}/postpone   { "due":"tomorrow" }   → reschedule (optional; 501 if unsupported)
GET  /health                                       → liveness for the status bar
```
Rules: flat JSON, server-truncated titles, bounded task count (e.g. ≤50), `priority` normalized to
1–4 (4 = highest, matching Todoist). `etag` lets the device skip re-rendering unchanged lists.
Optional endpoints may return `501` and the UI hides that action.

- **`yapp-server`** (≈100 lines Flask/FastAPI) maps the contract onto Todoist via the existing yapp
  code: `_yapp_common.get_api()` for the client, `todolst.py`'s flatten + priority-sort for `/tasks`,
  `todomark.py`'s `close_task` for `/complete`. Todoist-library quirks (paginator vs list, completion
  fallback) stay server-side where they're already solved.
- **LAN box** implements the same four endpoints however it wants — the contract is the only spec.

### 8.2 On-device data model & UI (mirror of `todomark`, source-agnostic)
The Task Manager renders the contract's model the way `todomark` does, on a 128×64 **mono** screen:
- **Priority** 1–4, sorted **descending** so P1 floats to top. `todomark` uses a color bar; mono has
  no color → show a **priority tag/glyph** (`P1`/`(A)` … `P4`/`·`) at the line start.
- **Nesting** via `parent_id`: main tasks then subtasks, prefixed `↳` and indented.
- **Due** date on the selected/detail line.
- **Complete** = `POST /complete`; row disappears, mirroring the GUI. **Empty state**: "No open tasks 🎉".

The *same* Task Manager component renders both apps — only base URL + token differ.

### 8.3 Sync semantics
Background poll (~5 min) + immediate sync on app entry + on-demand "Sync now" (all via
`esp_http_client`). Per-source caps on task count and title length bound device memory. Parse with
**cJSON** (bundled) on a bounded buffer; use `etag` to avoid redundant re-renders.

### 8.4 Transport / TLS
Default to **plain HTTP on the LAN** for both sources (no on-device TLS — the big SRAM saver),
appropriate for a desktop appliance on a trusted home network. Keep **TLS a compile-time toggle**
(`esp-tls` + CA bundle) so a remotely-hosted `yapp-server` can be reached over HTTPS later.

**Stretch goal:** an app that talks to Todoist directly (TLS + larger buffers) once MVP is proven.

---

## 8A. Power, Sleep & Startup Behavior (Settings-driven, battery-ready)

USB-C powered today, but these controls are built now so the **future battery build needs no
rearchitecting**. Values live in NVS, edited in the **Settings app** (§6), not the provisioning form.

- **Startup behavior:** on boot `app_manager` reads `STARTUP_TARGET` → Launcher (default), a chosen
  app (e.g. "Yapp"), or last-used. Home still always returns to the Launcher (§5.2).
- **Inactivity timeout:** an idle timer (reset by any input event) fires after `IDLE_TIMEOUT_S`.
  **Stage 1:** blank/dim the OLED (saves the panel, kills burn-in) and pause the render loop.
  **Stage 2 (only if deep sleep on):** enter low power.
- **Deep sleep toggle (`DEEP_SLEEP_EN`):**
  - *Off (default, USB build):* timeout only blanks the screen; Wi-Fi + sync stay alive; input wakes instantly.
  - *On (battery-oriented):* after timeout enter low power. Prefer **light sleep** (`esp_light_sleep`,
    Wi-Fi state retained, fast wake) over full deep sleep initially — deep sleep tears down Wi-Fi/RAM
    and forces a reconnect, jarring for a task display. Expose the toggle as "deep sleep," implement
    light sleep first; revisit with real battery hardware.
- **Wake sources:** encoder rotation + the Select/Home buttons configured as GPIO wake sources
  (`esp_sleep_enable_gpio_wakeup`) so the device returns on first touch.
- **Implementation hook:** ESP-IDF **power management** (`esp_pm`, DFS + automatic light sleep) so
  idle→low-power transitions are kernel-managed, keeping the path clean for real duty-cycling later.

---

## 9. Storage, Flash Layout & OTA

4MB flash, **OTA in v1 (decided)** using ESP-IDF's native bootloader + dual-app-partition OTA
(`esp_https_ota` / `esp_ota_ops`) — no MCUboot needed.

### 9.1 Partition table (`partitions.csv`, 4MB)
```
nvs        24KB    — config + Wi-Fi calibration
otadata    8KB     — which OTA slot is active
phy_init   4KB
ota_0      ~1.9MB  — app slot A
ota_1      ~1.9MB  — app slot B
```
- **Slot budget watch (⚠):** the full app (LVGL + Wi-Fi + HTTP + app code) must fit one ~1.9MB slot.
  Keep the LVGL Kconfig trim (§4.4) honest; track image size in CI so overflow is caught early.
- **Update delivery:** the network task pulls a signed image from a configurable **`FW_URL`** (served
  by the LAN box or `yapp-server`) via `esp_https_ota`; the bootloader boots the new slot and
  **rolls back** if the app fails to self-confirm (`esp_ota_mark_app_valid_cancel_rollback`).
- **Dev flashing** stays `idf.py flash` over USB-C; OTA is the field path.

### 9.2 NVS config schema — every field populated *only* via Settings or the provisioning form
| Key | Type | Size | Source | Purpose |
|---|---|---|---|---|
| `wifi_ssid` | str | 32B | provisioning | Target AP SSID |
| `wifi_psk` | str | 64B | provisioning | WPA2 PSK |
| `yapp_url` | str | 96B | provisioning | `yapp-server` base URL → "Yapp" app |
| `yapp_token` | str | 128B | provisioning | Auth token for `yapp-server` |
| `local_url` | str | 96B | provisioning | LAN-box base URL → "Local" app |
| `local_token` | str | 128B | provisioning | Auth token for the LAN box (optional) |
| `fw_url` | str | 96B | provisioning | OTA firmware image URL (optional) |
| `startup_tgt` | u8/str | 16B | settings | Boot target (§8A) |
| `deep_sleep` | u8 | 1B | settings | Deep-sleep toggle (§8A) |
| `idle_to_s` | u16 | 2B | settings | Inactivity timeout seconds (0 = off) |
| `provisioned` | u8 | 1B | system | Boot flag → Launcher vs. first-run provisioning |

Native `nvs_flash` handles wear-leveling. **Declarative schema, not hand-wired keys:** one table
(key, type, label, max-len, secret?, write-path) drives both the generated provisioning form and the
NVS read/write. Adding a future field = one row. Two write paths: provisioning form (secrets/URLs)
and the Settings app (behavior). Optionally enable **NVS encryption** for the secret keys (§10).

---

## 10. Security
- **Secrets at rest:** PSK + tokens sit in NVS. Enable ESP-IDF **flash encryption** + **NVS
  encryption** to protect them (complicates dev flashing — enable late). At minimum, document the exposure.
- **Token scope:** `yapp-server` holds the powerful Todoist OAuth token; the device gets a narrow,
  revocable device token. Losing a device shouldn't force rotating your Todoist creds.
- **Transport:** LAN HTTP is acceptable on a trusted home network for v1; a remote source must be HTTPS.
- **Setup AP (`TaskMaster-Setup`) is open/unencrypted** — fine for a brief setup window during which
  the user pastes secrets; ensure it auto-disables after success/timeout and never stays up. (Pasted
  PSK + tokens cross an open link here — acceptable for home setup; note the exposure.)
- **Secure Boot v2** is available if the device is ever distributed; pairs with flash encryption.

---

## 11. Repository Layout (ESP-IDF)

```
TaskMaster/
├─ PLAN.md
├─ sdkconfig.defaults      ← Kconfig: Wi-Fi, esp_http_server, OTA, PM, LVGL trim, partition table
├─ partitions.csv          ← nvs / otadata / phy / ota_0 / ota_1
├─ CMakeLists.txt
├─ main/
│  ├─ main.c               ← app_main: nvs init, tasks, event loop bring-up
│  ├─ board_pins.h         ← the one place GPIO wiring lives
│  ├─ app_framework/       ← device_app_t, app_manager, launcher
│  ├─ apps/                ← task_manager.c (shared by Yapp+Local), provisioning_web.c, settings.c, (pomodoro.c)
│  ├─ net/                 ← wifi.c, softap_portal.c (esp_http_server + DNS), sync.c (contract client), ota.c
│  ├─ storage/             ← nvs.c, config_schema.c (declarative key table → form + NVS)
│  ├─ power/               ← idle_timeout.c, sleep.c (esp_pm / esp_sleep, wake sources)
│  └─ ui/                  ← display.c (esp_lcd SH1106 + esp_lvgl_port), lv_conf.h, widgets.c, hint_strip.c
├─ components/             ← pinned managed components (lvgl, knob, button, esp_lcd panel)
├─ proxy/
│  └─ yapp_server/         ← contract impl over Todoist (reuses ~/yapp-cli code)
└─ docs/                   ← wiring diagram, panel notes, device REST contract, dev setup
```
*(Managed deps — `lvgl/lvgl`, `espressif/knob`, `espressif/button`, the SH1106 `esp_lcd` panel — are
pinned via `idf_component.yml`.)*

---

## 12. Toolchain & Build Workflow
- **CLI only: native ESP-IDF (`idf.py`).** Activate the env per shell session:
  `source "$HOME/.espressif/tools/activate_idf_v6.0.1.sh"`.
- **ESP-IDF version: pinned** — currently **v6.0.1** (installed via `eim`). Reproducible base;
  record the exact rev. (Note: v6.0 split the monolithic `driver` component into per-peripheral
  `esp_driver_*` — components are named explicitly in `main/CMakeLists.txt`.)
- Build: `idf.py build`  ·  Flash + serial: `idf.py -p <PORT> flash monitor`  ·
  Set target (once): `idf.py set-target esp32c3`.
- Config: `idf.py menuconfig` → baked into `sdkconfig.defaults`.
- Managed deps (LVGL, `knob`, `button`, SH1106 `esp_lcd` panel) pinned via `idf_component.yml`.

---

## 13. Test & Validation Strategy
- **Per-driver bring-up first:** tiny standalone test apps for (a) OLED "hello", (b) `knob` count to
  serial, (c) `button` events to serial — before any app logic. The SH1106 column offset and encoder
  steps are the two most common silent failures.
- **Unit tests (`Unity`, ESP-IDF's framework):** app-manager state machine, JSON→task-model parsing
  (cJSON), provisioning form-body parser, config-schema↔NVS round-trip. Run host-side where possible
  (Linux target / `qemu`).
- **Integration on hardware:** provisioning happy path; power-cycle persistence; sync indicator
  correctness; app-switch with no use-after-free across tasks; OTA pull + rollback.
- **Soak test:** 24h run; confirm no heap exhaustion (`heap_caps` watermarks), no task-stack overflow
  (`uxTaskGetStackHighWaterMark`), no Wi-Fi reconnect leak.

---

## 14. Phased Roadmap

| Phase | Goal | Exit criteria |
|---|---|---|
| **0 — Bring-up + spike** | ESP-IDF build, partition table, per-driver tests **+ SoftAP/HTTP spike** | App boots; OLED draws; `knob`/`button` register; **a phone loads a page served by the device over SoftAP** |
| **1 — Core OS** | FreeRTOS tasks + app framework + Launcher | Launcher lists apps, encoder navigates, app switch is clean (no races) |
| **2 — Provisioning portal** | Paste-from-phone setup form → NVS | Join `TaskMaster-Setup`, paste full config in one form, persist to NVS, associate, survive reboot |
| **3 — Sync + Task Manager** | `yapp-server` proxy + two source apps over the contract | Tasks display priority-sorted with nesting (mirrors `todomark`); status bar accurate; complete/postpone work; "Local" app works against a stub server |
| **4 — Settings + power** | Settings app + idle timeout + sleep scaffolding | Startup target, deep-sleep toggle, timeout all persist and take effect; screen blanks on idle |
| **4.5 — OTA path** | `esp_https_ota` + rollback | Device pulls a signed image from `fw_url`, boots the new slot, rolls back on failed confirm |
| **5 — Hardening + Phase 2** | Soak, error states, enclosure; then BLE provisioning / direct Todoist / Pomodoro | 24h soak clean; graceful Wi-Fi-loss + API-error UI; fits enclosure; Phase-2 items as separate increments |

Phases 0–4.5 = MVP. Phase 5 = hardening + post-MVP.

---

## 15. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| SRAM exhaustion (Wi-Fi + JSON + LVGL) | Med | High | Proxy shrinks/flattens JSON; LVGL 1-bit + single partial buffer + Kconfig-trimmed widgets/fonts; bounded buffers; cap task count; XIAO-S3 escape hatch |
| Direct Todoist TLS too heavy on-device | Med | Med | `yapp-server` proxy is the default; direct is stretch only |
| Wrong SH1106 column offset → blank/garbled screen | Med | Low | Phase-0 driver bring-up; set the 2-px offset |
| Task races on active-app / shared model | Med | High | Strict ownership (§5.2); single mutex; app switch on the UI task only |
| App image overflows ~1.9MB OTA slot | Low | Med | Kconfig-trim LVGL; track image size in CI; drop unused features |
| Encoder missed steps at speed | Low | Low | `knob` component on PCNT; tune steps-per-detent |
| ESP32-only lock-in (left Zephyr's portability) | Low | Low | Accepted — committed to Espressif silicon; `device_app_t` + the REST contract stay portable concepts |

*(Note: SoftAP/HTTP/captive-portal — formerly the top risk on Zephyr — is now a supported ESP-IDF
path and drops off the register, downgraded to a Phase-0 verification spike.)*

---

## 16. Decisions — all resolved ✅

| # | Decision | Outcome |
|---|---|---|
| 1 | Framework | **ESP-IDF** (FreeRTOS), **CLI-only** via `idf.py` (currently v6.0.1) |
| 2 | MCU | **XIAO ESP32-C3** (XIAO ESP32-S3 = drop-in upgrade if SRAM bites) |
| 3 | OLED controller | **SH1106** — `esp_lcd` panel, 2-px column offset |
| 4 | UI library | **LVGL** via `esp_lvgl_port` — 1-bit, single partial buffer, trimmed |
| 5 | Backend / data | **One device contract, two source apps** — "Yapp" (`yapp-server`/Todoist) + "Local" (LAN box) |
| 6 | Transport | **Plain HTTP on LAN** for both; TLS a compile-time toggle for remote later |
| 7 | Provisioning | **Paste-from-phone SoftAP portal** (all config in one form); carousel dropped; BLE = a Phase-2 second transport |
| 8 | OTA | **Yes — ESP-IDF native dual-slot** (`esp_https_ota`) + rollback, image from `fw_url` |
| 9 | Settings/power | **Settings app** controls startup target, deep-sleep toggle, inactivity timeout — battery-ready scaffolding (§8A) |
| 10 | IDF version | **v6.0.1** (installed via `eim`), pinned for reproducibility |

No open decisions remain — the plan is build-ready.

---

## 17. Acceptance Criteria (MVP "done")

- [ ] Fresh device raises `TaskMaster-Setup`; user pastes the full config (Wi-Fi, source URLs, tokens)
      from a phone in one form; device associates. **No value is typed on the knob.**
- [ ] All config persists across power cycle; device auto-connects on next boot.
- [ ] Launcher lists registered apps; encoder navigates; Select/click switches app with no crash/leak.
- [ ] Task Manager shows Todoist tasks (via `yapp-server`) within one sync cycle, priority-sorted with
      nesting, mirroring `todomark`; the "Local" app works against a stub source; status bar reflects
      real Wi-Fi + sync state.
- [ ] Select completes the highlighted task; Postpone (submenu) reschedules; both reflect on next sync.
- [ ] **Home returns to the Launcher from anywhere**, even mid-action, never swallowed by an app.
- [ ] The contextual **hint strip** shows the current control labels and updates when an app changes
      mode (e.g. Task Manager list view → detail submenu).
- [ ] Settings changes (startup target, timeout, deep-sleep toggle) persist and take effect.
- [ ] OTA: device pulls a signed image from `fw_url`, boots the new slot, and rolls back on failed confirm.
- [ ] Input→visible-response latency feels instant (<100ms) under active background sync.
- [ ] 24h soak: no heap/stack exhaustion, no stuck state after a Wi-Fi drop/reconnect.
```
