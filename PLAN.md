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
- Background sync with a visible connectivity/sync status indicator, exposed to apps via a documented
  read-only API (`net_status.h`, §6).
- **Wi-Fi master on/off** in Settings — turn the radio off to save battery / stay offline; the device
  runs offline against cached tasks (§8A/§8.3).
- App framework with ≥2 apps proving the interface (Launcher + Task Manager), and **non-removable core
  apps** (the **Launcher** and **Settings** — the device hub that includes Wi-Fi setup) vs. removable
  manifest-driven user apps (§6).

### Post-MVP — after v1.0 is stable (build Phase 5, §14)
> Naming note: "Phase 5" here is the **build phase** in §14. (An earlier draft called this set
> "Phase 2," which collided with the build Phase 2 = provisioning; that label is retired.)
- **BLE provisioning** as a second transport (trivial alongside SoftAP in ESP-IDF) (§7).
- Direct Todoist integration on-device (bypass the proxy) — see §8.4 stretch.
- **Pomodoro** bundled example *user app* (removable) — validates framework extensibility for outside
  developers (§6).

### Non-Goals (v1)
- Multi-account / multiple simultaneous backends per source.
- Offline task editing with conflict resolution (writes go to the backend or are queued simply).
- Battery operation as *hardware* (device is USB-C powered). But the **Settings + sleep/timeout
  scaffolding is built now** (§8A) so the future battery build is a hardware change, not a rewrite;
  v1 uses the timeout for screen-blank, ships light-sleep behind the toggle, and already lets the user
  cut the biggest draw via the **Wi-Fi on/off toggle** (§8A).
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
| Rotary input | EC11 encoder + switch | 20 detents/rev; A/B + SW = 3 GPIO; push = app-usable. Hand-rolled Ben-Buxton GPIO decode (C3 has no PCNT, §4.3) |
| Buttons | 2× tactile momentary | **Select** (app-usable) + **Home** (OS-reserved); internal pull-ups; sample-debounced GPIO read (§4.3) |
| Enclosure | 3D-printed, 15–20° wedge | Exposes screen, knob, 2 buttons only |

**Pin budget:** OLED (SDA/SCL = 2), encoder (A/B/SW = 3), buttons (2) = **7 GPIO** + power. *(Battery
build: add an **RC filter** on encoder A/B + buttons so the Phase-4 interrupt-driven input is bounce-free
in hardware and tickless idle can engage — §14.)* XIAO
ESP32-C3 exposes 11 GPIO — comfortable headroom.

**⚠ SRAM is the real constraint, not GPIO.** 400KB SRAM holds the Wi-Fi stack, optionally an
mbedTLS session, the JSON buffer, and LVGL. This drives several decisions below (proxy architecture,
LVGL trim, HTTP-on-LAN default). If it ever bites, the pin-compatible **XIAO ESP32-S3** (512KB SRAM
+ 8MB PSRAM) is a drop-in escape hatch with no plan changes.

---

## 4. Firmware & Software Architecture (ESP-IDF / FreeRTOS)

ESP-IDF gives us FreeRTOS, a managed-component dependency system, native Wi-Fi/SoftAP/HTTP/OTA/NVS,
and official LVGL (adopted Phase 3, §4.4). Input is a hand-rolled GPIO decoder (§4.3). Peripherals are configured in code + `menuconfig`
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

### 4.3 Input — hand-rolled GPIO decoder on the C3 (decided)
**The C3 has no PCNT**, so the Espressif `knob` component's headline feature — zero-CPU hardware
quadrature decoding — is unavailable; it would fall back to GPIO anyway. We therefore use a
**hand-rolled Ben-Buxton quadrature state machine** for the encoder and a **sample-debounced reader**
for the buttons (`input.c`), polled by a 1 ms input task that translates edges into our `EV_*` enums
and posts them to a FreeRTOS queue. Tiny, dependency-free, and tunable in code. (Home is tagged
specially — see §4.6/§5.2.)

> **Deferred reevaluation (Phase 4, battery):** the 1 ms poll prevents light sleep, so the battery
> phase converts input to **interrupt / GPIO-wake driven** regardless of library. At that point we
> reevaluate the managed **`button`** component specifically — its wake-source support and free
> long-press/double-click semantics finally justify the dependency. The `EV_*` queue is the
> abstraction boundary, so swapping the input source touches nothing above the input task.

### 4.4 UI library: LVGL (decided)
**LVGL** via the official `esp_lvgl_port` component, on the SH1106 panel through `esp_lcd`. Cost is
real on 400KB SRAM + Wi-Fi, so budget for it:
- **1-bit (`I1`) color depth** matched to the mono panel; **one full-frame buffer (~1 KB)** in
  `RENDER_MODE_FULL`, *not* a partial fraction. A full mono frame is exactly 128×64/8 = **1024 bytes**;
  shaving it saves nothing meaningful on 400 KB but multiplies flush calls per frame. One buffer = one
  flush/frame, freeing CPU for the real SRAM consumers (TLS + JSON). *(This is what `lvgl_disp.c`
  ships.)*
- Pump `lv_timer_handler` on input/data-change events + while animating only (§4.2).
- Trim hard via Kconfig: disable unused widgets, fonts, the LVGL FS; compile only what the Launcher +
  Task Manager use. Watch the OTA-partition budget (§9).
- Keep the app framework's `render()` contract drawing-library-agnostic so a perf-critical screen
  could drop to raw `esp_lcd` later.

> **Adoption timing (decided):** LVGL was **deferred to Phase 3** and is now **adopted as Phase 3's
> first step** (§8.5 Part A) — the UI foundation lands before `app_tasks`, and the Phase 0–2 screens
> (Launcher, Setup, Hello), which used the raw `sh1106` text renderer through Phase 2, are ported to
> LVGL then. Deferring proved the app framework + clean app-switch first and moved the LVGL/SRAM-budget
> risk to where there's real UI to justify it.

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
| Input handler | High (e.g. 10) | 1 ms poll: Ben-Buxton encoder decode + button debounce → `EV_*` (§4.3). **Home → posts "go home" to the UI task; never dispatched to the app.** Others post to the active app via the UI-task queue |
| Render / UI | Medium (e.g. 5) | Owns the active-app pointer; runs `render()` on demand + animation tick; pushes frame (raw `sh1106` now, `esp_lcd`/LVGL in Phase 3) |
| Network | Low (e.g. 3) | Wi-Fi connect, sync (~5 min) via `esp_http_client`; writes the shared task model under a mutex |

Wi-Fi/IP lifecycle runs on ESP-IDF's `esp_event` loop, feeding the network task.

---

## 5. System Internals

### 5.1 Peripheral & component bring-up
- **I2C** via the new `i2c_master` driver @ 400kHz → `esp_lcd` SH1106 panel (set the **2-px column
  offset** + segment remap; this is the #1 "blank screen" cause).
- **Hand-rolled GPIO input** (§4.3): Ben-Buxton decode on encoder A/B; sample-debounced reader on
  encoder SW, Select, Home (internal pull-ups). 1 ms poll task → `EV_*`. (C3 has no PCNT; managed
  `knob`/`button` reevaluated at the battery phase.)
- A single `board_pins.h` holds GPIO assignments — the one place wiring is described.

### 5.2 Shared state & ownership (the rule that prevents most bugs)
- **Connectivity** (`net_status`, §6) is the one piece of shared status core owns: writer = `wifi_mgr`
  on the event loop, readers copy-out; the UI re-renders on change. *(There is no core task model —
  tasks are userspace, §8. An app's `task_t` model is owned by the **UI task only**, since fetch
  results are delivered there by `async_job` (§8.5 step 8) — so app models need **no mutex**.)*
- **Active app pointer** owned by the UI task. Input events and "switch app" requests are *messages*
  (queue items), never direct pointer writes from other tasks.
- **Home button** handled at this boundary: on a "go home" message the UI task runs the current
  app's `exit()` and switches to the Launcher. Apps never see Home — a guaranteed escape hatch.
  Because Home can arrive at *any* point (mid-render, mid-fetch, even mid-`init`), `exit()` must be
  **idempotent and total**: free everything `init()` *could* have allocated regardless of how far it
  got, null-out as it goes (and, once LVGL lands in Phase 3, free the widget tree in one screen-delete
  — Phase-1 raw-rendered apps allocate no widgets). See §6A for the full discipline.
- Network → UI "data changed" wakeup via a **FreeRTOS task notification** so render reacts
  immediately (ties into §4.2 on-demand render).

---

## 6. Application Framework

Polymorphic-in-C via an explicit interface. Each app is a **self-contained ESP-IDF component** (its
own directory, optionally its own git repo) that **self-registers** with the core — so the set of
apps on a build is composed from one editable manifest, with **no edits to core** and no fork (§6.1).

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

### 6.1 App architecture — apps as self-registering components

**Requirement:** a developer can build an app in a **separate repo** and add/remove it from a device
build by editing **one manifest line** — no forking, no edits to core, and a disabled app is not
compiled. This rules out a core-owned `#ifdef`/`g_apps[]` table (an external repo can't be reached by
a core header, and a hardcoded table would force a core edit per app = a fork by another name). The
model is therefore **self-registration + an editable manifest**.

**Three roles (clean separation of ownership):**

```
 ┌────────────────────────┐   stable, versioned app API   ┌──────────────────────────┐
 │  taskmaster_core        │◄──────────────────────────────│  app component (per app) │
 │  (ESP-IDF component)    │   device_app_t                │  own dir / own git repo  │
 │  - device_app_t         │   app_manager_register()      │  REQUIRES taskmaster_core│
 │  - app_manager registry │   ui_set_hints() (§6 hints)   │  TASKMASTER_REGISTER_APP │
 │  - UI / input / net /   │                               └──────────────────────────┘
 │    storage / launcher   │                                         ▲  (many)
 └────────────────────────┘                                         │
            ▲   iterates registry at runtime                         │ listed in
            │                                                        │
 ┌──────────┴───────────────────────────────────────────────────────┴──────────┐
 │  firmware (config) repo:  main/ + partitions + sdkconfig + THE MANIFEST       │
 │  main/idf_component.yml  ← the editable app list (one line per app)           │
 └──────────────────────────────────────────────────────────────────────────────┘
```

- **`taskmaster_core`** — the OS as one component: the `device_app_t` interface, `app_manager`
  (registry + lifecycle), UI/input/net/storage, Launcher. Exposes the **stable, versioned app API**
  (`device_app_t`, `app_manager_register()`, the hint API). The only thing an app depends on.
- **App component** — one per app, its own directory and (optionally) its own git repo.
  `REQUIRES taskmaster_core`, implements `device_app_t`, and **self-registers** — core never
  references the app by name.
- **Firmware/config repo** — the thin top-level project that picks which apps ship, via the manifest.

**Self-registration** (core stays oblivious to every app):
```c
// provided by taskmaster_core
#define TASKMASTER_REGISTER_APP(app) \
    static void __attribute__((constructor)) _tm_reg_##app(void) { \
        app_manager_register(&app); \
    }
```
```c
// app_pomodoro component
static const device_app_t pomodoro = { .name="Pomodoro", .init=.., .on_event=.., .render=.., .exit=.. };
TASKMASTER_REGISTER_APP(pomodoro);
```
The app component pins its registration against `--gc-sections` with
`idf_component_register(... WHOLE_ARCHIVE)` (or a `linker.lf` `KEEP`). At boot the constructors call
`app_manager_register()`; the Launcher iterates the registry in registration order.

**The editable list = `main/idf_component.yml`** (the developer-facing knob):
```yaml
# main/idf_component.yml — paths are relative to main/; core is auto-discovered in components/
dependencies:
  # ── apps: comment a line to remove that app (not built, not linked, not shown) ──
  app_tasks:    { path: ../apps/app_tasks }               # in-tree app
  app_settings: { path: ../apps/app_settings }
  app_clock:    { git: https://github.com/somedev/tm-clock.git, version: v0.2.0 }   # external repo
  # app_pomodoro: { git: https://github.com/you/tm-pomodoro.git }                   # disabled
```
- **Add an app** — one line: a local `path:` or a `git:` URL to the app's own repo. The ESP-IDF
  Component Manager fetches/builds it, it self-registers, it appears in the Launcher.
- **Remove an app** — comment the line. Not fetched, not compiled, not registered (the strongest
  form of "not built").
- **Order** — registration order = Launcher order (the Launcher can also sort by `name`).
- **Local dev** — point `EXTRA_COMPONENT_DIRS` at a sibling checkout while iterating, no manifest churn.

**Boundary contract:** `device_app_t` + `app_manager_register()` + the hint API form the **versioned
app API**; an app component declares a compatible `taskmaster_core` version in its manifest. Keeping
this boundary the single dependency is what makes a separate-repo app a one-line add.

> **Deferred polish (not now):** publishing `taskmaster_core` + first-party apps to the ESP-IDF
> component registry, semantic-versioning the API, and a GitHub-Actions build that emits a flashable
> `.bin` from a config repo (ZMK-config style). The mechanism above already enables separate-repo
> apps today via local `path:` or `git:` deps; these only add distribution polish.

### Contextual control hints (system-drawn hint bar — right column)
*(Inspired by CrossPoint Reader's contextual button labels.)* The OS can draw a **vertical hint bar
down the right edge** of the OLED showing what the controls do **right now** — so the meaning of the
encoder/Select is always visible and changes with the app's current mode (e.g. Task Manager *list*
view vs *detail submenu*).

**Geometry (128×64).** The bar is the **rightmost 20px column, full height**, holding **three 20×20
boxes** with **1×20px** gaps (1px tall, column-wide) at top, bottom, and between — boxes at
`y = 1–20, 22–41, 43–62`. The remaining **108×64** on the left is the app content area. Each box shows
a **glyph** (↻ scroll, ● open, ✓ done, ▣ home) or a **≤3-char** label.

**Box → control map** (top to bottom):
1. **Home** — OS-fixed glyph (▣); always "back to Launcher", apps don't set it.
2. **Encoder** — one physical knob, two actions, so the box is **split into two ~9px cells**: rotate
   (top) over push/click (bottom). The rotate cell defaults to a ↻ glyph when the app gives no label.
3. **Select** — the app's label/glyph.

Apps declare only the three app-usable controls (Home is OS-fixed):
```c
typedef struct {
    const char *rotate;   /* encoder rotation → encoder box, top cell    (NULL = ↻ default) */
    const char *click;    /* encoder push     → encoder box, bottom cell (NULL = hide)       */
    const char *select;   /* Select button    → bottom box               (NULL = hide)       */
} control_hints_t;

void ui_set_hints(const control_hints_t *h);   /* call from the active app, UI task */
```

**Per-app: two modes** (screen real estate is scarce, so the bar is opt-in):
- **Hint bar on** — the OS draws the 20px column; the app gets the **108×64** content area and calls
  `ui_set_hints()` from `init()` and on mode changes.
- **Hint bar off** — the app gets the **full 128×64** and owns the whole screen (it may draw its own
  affordances). An app selects its mode in its `device_app_t` (e.g. a `hint_bar` flag); Home still
  works as the physical escape hatch regardless.

There is **no status bar** *(in MVP)* — connectivity/sync is surfaced by apps that want it (via
`net_status.h`), not an OS-reserved strip. Cost is tiny: the bar redraws only when hints change
(on-demand, §4.2). *(A rich status bar returns post-MVP — see §6C.)*

### 6C. Future UI upgrade — glyph hint bar + rich status bar (post-MVP, NOT Phase 3)
Two related UI upgrades, **deferred** (assets staged in `icons/`: `home`, `scroll`, `reset`,
`connected`, `not_connected`, …). Decisions beyond the sketch below are left for when we build it.

1. **Hint-bar glyphs.** Replace the ≤3-char text labels (`HOM`/`<>`/`OPN`) with **1-bit glyphs** in the
   20px boxes (the `icons/` assets). The `control_hints_t` contract is unchanged — only the rendering
   swaps text → glyph; an app can still fall back to a short label. Cleaner at 20px.

2. **Rich status bar — in the LAUNCHER only** (not an OS strip over every app, so app content areas are
   unaffected). Shown on the Launcher screen **only when online** (and a city is set), carrying:
   - a **connectivity glyph** (`connected` / `not_connected`),
   - the **time** — `esp_sntp` synced from an NTP server at a sensible interval, and
   - **weather** (an API call).
   - **Offline / Wi-Fi off / no city → the Launcher shows no status bar** (its current plain list).
   - **City** is entered at **provisioning** (likely an `ACFG_PASTE` field, §9.4, or a core field — TBD).
     **Timezone is an API call** keyed by the city (so NTP time displays correctly); **weather** is a
     second API call (same provider likely).

   This stays clear of the apps entirely (it's Launcher chrome), and reconciles with §6's MVP "no status
   bar" — apps never get one; the Launcher gains this only online + post-MVP. Open (decide later): which
   weather/timezone API; sync intervals; where the city/time/weather service lives; glyph asset format.

### Apps — core (built-in, non-removable) vs. user (manifest-driven, removable)

Two classes share the one `device_app_t` interface:
- **Core apps** are compiled into `taskmaster_core` and register themselves unconditionally — they are
  **not** listed in `main/idf_component.yml` and the user **cannot remove** them. These are the OS:
  the Launcher and **Settings** (the device hub, which includes Wi-Fi setup).
- **User apps** are separate components added/removed via the manifest (§6.1) — e.g. the Task Manager
  instances, and bundled examples like Pomodoro. Removing one is a one-line manifest edit.

- **App 1 — OS Launcher (boot default, core):** vertical scrolling list over the registered
  `device_app_t` table. Encoder rotate moves highlight; encoder click (or Select) →
  `app_manager_switch_to()`.
- **App 2 & 3 — Task Manager, two instances ("Yapp" + "Local"):** one Task Manager *component* (the
  hardware embodiment of `yappmark`/`todomark`, §8) registered **twice**, each bound to a different
  task source via config. Both speak the identical device contract (§8.1); only base URL + token
  differ. Content = paginated task list (long lines auto-scroll), with Wi-Fi/sync surfaced inline (a
  small glyph row — no OS status bar, §6); the **right hint bar** carries the controls. Control map
  (encoder + Select usable, Home reserved):
  - *Encoder rotate* — move highlight through tasks. → hint `↻ Scroll`
  - *Select* — **complete** the highlighted task (the signature one-tap `todomark` ✓ action). → `✓ Done`
  - *Encoder click* — open detail submenu (View description / Postpone / Sync now). → `◉ Menu`
  - *Home* — back to Launcher (OS-reserved, system-drawn).

  In the detail submenu the app re-publishes hints (e.g. rotate `Choose` · click `Select`), so the
  hint bar always reflects the current mode.

  Sync runs on app entry + periodically. A source with no configured URL stays hidden in the Launcher.
- **App 4 — Settings (core, non-removable) — the device hub:** a knob-driven menu (rotate to choose,
  Select to act) that holds **all** device/network/system functions. This is the single core utility
  app — Wi-Fi setup is a menu item here, **not** a separate app. Items:
  - **Wi-Fi setup** — raise the paste-from-phone provisioning portal (§7): SoftAP + the schema-driven
    form. The only item that's an *action* (opens the portal) rather than a knob-edited value; it's
    also **auto-opened at boot** when the device is unprovisioned or Home is held at reset (§7A.3).
  - **Wi-Fi** — on/off master toggle: turn the radio off to save battery / stay offline (§8A).
  - **Startup behavior** — boot into Launcher (default) · a specific app · last-used app.
  - **OLED brightness / contrast** — knob-adjustable panel brightness (SH1106 contrast), applied live.
  - **Deep sleep** — on/off master toggle (forward-looking for the battery build).
  - **Inactivity timeout** — Off · 30s · 1m · 5m · 15m before dim/sleep.
  - **Device / network info** — read-only: IP, MAC, RSSI, SSID, **firmware version + build date**, free
    heap, uptime.
  - **Factory reset** — wipe NVS (creds + app data) behind the reusable **confirm dialog**, then reboot
    into Wi-Fi setup (`config_factory_reset()`).
  - **Restart** — soft reboot (`esp_restart`) for recovery without unplugging.
  - **OTA update** — pull a signed image from `fw_url` (Phase 4.5; a stub/"coming soon" until then).
  - **Per-app config** — for each installed app that declared config (§9.4): its **`ACFG_KNOB`**
    scalars are knob-editable here (with bounds), and its **`ACFG_PASTE`** strings/tokens are shown
    masked with a **"re-provision"** shortcut that opens the Wi-Fi-setup form (no knob typing). Core
    names no app — these sections come from the `app_config` registry.
  - **Built schema-driven (§8A.1 step 0):** every knob-editable item above is a row in a declarative
    settings table rendered by **one** generic editor; core rows and per-app `ACFG_KNOB` rows share it,
    so adding a setting is a row, not a screen, and core hardcodes no app. (Room to grow: sync interval
    is **not** here — it's app-owned, §8A.1.)
  Behavior settings persist to NVS immediately on change (§9). **Boundary:** Settings owns
  network/device/system + the *editing surface* for app-declared config (§9.4); the app config's
  *meaning* (and the task sources themselves) live in the app repos (§11.2).
- **Pomodoro (user app, Phase 5):** a bundled *removable* example that proves the framework for
  third-party devs (no network needed).

### Platform status for apps — connectivity (`net_status.h`)

Connectivity is **platform state**, so apps read it from one documented place instead of each owning a
Wi-Fi event handler. The app-facing surface is read-only and tiny:

```c
net_status_t ns;
net_status_get(&ns);            // copy-out snapshot (mutex-guarded, §5.2)
if (ns.online) { ... }          // or: net_is_online()
// ns.state ∈ { NET_WIFI_OFF, NET_DISCONNECTED, NET_CONNECTING, NET_CONNECTED, NET_PORTAL }
// ns.rssi  = dBm when online
const char *label = net_state_str(ns.state);   // "OFF"/"---"/"..."/"OK"/"SETUP"
```

**The contract (what makes it streamlined):** an app just reads `net_status_get()` inside `render()`
and the **platform re-renders the app whenever connectivity changes** — so the displayed state is
always current with no subscription, polling, or radio handling in the app. Mechanically, a change
posts a system event (`EV_SYS_NET_CHANGED`) onto the UI event queue; the **UI task** consumes it and
calls the active view's `render()`. System events are **never delivered to `on_event`** — apps stay
input-only and can't accidentally depend on event plumbing. Apps that need to *act* on a transition
(not just redraw) cache the last `state` in their own struct and compare in `render()`.

Apps **never** call `esp_wifi_*` or manage the radio — that's core's job (the network task, §8.3; the
Setup app, §7; the `WIFI_EN` toggle, §8A). The same pattern is how future platform status (battery,
sync) will be exposed: a `*_get()` snapshot + a system event that triggers re-render. The full
app-author guide lives in [`docs/APP_API.md`](docs/APP_API.md).

---

## 6A. Memory safety & leak discipline

On a 400KB device with no MMU, a leak or stray write doesn't throw — it starves the heap or silently
corrupts a neighbor and reboots days later. The rules below are *constraints Phase 1 is built
against*, not a retrofit. They assume — and do not restate — the ownership model (§5.2) and the
UI-task-only, cooperative `init`/`exit` lifecycle (§6), which already remove the worst class of
race-on-teardown by construction.

### 6A.1 Make leaks structurally impossible (the big lever)
- **Bounded over dynamic.** Task data lives in a fixed `task_t tasks[TASKMASTER_MAX_TASKS]` (core-owned,
  §5.2), never a growing list. The network task parses *into* that array, then frees the cJSON tree
  immediately. **No per-task `malloc`.**
- **No per-widget allocation (Phase 1) → widgets owned by the app's screen (Phase 3+).** While the
  Launcher renders on the raw `sh1106` text renderer (§4.4), apps allocate **no** UI objects, so
  `exit()` has no widget tree to free at all. Once LVGL lands (Phase 3), every app parents its widgets
  to its own screen object and `exit()` deletes/cleans that screen (`lv_obj_clean`) to free the whole
  tree in one call — you never track individual widgets, so you can't leak one and `exit()` stays
  trivially total (§5.2).
- **Stream, don't accumulate.** The HTTPS client parses into the fixed struct as bytes arrive, reusing
  one buffer — it never concatenates a whole Todoist/LAN-box body into a heap string.
- **One owner per allocation, documented.** Allocations that cross a function boundary are the ones
  that rot; the `device_app_t` contract states who frees what.

### 6A.2 No app-owned worker tasks — use the core `async_job` service
Apps **must not spawn their own tasks.** Background I/O (a task fetch) goes through the core
**`async_job`** service (§8.5 step 8): the work runs on a **core-owned** worker, and the result is
delivered back **on the UI task**. This designs out the in-flight-fetch use-after-free: an app's
`exit()` calls `async_job_cancel()`, so a fetch in flight can't write into freed app memory; and
because results land on the UI task, the app's `task_t` model is single-threaded (no mutex). Core owns
the worker; apps stay single-task — the §6A teardown guarantee holds for every app, first- or
third-party.

### 6A.3 Tooling — all built into ESP-IDF, debug builds only
- **Heap poisoning:** `CONFIG_HEAP_POISONING_COMPREHENSIVE` — canaries catch overflow, use-after-free,
  double-free at runtime.
- **Leak tracing:** `heap_trace_start(HEAP_TRACE_LEAKS)` / `heap_trace_dump()` around one
  launch→Home→relaunch cycle prints exactly what wasn't freed.
- **Stack overflow trap:** `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`, plus per-task
  `uxTaskGetStackHighWaterMark()` to size stacks from data (§13 soak).
- **Static:** `-Wall -Wextra` and `idf.py clang-check`. Pure-logic code (JSON→struct mapping, encoder
  state machine) is unit-tested on the host `linux` target, where ASan/LeakSan actually run.

### 6A.4 The test that proves Home is leak-safe
Wrap launch→Home→relaunch in a heap-trace leak cycle and assert `esp_get_minimum_free_heap_size()`
after Home **equals** the value before launch. Run it per app, and explicitly **fire Home mid-fetch**
— the case that would catch any accidental cross-task reference. Clean 100× ⇒ Home teardown is sound.

> **When this lands:** the harness is built and first run in **Phase 2** (§14) on a heap-poisoning
> debug build — it was deferred out of Phase 1, whose lifecycle/Launcher were verified functionally on
> hardware but without heap tracing. Phase 1 already satisfies its preconditions: app `exit()` is
> total/idempotent, no app spawns its own task (§6A.2), and `app_hello` holds no heap. So the Phase-2
> run is the *measurement* that confirms what Phase 1 built. It then stays a standing gate for every
> app added thereafter (§17).

---

## 7. Provisioning — paste everything from your phone

**Core principle:** every value that lands in NVS is entered through **Wi-Fi setup** — the provisioning
item in the non-removable **Settings hub** (§6) — by pasting from a phone or laptop. Nothing is
hardcoded; nothing is dialed in letter-by-letter. One form, paste, submit, done — for today's fields
and any added later. Wi-Fi setup is **auto-opened at boot** when unprovisioned (or Home-held-at-boot /
config missing), overriding the configured startup target, and is **also reachable from Settings
anytime** to re-provision. *(Interim Phase-2 build: a standalone `app_setup`, §7A.4.)*

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

### Second transport — BLE provisioning (Phase 5, nice-to-have)
ESP-IDF's `wifi_provisioning` component supports a **BLE transport** alongside SoftAP using the same
manager, so a Web Bluetooth page or the reference phone app can deliver the same config blob — still
paste-from-phone, no on-screen typing. Add it without rearchitecting.

> The on-screen character carousel from the original draft is **dropped** — it's the letter-by-letter
> entry the product explicitly avoids.

---

## 7A. Phase 2 — build plan (provisioning · storage · Wi-Fi link)

**Theme:** make the device *connect*. Phase 1 boots into a Launcher with a placeholder AP; Phase 2
turns that into the real **Setup/Wi-Fi core app** that writes the whole config to NVS and brings the
device online as a station — plus the storage + link-manager foundations the rest of the product
stands on. Sync/Task Manager is **not** here (that's Phase 3); Phase 2 stops at "online + config
persisted." It also stands up the §6A.4 leak harness carried over from Phase 1.

### 7A.1 New modules
| Module (in `taskmaster_core/`) | Role |
|---|---|
| `storage/nvs_config.[ch]` | Declarative config schema → typed NVS get/set; `provisioned` flag; factory reset |
| `net/wifi_mgr.[ch]` | Owns the radio: STA connect, retry/backoff, `esp_event` handlers → drive `net_status`; honors `WIFI_EN` |
| `app_setup.c` (core app) | The **Setup/Wi-Fi** `device_app_t` (§6): runs the portal, instructional OLED, teardown |
| `provisioning.[ch]` (extends Phase-0 `softap_portal.c`) | SoftAP + HTTP form (GET/scan/POST) + captive DNS |

### 7A.2 Storage layer — schema-driven NVS (`nvs_config`)
One declarative table is the single source of truth (the §9.2 schema): `{key, type, max-len, secret?,
write-path, default}`. It drives **both** the NVS read/write **and** the generated form (7A.4), so a
new field is one row, no UI rework.
- API: `config_init()`, `config_get_str/_u8/_u16(...)`, `config_set_str/_u8/_u16(...)`,
  `config_is_provisioned()`, `config_factory_reset()`. One NVS namespace; `nvs_flash` wear-levels.
- Pure key↔value mapping is **host-unit-tested** (Unity on the `linux` target) — round-trip + bounds.

### 7A.3 Wi-Fi link manager + boot-mode state machine (`wifi_mgr`)
`wifi_mgr` is the only owner of `esp_wifi_*`; everything else reads `net_status` (§6). It maps Wi-Fi
events → states: `START/retry → NET_CONNECTING`, `GOT_IP → NET_CONNECTED(rssi)`,
`DISCONNECTED → NET_DISCONNECTED` (retry with backoff), `WIFI_EN=0 → NET_WIFI_OFF` (radio down).
Boot decision in `app_main`:
```
config_init()
if (home_held_at_boot() || !config_is_provisioned())   → Settings → Wi-Fi setup (NET_PORTAL)   // overrides STARTUP_TARGET
else if (WIFI_EN)                                        → wifi_mgr_start_sta() (NET_CONNECTING…) → STARTUP_TARGET (§8A)
else  /* provisioned, Wi-Fi off */                       → offline            (NET_WIFI_OFF) → STARTUP_TARGET
```
("Settings → Wi-Fi setup" is the Phase-4 target; the interim Phase-2 build launches the standalone
`app_setup`, §7A.4. No-credentials always wins over the configured startup target.)
(The always-on **sync** task that consumes the link is Phase 3; Phase 2 delivers only the link +
accurate `net_status`.)

### 7A.4 Setup app — **folded into Settings** (`app_settings`) ✅

> **Done (brought forward from Phase 4 to unblock step 12's offline verification).** Wi-Fi setup is no
> longer a standalone app — it's the **Wi-Fi setup item in the Settings hub** (§6). The interim
> `app_setup` was **removed**; its portal logic is now the *Wi-Fi setup* action inside `app_settings.c`
> (a `MODE_PORTAL` sub-mode). The boot branch auto-opens **Settings → Wi-Fi setup** when unprovisioned
> / Home-held (`app_settings_enter_setup()` + `initial = app_settings_get()`). `wifi_mgr` /
> `softap_portal` / `nvs_config` are unaffected — only the owning app changed.
>
> Settings menu (ui_list): **Wi-Fi: On/Off** — the master toggle that persists `WIFI_EN` and drives
> `wifi_mgr_start_sta()` / `wifi_mgr_stop()` (this is how the device + task apps go offline/online,
> §8.3 / step 12) — and **Wi-Fi setup** (raise the portal). Device info / factory reset / restart /
> OTA-stub + the power/idle scaffolding remain for the rest of Phase 4 (§ build-order row 4).

A non-removable core `device_app_t` (§6), reachable from the Launcher anytime and auto-launched at
boot when unprovisioned.
- `init()` — bring the portal up (`provisioning_start()`), `net_status_set(NET_PORTAL)`, draw the
  instructional screen. If launched while already connected, raise **AP (or APSTA) for the session**.
- `render()` — instructional only: network name, `192.168.4.1`, and a live status line
  (Waiting → Connecting → Saved ✓ → Error). The knob never enters characters.
- `on_event()` — minimal (e.g. Select = rescan APs); the phone drives the flow.
- `exit()` — **total teardown (§6A):** stop the HTTP server + SoftAP and **restore the prior Wi-Fi
  state** via `wifi_mgr_refresh_status()` (a live STA link is preserved across the AP cycle). Must be
  leak-clean (7A.7). *(Done in step 7.)*

> **Step 7 unified the Wi-Fi init:** `wifi_mgr` is now the single owner of `esp_wifi_*` and coordinates
> mode from two independent wants (STA / AP → AP+STA). `softap_portal` is just the captive HTTP + DNS
> on top. This is what lets the Setup app raise the AP **while a station link stays up** — verified on
> hardware: STA connected → AP+STA up (no STA drop) → AP down → STA still connected.

### 7A.5 The form: GET → scan → POST → validate → commit
1. `GET /` (+ captive wildcard) → one page **assembled dynamically**: a **core section** (Wi-Fi
   SSID/password from the scan, OTA `fw_url`) **+ one section per installed app** of its `ACFG_PASTE`
   fields (§9.4), secrets as password fields, SSID with a `<datalist>`. Core names no app — the app
   sections come from the `app_config` registry.
2. `GET /scan` → JSON of nearby APs (`esp_wifi_scan_*`) to populate the SSID picker.
3. `POST /save` → parse the url-encoded body → route each field: `wifi_*`/`fw_url` → `nvs_config`;
   `cfg.<ns>.<key>` → the app's `app_store` namespace (§9.4).
4. **Save → respond → reboot → connect** (revised — see note): require a non-empty `wifi_ssid`, set
   `provisioned=1`, return a "Saved, restarting to connect" page, then `esp_restart()` after the
   response flushes. On reboot the boot-mode branch (§7A.3) connects as a station.

> **Why not live "validate-before-commit"?** In **AP+STA**, associating the STA drags the SoftAP onto
> the home AP's channel, which knocks the phone off our setup AP — so a *failure* page can't reliably
> be delivered back to the phone. We therefore commit + reboot + connect, and rely on the existing
> **Home-held-at-boot escape hatch** (§7A.3) so bad creds never lock the device out of re-provisioning
> (a future connect-timeout auto-fallback to Setup is Phase-5 hardening). The optional source
> `/health` validation moves to Phase 3 sync, where the link already exists.

### 7A.6 `net_status` ownership after Phase 2
| State | Set by |
|---|---|
| `NET_PORTAL` | Setup app `init()` |
| `NET_CONNECTING` / `NET_CONNECTED` / `NET_DISCONNECTED` | `wifi_mgr` event handlers |
| `NET_WIFI_OFF` | boot / Settings `WIFI_EN=0` (Phase 4 UI; key + behavior honored from Phase 2) |

Offline rendering (cached tasks + `OFFLINE`) is Phase 3's Task Manager (§8.3); Phase 2 just makes the
state correct.

### 7A.7 §6A.4 leak harness ✅ (done)
- `CONFIG_TM_LEAK_TEST` (component `Kconfig`) + `sdkconfig.ci` overlay (adds
  `CONFIG_HEAP_POISONING_COMPREHENSIVE`). Build the variant in a side dir:
  `idf.py -B build_leak -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ci" -D SDKCONFIG=sdkconfig.leak build flash`.
- `leak_test.c` runs on the UI task at boot: one warm-up cycle to absorb one-time allocs, then N
  launch→event→render→Home cycles per app, checking the free heap returns to baseline, plus
  `heap_caps_check_integrity_all()`. A standing per-app gate (§17).
- **Verified on hardware** (poisoning on): `Hello` Δ=+16, `Setup` Δ=−88 (20 cycles incl. HTTP+AP
  up/down) — both **PASS**, no integrity failure, no panic.

### 7A.8 Security touchpoints (§10)
Open `TaskMaster-Setup` AP is up **only** during setup and **auto-disables** on success/timeout (never
left up). PSK + tokens cross the open link during the brief paste window — acceptable for home setup,
documented. NVS encryption stays deferred ("enable late").

### 7A.9 Build order (each step independently testable)
1. `nvs_config` + host unit test (schema round-trip).
2. `wifi_mgr` STA connect against **pre-seeded** NVS creds; verify `NET_CONNECTING → NET_CONNECTED` on
   hardware (the inline net indicator flips `SETUP → … → OK`).
3. Boot-mode branch on `provisioned` / Home-held / `WIFI_EN`.
4. `app_setup` shell (core app, auto-launch, instructional OLED, AP via the Phase-0 portal).
5. Schema-driven `GET /` form + `GET /scan`.
6. `POST /save` → NVS → validate → connect → commit; failure re-prompt.
7. Re-provision-from-Launcher (APSTA/restore on `exit()`).
8. Leak harness + run; AP auto-disable + timeouts; error/edge polish.

### 7A.10 Exit criteria (§14)
Join `TaskMaster-Setup`, paste the full config in one form, persist to NVS, **associate**, survive
reboot (auto-connects, boots to Launcher); the Setup app auto-launches when unprovisioned and is
reachable from the Launcher; **and** the §6A.4 leak-clean cycle passes on a heap-poisoning debug build.

### 7A.11 Defaults chosen (reversible)
- **Save → reboot → connect** (revised from validate-before-commit): the AP+STA channel hop makes a
  live failure response unreliable, so we commit + reboot + connect, with the Home-held escape hatch
  (§7A.3) preventing any bad-creds lockout. (Verified on hardware: form → save 7 fields → reboot →
  associate → got IP.)
- **Re-provision uses AP/APSTA for the session**, restored on `exit()` — you can always re-provision,
  even while "offline."
- **Offline write actions:** deferred to Phase 3 (queue vs disable) — no write path exists yet in P2.

---

## 8. Data & Sync Strategy — tasks are **userspace** (core knows nothing about them)

**Restructure (decided):** tasks are **purged from core** — `task_t`, fetching, parsing, and the
task-list formatting all live in **apps**, not the firmware core (§11.2). Core provides only
*task-agnostic* facilities the apps build on: a **generic scrollable list widget** (also used by the
Launcher/Settings) and a **generic async job service** (so an app can fetch off the UI task without
spawning its own task, §6A.2). The shared abstraction between source apps is just the **`task_t`
model + the list formatting**, kept thin per-app (decided: "generic list in core + thin per-app").

Two source apps, **each fetching its own way**:
- **App: "Yapp"** (`~/yappcloud`) → talks **directly to Todoist** over HTTPS (no proxy, decided): the
  device hits `api.todoist.com` with the user's token, parses Todoist's JSON into `task_t`. The
  `todomark`-on-hardware experience, no server to run.
- **App: "Local"** (`~/yapplocal`) → a **platform-agnostic LAN box** speaking the simple device
  contract (§8.1) over whatever it likes (local DB, file, custom system — not Todoist).

So the old "one REST contract for *both* sources" is relaxed: the contract (§8.1) is now **just the
Local source's interface**; Yapp bypasses it and speaks Todoist directly. The device stays decoupled
(any app can add a source); core stays vendor- and task-agnostic.

> **Why direct Todoist is OK on the C3:** ESP-IDF's `esp_crt_bundle` (Mozilla roots) + `esp_http_client`
> + `esp-tls` do TLS 1.2; a session costs ~40–50 KB heap *during* the fetch and we hold ~190 KB free
> (leak-test watermark). The app opens the TLS connection, fetches, parses into the bounded `task_t`
> array, and tears the session down — not holding it. Response size is capped (`TASKMASTER_MAX_TASKS`).

### 8.1 The Local-source contract (the "known endpoints") — yapplocal only
The **Local** source (`~/yapplocal`) exposes a small REST contract; the device's Local app speaks it.
(Yapp does **not** — it talks to Todoist directly, §8.) Any LAN server implementing this is a valid
Local source:
```
GET  /tasks         → { "tasks":[ { id, title(≤N), priority(1-4), due, parent_id, done } ], "etag" }
GET  /tasks/{id}    → { id, title, …, description }  → detail view (optional; 404 → no description)
POST /tasks/{id}/complete                          → mark complete
POST /tasks/{id}/postpone   { "due":"tomorrow" }   → reschedule (optional; 501 if unsupported)
GET  /health                                       → liveness (shown inline by the app)
```
Rules: flat JSON, server-truncated titles, bounded task count (e.g. ≤`TASKMASTER_MAX_TASKS`),
`priority` 1–4 (4 = highest, matching Todoist). `etag` lets the app skip re-rendering an unchanged
list. Optional endpoints may return `501` and the UI hides that action. The contract types + parse
live in the **Local app** (userspace), not core.

- **Yapp (direct Todoist):** the app calls `GET https://api.todoist.com/rest/v2/tasks` with a Bearer
  token, and maps Todoist's JSON (`content`, `priority` 1–4, `due.date`, `parent_id`, `is_completed`)
  into the same `task_t` model. Complete = `POST …/tasks/{id}/close`; postpone = `POST …/tasks/{id}`
  with `due_string`. The Todoist token is the app's declared config (§9.4).
- **Local (LAN box)** implements the four endpoints above however it wants — the contract is the spec.

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

**Offline (Wi-Fi off, `WIFI_EN=0`, §8A):** the network task is idle and no sync is attempted. The
Task Manager renders the **last cached tasks** with an **OFFLINE** status-bar indicator; "Sync now"
is unavailable, and write actions (complete/postpone) are disabled (Phase 3 may instead queue them to
replay on reconnect — decided when the write path lands). The same offline rendering covers a dropped
Wi-Fi connection, so it's one code path, not two.

### 8.4 Transport / TLS
Default to **plain HTTP on the LAN** for both sources (no on-device TLS — the big SRAM saver),
appropriate for a desktop appliance on a trusted home network. Keep **TLS a compile-time toggle**
(`esp-tls` + CA bundle) so a remotely-hosted `yapp-server` can be reached over HTTPS later.

**Stretch goal:** an app that talks to Todoist directly (TLS + larger buffers) once MVP is proven.

### 8.5 Phase 3 — build plan (UI foundation → sync → Task Manager)

**Theme:** adopt **LVGL** (Part A, done), then make the device *show and act on real tasks* — but with
**tasks living entirely in userspace** (§8). Core grows two *task-agnostic* facilities (a generic list
widget + an async job service); each source **app** does its own fetch/parse into `task_t` and renders
via the core list. End state: open the Yapp app → it pulls **directly from Todoist** → priority-sorted,
nested tasks → complete/postpone; the Local app does the same over its LAN contract; Wi-Fi off → cached
tasks + `OFFLINE`.

> **Order matters:** the UI foundation (Part A) lands *first* — building `app_tasks` on the raw
> renderer and re-porting it to LVGL later would be wasted work. The existing screens (Launcher,
> Setup, Hello) migrate to LVGL in Part A so all apps share one look.

#### 8.5.1 New pieces (post-restructure — **core stays task-free**, §8)
| Piece | Where | Role |
|---|---|---|
| LVGL (`lvgl/lvgl`) | core managed dep | LVGL on the panel via the `sh1106` framebuffer (done, Part A) |
| `ui_frame` | `taskmaster_core` | OS frame: optional right hint bar + content area (done, Part A) |
| **`ui_list`** (generic) | `taskmaster_core` | **Task-agnostic** scrollable/selectable list widget — used by Launcher, Settings, and app task lists |
| **`async_job`** (generic) | `taskmaster_core` | **Task-agnostic** worker service: submit work off the UI task → result delivered on the UI task; cancel-on-app-exit (§6A) |
| `task_t` + parse + render | **app side** (`~/yappcloud`, `~/yapplocal`) | The task model, fetch, JSON parse, and task→list formatting — **not in core** |
| `~/yapplocal` | own repo (host + app) | LAN box over the §8.1 contract + the Local device app |
| `~/yappcloud` | own repo (**app only**) | The Yapp device app talking **directly to Todoist** — no host server (proxy dropped) |

#### 8.5.2 Build order — Part A: LVGL UI foundation
1. **LVGL bring-up.** ✅ **Done.** LVGL **9.4** (`lvgl/lvgl` managed dep) driven onto the panel
   **through our existing `sh1106` framebuffer** — a custom **I1** flush callback (`lvgl_disp.c`) maps
   LVGL's 1-bit, MSB-first, full-refresh buffer to `sh1106_pixel` + `sh1106_flush` (no `esp_lcd` panel,
   so the 2-px offset + I2C stay in the proven driver). Tick via `esp_timer`; `lv_timer_handler` pumped
   on a dedicated task. **Font: `UNSCII_8`** (1-bit) as the default — anti-aliased fonts (Montserrat)
   **dither to "dots"** on a mono panel; UNSCII is crisp *and* brings lowercase (retiring the 5×7
   uppercase-only bring-up font). Verified on hardware: solid box + crisp `LVGL ok 123`. Footprint:
   ~**+187 KB** flash when used (still 44% free in the OTA slot); ~0 while gc'd. *(Note: LVGL ships a
   `CMakePresets.json` that can wedge CMake configure — delete it if a fresh build trips on "hidden
   preset `_base`".)* LVGL-port/`esp_lcd` not needed; Kconfig-trim deferred to when RAM/flash pressure
   warrants.
2. **OS UI frame.** An LVGL layer the OS owns: the optional **right hint bar** (20px column, 3 boxes —
   Home / encoder-split / Select, §6) plus a **content container** apps draw into (108×64 with the bar,
   128×64 without it, per the app's `hint_bar` mode). `ui_set_hints()` updates the bar; **no status
   bar** — apps surface connectivity via `net_status.h` if they want it.
3. **Widget lifecycle (§6A).** ✅ **Done.** Each app builds its widgets into the OS frame's **content
   container**; on app switch `app_manager_switch_to()` calls `ui_frame_reset_content()` →
   `lv_obj_clean(content)`, freeing the whole subtree in one call. **Deviation from "screen per app":**
   one persistent screen + content-clean gives the identical §6A guarantee (whole tree freed at once)
   with far less complexity — the OS frame (hint bar) persists across switches. **Threading:** the UI
   task is the *single* LVGL owner — it pumps `lv_timer_handler` in its own loop (timed input receive),
   so no separate task and no mutex. (UI task stack raised to `UI_TASK_STACK`=8KB for LVGL rendering.)
4. **Port existing screens.** ✅ **Done.** Launcher (LVGL list, cursor + wrap), Hello, and Setup ported
   via a tiny `ui_text`/`ui_text_row` helper layer. **Two fonts:** `UNSCII_8` for content (has
   lowercase) and the generated thin `lv_font_tm5x7` for the tight hint-bar boxes. Setup demonstrates
   the **hint-bar-off / full-width** mode. Verified on hardware: all Phase 0–2 flows work in LVGL.
   *(Priority/nesting glyphs land with the Task Manager in Part B.)*

#### 8.5.3 Build order — Part B: tasks in userspace (restructured per §8)
Steps 5/6/6.5 are **done** but partly superseded by the restructure — see 5★/6★ below.

- **5★ — task model: move OUT of core.** Step 5 built `task_model.[ch]` *in core*; the restructure
  (§8) makes tasks userspace, so the `task_t` types + model move to the apps (or their thin shared
  copy). Core's `task_model` is **removed**, and the Launcher's inline `sync` indicator (task-specific)
  is **removed**. `net_status` stays in core (connectivity is universal). *(`task_t` fields/bounds from
  step 5 are reused verbatim, just relocated.)*
- **6★ — host servers: drop the Yapp proxy.** `~/yapplocal`'s LAN server stays. **`~/yappcloud`'s
  `yapp_server` proxy is dropped** (Yapp now talks to Todoist directly, §8) — that repo becomes
  app-only.
- **6.5 — App-declared config facility (§9.4).** ✅ **Done** (unchanged by the restructure).

7. **Core: generic `ui_list` widget (task-agnostic).** A scrollable, selectable list: feed it rows
   (text + optional leading glyph + indent level), it handles the scroll window, selection highlight,
   and wrap. **Refactor the Launcher to use it** (it hand-rolls a list today), proving it's generic.
   Settings and the app task lists reuse it. No task concepts in it.
8. **Core: generic `async_job` service (task-agnostic).** ✅ **Done.** `async_job_submit(work, done,
   ctx, ctx_size)` → runs `work` on a **core-owned worker task**, delivers to `done` **on the UI task**
   (via `EV_SYS_JOB_DONE`); apps do background I/O *without* spawning their own tasks (§6A.2). Results
   land on the UI task, so an app's `task_t` model is **UI-task-only → no mutex**. **Verified on
   hardware:** a 1.5 s worker job ran off the UI task (UI stayed live) and delivered `out=42` on the UI
   task. Single worker (one job at a time; submit returns NULL if busy). App-author docs in
   `docs/APP_API.md` §8.
   - **Cancellation must be cooperative — never `vTaskDelete` a blocked worker.** A worker stuck in
     `esp_http_client_perform()` holds an open socket + a ~40 KB TLS session; hard-killing it **orphans
     that memory** (a silent leak that defeats §6A.4). `async_job_cancel()` sets a **cancel flag** the
     worker polls (in its read loop) and bails; the worker frees *its own* client + returns.
   - **The client handle is worker-LOCAL, and the UI thread never touches it (step-13 correction).** An
     earlier design had the abort hook call `esp_http_client_close()` on the in-flight handle from the
     UI task to unblock a read — but `esp_http_client` is **not thread-safe**, so closing it while the
     worker is inside `read()`/`open()` is a data race that **crashed on Home-mid-fetch** (verified:
     repeated reboots; fixed → zero reboots). So cancel is flag-only; a cancelled fetch simply runs to
     completion in the worker (bounded by `timeout_ms`) and its result is dropped by `deliver`. The
     `async_job_on_cancel` abort hook remains for *thread-safe* signals only.
   - **`exit()` runs on the UI task and must stay fast**, so it *requests* cancel and returns; it does
     **not** join a worker that could be on a multi-second TLS timeout. `work` only ever touches the
     core-owned **ctx copy** (never app statics), and `done_fn` is skipped once cancelled — so a worker
     that keeps running after the app exits can never write into freed app state.
9. **App-side task model + render (thin, per app).** ✅ **Done.** `tasks.[ch]` in the app repo: a
   bounded `task_t[]` (§6A.1) + a `task_view` that formats tasks into `ui_list` rows — priority marker
   `P1..P4` (P1 = highest) + `parent_id` nesting (indent) + title; empty state "No open tasks". Built +
   verified in the **Local app** with static canned tasks (priority-sorted, nested, scrolls, fits left
   of the hint bar). Also fixed `ui_list` to pin rows one-line (width × `UI_ROW_H`): selected scrolls,
   others ellipsis. `tasks.[ch]` is copied into yappcloud at step 11. *(Due-on-detail moves to the
   step-10 submenu.)*
10. **Local app (yapplocal): fetch over the LAN contract.** ✅ **Done.** `async_job` → `esp_http_client`
    GET `<url>/tasks` (base URL from app config §9.4, ns `local`) → **cJSON** parse (`espressif/cjson`,
    not bundled in IDF v6) into `task_t[]` → render via `ui_list`. *Select* = complete (`POST
    …/complete`, **optimistic remove + re-sync**); encoder *click* = Sync now; no URL → "Set URL in
    setup". `exit()` cancels an in-flight fetch (cooperative). **Verified on hardware:** device fetched
    4 tasks from the LAN server, parsed + rendered; completing tasks POSTed + re-synced (server store
    emptied). Encoder *click* now opens the **detail submenu** (below), not Sync-now directly.
    - **Detail submenu + hide-when-unconfigured (polish) — ✅ Done + verified on hardware.** Encoder
      *click* on a task opens a `ui_list` submenu — **Details / Postpone / Sync now / Back** (shared in
      `tasks.h`: `task_detail_t`, `task_menu_render`, `task_desc_render`); *Select* still one-tap
      completes. **Details** shows the task's **due date** (from the cached `task_t`, no fetch) + the
      **description** fetched **on-demand** (`GET …/tasks/{id}` → `description`, wrapped via core
      `ui_text_wrap`; "(no description)" when empty — no per-task RAM cost). **Postpone → tomorrow**
      (`POST …/tasks/{id}/postpone {"due":"tomorrow"}` for Local — optional, 501 = no-op; Yapp:
      `POST …/tasks/{id} {"due_string":"tomorrow"}`, verified `ok=1 status=200`). The submenu is
      **online-only** (its actions need the network; offline the LIST click hint shows `---`).
      **Hide-when-unconfigured:** core `device_app_t.available()` (NULL = always shown) + the Launcher
      filters to available apps; Local hides with no URL, Yapp hides with no token, so the Launcher only
      lists usable apps (configure them via the provisioning form, §9.4).
11. **Yapp app (yappcloud): fetch Todoist directly (HTTPS).** ✅ **Done.** `async_job` → `esp_http_client`
    + `esp-tls` + `esp_crt_bundle` → `GET https://api.todoist.com/api/v1/tasks` with the Bearer token
    (app config, §9.4) → parse into the same `task_t[]` → render. Complete = `POST …/tasks/{id}/close`
    (verified on hw). Open→fetch→parse→**close the TLS session** (don't hold it). Token from form
    provisioning (ns `yapp`, key `token`).
    - **Endpoint migration:** Todoist **retired `/rest/v2/`** (HTTP 410 Gone). The current **unified API
      v1** is `GET /api/v1/tasks`, which returns `{"results":[...], "next_cursor":…}` (not a bare array)
      — the parser unwraps `results`; `next_cursor` pagination is ignored (first page, capped at
      `TASK_MAX`). Field map: `content`→title, `checked`→done, plus `id`/`priority`/`parent_id`/`due.date`.
    - **Verified on hardware:** 6 real tasks fetched + parsed + rendered; complete (`POST …/{id}/close`)
      returns 2xx and the task drops; heap min ~46 KB during the TLS handshake (healthy margin — the
      direct-Todoist path is feasible on the C3 as predicted, no proxy needed). cJSON used on a capped
      buffer (small account); jsmn/SAX remains the documented upgrade path below if a large account
      proves too heavy.
    - **Parsing heavy payloads — don't DOM the whole tree.** cJSON builds the full JSON tree (~3–5× the
      body); a large Todoist account could blow the ~190 KB heap (with TLS already taking ~40–50 KB).
      Strategy: **(1) bound the request** — use a Todoist **filter** (e.g. due-before/overdue, or a
      project) so the server returns fewer tasks; **(2) hard-cap the read buffer** and bail with a clear
      "too many tasks — narrow the filter" if exceeded; **(3) parse light** — read the body in
      `esp_http_client` chunks and use **jsmn or a hand-rolled field scanner** for just the 6 fields we
      need (`id`, `content`, `priority`, `due.date`, `parent_id`, `is_completed`), not cJSON. (jsmn still
      buffers the body but drops cJSON's node overhead; a true chunked SAX pass keeps memory flat — pick
      per measured headroom at build time.) **Fallback:** the `~/yapplocal`-style proxy stays a
      documented escape hatch if a real account proves too heavy to parse on-device.
12. **Offline + write semantics.** ✅ **Done + verified on hardware** (via the Settings Wi-Fi toggle:
    Wi-Fi off → Yapp shows cached list + OFFLINE banner + `OFF` hint; Select queued the close
    (`OFFLINE 1 queued`); Wi-Fi on → on reopen it replayed `close … ok=1 status=204` and re-synced).
    `WIFI_EN=0` / dropped link → render **cached** `task_t[]` + an `OFFLINE` marker,
    "Sync now" unavailable, writes **queued** (bounded) to replay on reconnect — one path for both
    off-by-toggle and dropped (§8.3). Each app owns its cache (userspace).
    - **Shared in `tasks.h`** (both apps, header-only, identical copies): an NVS-blob **cache** of the
      `task_t[]` — saved on `exit()` (not per-sync, to bound flash wear), loaded on `init()` so tasks
      show instantly even offline / after a reboot; a bounded **`task_queue_t`** (`TASK_QUEUE_MAX`) of
      completes done offline, persisted immediately; and `task_view_render_offline()` which draws an
      `OFFLINE` / `OFFLINE  N queued` banner (chosen UX) above the cached list, shrinking the `ui_list`
      window by one row (new core `ui_list_set_rows()`).
    - **One path:** everything keys off `!net_is_online()`, so a user toggle-off and a dropped link are
      identical. Offline the click hint flips **SYN → OFF** (sync unavailable).
    - **Replay on reconnect:** the UI task re-runs `render()` on every connectivity change, so
      `render()` detects the offline→online edge and drains the queue via **chained close/complete
      jobs** (`async_job`), then re-syncs — no extra task or timer. A failed replay is left queued and
      retried on the next reconnect (no server hammering); a **poison entry** (keeps failing) is dropped
      after `TASK_QUEUE_TRIES` attempts (step 13) so the queue can't wedge.
    - **Verified** via the Settings app's **Wi-Fi: On/Off** toggle (§7A.4): toggle off → offline banner
      + cached list + `OFF` hint; complete → queued; toggle Wi-Fi on → replay `close … ok=1 status=204`
      then re-sync. (A replay that can't reach its server — e.g. the Local LAN server from a foreign
      network — stays queued and retries on the next reconnect, as designed.)
13. **Harden + §6A.4 gate.** ✅ **Done + verified on hardware.**
    - **Home fired mid-fetch — found + fixed a crash.** The abort hook closed the shared
      `esp_http_client` handle from the UI task while the worker was inside `read()`/`open()` — a data
      race (client isn't thread-safe) that **rebooted the device** on Home-mid-fetch. Fix: the handle is
      now **worker-local**, cancel is **flag-only** (read loop polls `async_job_cancelled()`), the UI
      thread never touches the client. Re-verified: the exact repro now shows **zero reboots / zero
      panics** across ~15 fast open→Home cycles. Docs corrected (`async_job.h`, step 8 above).
    - **Poison-queue guard.** A queued write that keeps failing is dropped after `TASK_QUEUE_TRIES` (5)
      so a bad id / permanently-unreachable server can't wedge the queue + OFFLINE banner forever;
      transient fails still retry on the next reconnect.
    - **§6A.4 leak gate.** The launch→Home→relaunch harness under `CONFIG_HEAP_POISONING_COMPREHENSIVE`
      **PASSes on all 4 apps** (Hello / Local / Yapp / Settings) — 100-cycle deltas −44/−36/−48/−12 B
      (noise, not linear), heap integrity clean. (Hello's 20-cycle −336 was one-time NVS write-cache,
      confirmed one-time by the flat 100-cycle result — a real leak would be ~−1680.)
    - **Bounds/caps audited:** body reads hard-capped (`YAPP_BODY_MAX`/`LOCAL_BODY_MAX`), `TASK_MAX`
      tasks, `TASK_QUEUE_MAX` queued writes, `strlcpy`/`snprintf` throughout. **Source-down → cached:** a
      failed sync sets the error flag but never clears `s_view`, so the cached list keeps rendering
      (the "Sync failed" screen shows only when there's no cache).

#### 8.5.4 Design decisions / defaults (revisit if needed)
- **Tasks are userspace; core stays task-free** (§8). Core's only task-enabling additions are the
  *generic* `ui_list` + `async_job` — neither knows what a task is.
- **No app-owned tasks** (§6A.2 holds): background I/O goes through core `async_job`; results land on
  the UI task, so app models need **no mutex**.
- **Yapp = direct Todoist** (no proxy); **Local = LAN contract** (§8.1). `task_t` is the shared model;
  fetch/parse differ per app.
- **Optimistic writes**, offline writes **queued** (bounded), confirmed on next fetch.

#### 8.5.5 Exit criteria (§14 Phase 3)
LVGL is the UI foundation (Phase 0–2 screens ported, leak-clean); **core is task-free** (no `task_t`/
sync in core); the **Yapp app pulls directly from Todoist** and the **Local app over its LAN contract**,
both showing priority-sorted, nested tasks via the generic `ui_list`; complete + postpone work;
**Wi-Fi-off shows cached tasks + `OFFLINE`**; each source app passes the §6A.4 leak gate (incl.
Home-mid-fetch via `async_job_cancel`).

---

## 8A. Power, Sleep & Startup Behavior (Settings-driven, battery-ready)

USB-C powered today, but these controls are built now so the **future battery build needs no
rearchitecting**. Values live in NVS, edited in the **Settings app** (§6), not the provisioning form.

- **Startup behavior:** on boot `app_manager` reads `STARTUP_TARGET` → Launcher (default), a chosen
  app (e.g. "Yapp"), or last-used. **Override:** if **no Wi-Fi credentials are saved** (unprovisioned),
  `STARTUP_TARGET` is ignored and the device boots straight into **Settings → Wi-Fi setup** so it can
  be provisioned first (§7A.3). Home still always returns to the Launcher (§5.2).
- **Inactivity timeout:** an idle timer (reset by any input event) fires after `IDLE_TIMEOUT_S`.
  **Stage 1:** blank/dim the OLED (saves the panel, kills burn-in) and pause the render loop.
  **Stage 2 (only if deep sleep on):** enter low power.
- **Deep sleep toggle (`DEEP_SLEEP_EN`):**
  - *Off (default, USB build):* timeout only blanks the screen; Wi-Fi + sync stay alive; input wakes instantly.
  - *On (battery-oriented):* after timeout enter low power. Prefer **light sleep** (`esp_light_sleep`,
    Wi-Fi state retained, fast wake) over full deep sleep initially — deep sleep tears down Wi-Fi/RAM
    and forces a reconnect, jarring for a task display. Expose the toggle as "deep sleep," implement
    light sleep first; revisit with real battery hardware.
- **Wi-Fi master toggle (`WIFI_EN`):** the user can turn the **radio fully off** to save battery or
  stay offline — Wi-Fi is the single biggest power draw, so this is the largest non-sleep lever.
  - *On (default):* normal Station + sync.
  - *Off:* the network task stops, the radio is powered down (`esp_wifi_stop`), and the device runs
    **offline** — the Task Manager shows the last cached tasks with an **OFFLINE** status (§8.3);
    write actions (complete/postpone) are disabled or queued (Phase 3 decides). Applied immediately on
    toggle (settings persist on change, §9) and honored at boot.
  - **Exception:** opening **Wi-Fi setup** (Settings) temporarily brings the radio up (SoftAP) for the
    provisioning session regardless of `WIFI_EN`, then restores the toggle's state on exit — you can
    always re-provision even while "offline."
- **Wake sources:** encoder rotation + the Select/Home buttons configured as GPIO wake sources
  (`esp_sleep_enable_gpio_wakeup`) so the device returns on first touch.
- **Implementation hook:** ESP-IDF **power management** (`esp_pm`, DFS + automatic light sleep) so
  idle→low-power transitions are kernel-managed, keeping the path clean for real duty-cycling later.

### 8A.1 Phase 4 — build order (Settings hub build-out + power)

**Theme:** finish the Settings hub as a **schema-driven** menu and land the power/idle scaffolding.
Core stays app-agnostic — the *same* editor renders core settings and each app's declared `ACFG_KNOB`
(§9.4), so core never hardcodes an app. (Foundation already done, brought forward: the Settings app
shell, Wi-Fi on/off, Wi-Fi setup, boot-to-setup, hide-when-unconfigured; the NVS keys `startup_tgt` /
`deep_sleep` / `idle_to_s` / `fw_url` and `config_factory_reset()` already exist — Phase 4 is mostly
wiring UI + behavior onto them.)

0. **Generic settings editor + confirm dialog (core primitives) — ✅ Done + verified on hardware.**
   `settings_menu.[ch]`: a declarative row model — `{ label, kind (TOGGLE / ENUM / RANGE / ACTION),
   get()/set() accessors, choices | min/max/step, action + optional confirm } ` — plus one
   `ui_list`-based editor: rotate picks a row, Select enters edit, rotate changes the value (cycle enum
   / bump number / flip toggle) applied live via `set()`, ACTION invokes (behind confirm). Core settings
   are declared as rows; per-app `ACFG_KNOB` will reuse the same editor (§9.4), so core names no app.
   `confirm.[ch]`: a reusable modal yes/no dialog (state cleared before the callback so a rebooting
   action can't re-enter a live modal). `app_settings` rewritten onto both. *(Schema-driven over bespoke
   per-screen — matches the declarative `nvs_config` / `app_config` tables.)* **Verified:** Wi-Fi TOGGLE
   flips in place; Restart + Factory reset run behind the confirm dialog.
1. **Quick wins.** **Restart** (`esp_restart`) + **Factory reset** (`config_factory_reset()` behind the
   confirm dialog → reboot into Wi-Fi setup) — ✅ done (landed with step 0). **Remaining: Device /
   network info** (read-only: IP, MAC, RSSI, SSID, **firmware version + build date**, free heap, uptime).
2. **Startup behavior** (Launcher / a specific app / last-used) — a schema ENUM over `startup_tgt`,
   honored at boot by `app_manager` (unprovisioned still overrides → Wi-Fi setup, §7A.3).
3. **OLED brightness / contrast** — a schema RANGE driving the SH1106 contrast register; applied live +
   persisted. *(Added to Settings per decision.)*
4. **Inactivity timeout** (Off / 30s / 1m / 5m / 15m — schema ENUM over `idle_to_s`) + the idle timer:
   **Stage 1** blank/dim the OLED + pause the render loop; any input restores instantly.
5. **Deep-sleep toggle + light sleep.** First convert input from the **1 ms poll → interrupt /
   GPIO-wake** (the one foundational change, §4.6 deferral), then **Stage 2**: `esp_light_sleep` /
   `esp_pm` with encoder + Select/Home as wake sources. Labelled "deep sleep," light sleep under the
   hood first (§8A).
6. **Per-app config in Settings** — the editor iterates the `app_config` groups (§9.4): `ACFG_KNOB`
   scalars knob-editable, `ACFG_PASTE` strings shown masked with a **re-provision** shortcut that opens
   the form. Falls out of step 0.
7. **OTA update (Phase 4.5).** Stub / "coming soon" until then; the real path pulls a signed image from
   `fw_url` via `esp_https_ota` with rollback (§9).

**Decisions / out of scope (this phase):**
- **Sync interval is app-owned, not a core setting** (core⟂app, §11.2). An app either **hardcodes** it
  or **declares it as an `ACFG_KNOB`** (rendered by the shared editor, stored in the app's own
  namespace — the §9.4 `sync_min` example). If periodic sync is wanted, core exposes a generic **tick
  event** apps *may* consume; the interval value stays app-side and core adds no sync knob. *(Task apps
  today sync on entry + on demand; no periodic poll exists yet.)*
- **Encoder long-press / acceleration** stays **out of core navigation** (keeps nav simple). Core may
  deliver an optional **long-press event** that apps can opt into for their own gestures.
- **NTP time + status bar** (time / weather, §6C) — deferred, not this phase.

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
- **Slot budget watch (⚠):** the full app (LVGL + Wi-Fi + HTTP + app code) must fit one ~1.9MB slot;
  it's already ~1.1MB. Keep the LVGL Kconfig trim (§4.4) honest; track image size in CI. **Phase-5 BLE
  is the real squeeze:** the Bluedroid stack is ~500KB+ — use **NimBLE** (much lighter) and trim BLE
  Kconfig (no classic BT, minimal GATT) so the BLE-provisioning build still fits.
- **Update delivery:** the network task pulls a signed image from a configurable **`FW_URL`** (served
  by the LAN box or `yapp-server`) via `esp_https_ota`; the bootloader boots the new slot and
  **rolls back** if the app fails to self-confirm (`esp_ota_mark_app_valid_cancel_rollback`).
- **Dev flashing** stays `idf.py flash` over USB-C; OTA is the field path.

### 9.2 Core NVS config schema — **core fields only** (no app config; §9.4)
The core schema holds **only universal device/system config** — never anything app-specific (app
config is app-declared, §9.4, so core never names an app):

| Key | Type | Size | Source | Purpose |
|---|---|---|---|---|
| `wifi_ssid` | str | 32B | provisioning | Target AP SSID |
| `wifi_psk` | str | 64B | provisioning | WPA2 PSK |
| `fw_url` | str | 96B | provisioning | OTA firmware image URL (optional; OTA is core) |
| `startup_tgt` | u8/str | 16B | settings | Boot target (§8A) |
| `wifi_en` | u8 | 1B | settings | Wi-Fi master on/off — radio off for battery/offline (§8A); default on |
| `deep_sleep` | u8 | 1B | settings | Deep-sleep toggle (§8A) |
| `idle_to_s` | u16 | 2B | settings | Inactivity timeout seconds (0 = off) |
| `provisioned` | u8 | 1B | system | Boot flag → Launcher vs. first-run provisioning |

> **Removed (was wrong):** `yapp_url/token`, `local_url/token` — those are **app** config and now live
> with their apps (declared via §9.4, stored in each app's `app_store` namespace). Core mentioning a
> specific app's fields broke the core⟂userspace split (§11.2).

Native `nvs_flash` handles wear-leveling. **Declarative schema, not hand-wired keys:** one table
(key, type, label, max-len, secret?, write-path) drives both the generated provisioning form and the
NVS read/write. `provisioned` is gated only on `wifi_ssid` (app config is optional). Optionally enable
**NVS encryption** for the secret keys (§10).

### 9.3 App-owned storage (`app_store.h`) — config a third-party dev *can* add

The schema above is **core-owned device config**; an external app must not edit it (that would be a
core edit / a fork, against §6.1). So app config is a **separate tier**: each app persists its own
variables in its **own private NVS namespace**, with no core changes and no schema.

```c
static app_store_t store;
app_store_open(&store, "pomodoro");                 // your unique id (any length)
uint32_t mins; app_store_get_u32(&store, "work", &mins, 25);   // default 25
app_store_set_u32(&store, "work", 30);              // persisted immediately
```

- **Isolation by namespace:** one NVS namespace per app, so app keys can't collide with each other or
  with device config. The core namespace (`tmcfg`) is **reserved** — `app_store_open()` rejects it.
- **Any-length ids:** NVS caps a namespace at 15 chars, so ids of 1..15 chars are used verbatim and
  **longer ids are hashed** (64-bit FNV-1a → base36) to a 15-char namespace automatically. Keys stay
  ≤15 chars (NVS limit).
- **Collision (documented for awareness):** distinct ids almost never map to the same namespace, but
  it's *theoretically* possible (a short literal equal to another id's hash, or two long ids hashing
  alike — a 64-bit space, astronomically unlikely). If it ever happened the two apps would share a
  namespace; **pick a distinctive id** and it's a non-issue.
- **No ceremony:** typed `get/set` for str / u32 / blob; `get_*` take a default so there's no
  first-run special case. Open in `init()`, close in `exit()`.
- **Shared, finite, unguarded pool:** all apps + device config share the **one ~24 KB `nvs`
  partition** (§9.1) with **no per-app reservation** — an app that stores nothing costs nothing. Apps
  are asked to be **frugal** (small config/state, not bulk data; docs/APP_API.md). Today nothing stops
  a greedy app from crowding out others or provisioning; **per-app budget enforcement + partition
  sizing are deferred to Phase 6** (§14).
- **Two tiers + a declared overlay:** `nvs_config` = core device config (core only, §9.2); `app_store`
  = app-internal state (any app). The **app-declared config** below (§9.4) is a thin *overlay* on
  `app_store` — the app marks some of its namespace keys as user-fillable so the OS form/Settings can
  populate them. The values still live in the app's `app_store` namespace; the app reads them the same
  way. (Pulled forward from the old "Phase 6 facility" — it's foundational to the repo split, §11.2.)

### 9.4 App-declared configuration (the OS fills it; core never names an app)
**The rule:** core's provisioning form and Settings hold only **Wi-Fi + OTA `fw_url`** (§9.2). Every
other knob — a source URL, an API token, an app preference — is **declared by the app that needs it**,
and the OS assembles the form/Settings from Wi-Fi + whatever each *installed* app contributes. Add an
app → its fields appear; remove it → they vanish; **core mentions no app**.

**Declare (app side, at boot — mirrors app registration, §6.1):**
```c
static const app_cfg_field_t YAPP_CFG[] = {
    { .key="url",   .label="Server URL", .type=ACFG_STR,  .input=ACFG_PASTE, .max_len=96  },
    { .key="token", .label="API token",  .type=ACFG_STR,  .input=ACFG_PASTE, .secret=true, .max_len=128 },
    { .key="sync_min", .label="Sync min", .type=ACFG_U16, .input=ACFG_KNOB, .min=1, .max=60 },
};
TASKMASTER_REGISTER_APP_CONFIG("yapp", "Yapp", YAPP_CFG);   /* ns, display name, fields */
```

**Two input methods (respects the no-typing-on-the-knob rule, §7):**
- **`ACFG_PASTE`** — string/secret fields (URLs, tokens). Appear in the **paste-from-phone form**
  (§7A.5), never typed on the knob. The only way to set them on-device is re-opening the form.
- **`ACFG_KNOB`** — scalar fields (bool / enum / number). Knob-editable directly in **Settings** (§6),
  with bounds (`min`/`max`/enum list). Never in the paste form.

**Mechanism:**
- A small core registry (`app_config`) collects `{ns, name, fields[]}` from each app's boot
  constructor — exactly like the app registry. `app_config_group_count()/group(i)` iterate it.
- **Form (§7A.5):** core Wi-Fi/OTA section + one section per app of its `ACFG_PASTE` fields, inputs
  named `cfg.<ns>.<key>`. `POST /save` routes `wifi_*`/`fw_url` → `nvs_config`, and each `cfg.<ns>.<key>`
  → `app_store_open(ns)` + set. **Storage reuses the app's `app_store` namespace** (decided) — no new
  store.
- **Settings (§6):** iterates the same groups, rendering `ACFG_KNOB` fields as knob-editable entries
  and listing `ACFG_PASTE` fields (masked) with a **"re-provision"** shortcut that opens the form.
- **App reads** its config with the `app_store_get_*` it already uses (`app_store_get_str(ns,"url")`).
  A source with **no URL stays hidden** in the Launcher (§8.5).

This is the clean core⟂userspace boundary (§11.2): the *only* core knowledge is "apps may declare
config"; the *what* lives entirely in each app's repo.

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

Target structure — a thin firmware project + a reusable **`taskmaster_core`** component + **one
component per app** (§6.1). Apps are added/removed in `main/idf_component.yml`.

```
TaskMaster/                          ← firmware (config) repo
├─ PLAN.md
├─ sdkconfig.defaults                ← Kconfig: Wi-Fi, esp_http_server, OTA, PM, LVGL trim, partitions
├─ partitions.csv                    ← nvs / otadata / phy / ota_0 / ota_1
├─ CMakeLists.txt
├─ main/
│  ├─ main.c                         ← app_main: nvs/tasks/event-loop bring-up, hand off to app_manager
│  ├─ board_pins.h                   ← the one place GPIO wiring lives
│  └─ idf_component.yml              ← THE APP MANIFEST (taskmaster_core + one line per app)
├─ components/                       ← auto-discovered by ESP-IDF (always in the build)
│  └─ taskmaster_core/               ← the OS component (see §6.1)
│     ├─ include/                    ←   public app API: app.h (device_app_t), app_manager.h, ui_hints.h
│     ├─ app_manager.c  launcher.c   ←   registry + lifecycle + Launcher
│     ├─ ui/   input/   net/         ←   display(sh1106 now; esp_lcd+lvgl Phase 3), GPIO input decoder, contract client
│     ├─ storage/  power/  provisioning/
│     └─ idf_component.yml           ←   core's own managed deps (lvgl + esp_lcd panel — Phase 3)
├─ apps/                             ← NOT auto-discovered → presence is controlled by the manifest
│  ├─ app_tasks/                     ← first-party app component (could equally be its own repo)
│  │  ├─ app_tasks.c  (TASKMASTER_REGISTER_APP)
│  │  └─ CMakeLists.txt (REQUIRES taskmaster_core, WHOLE_ARCHIVE)
│  ├─ app_settings/                  ← …
│  └─ app_pomodoro/                  ← …
├─ proxy/
│  └─ yapp_server/                   ← contract impl over Todoist (reuses ~/yapp-cli code)
└─ docs/                             ← wiring diagram, panel notes, device REST contract, dev setup
```
External/third-party apps live in **their own repos** and are pulled by a `git:` line in
`main/idf_component.yml` (or `EXTRA_COMPONENT_DIRS` for local dev) — nothing else changes.

> **Status (Phase 1 complete):** the refactor above is **done** — platform services and the app
> framework live in `components/taskmaster_core/`, and `apps/app_hello/` is the first self-registering
> app component, listed in `main/idf_component.yml`. `main/` is now a thin composition root.

### 11.2 Apps live in their own repos (core ⟂ userspace)
**Decided.** Anything app-specific lives **outside** the TaskMaster (core) repo, in its own git repo,
so core and app development version independently and a core change can't silently break an app
("don't break userspace"). The coupling is *only* the **stable app API** (`device_app_t` + the helper
headers, §6) and the **device REST contract** (§8.1).
- Each **source product** is one repo holding both halves: the **device app component** (pulled into a
  build via a `git:` line in `main/idf_component.yml`, §6.1) **and** its **host server**. Today:
  **`~/yappcloud`** (Todoist) and **`~/yapplocal`** (LAN stub). Their device app components are added at
  §8.5 step 11.
- The core repo keeps only **core apps** (Launcher, Setup/Wi-Fi → Settings) and **`apps/app_hello`** as
  the canonical minimal example + framework smoke/leak-test target.
- Core must therefore treat the app API as a **stable contract**: additive changes preferred; breaking
  changes are versioned and called out.

### 11.1 Coding standards
- **No magic numbers.** Every numeric literal — pixel coordinates/sizes, stack depths, timeouts,
  counts, buffer lengths — is a **named `#define` or enum with a meaningful name, declared in the
  relevant header** (e.g. geometry in `hint_bar.h`/`ui_frame.h`, task stacks in the owning header).
  Inline numbers in `.c` files are not allowed; the only exceptions are self-evident sentinels/indices
  (`0`, `1`, `-1`). This keeps intent self-documenting and makes layout/config single-sourced and
  tunable in one place. Applies to new code **and** to any code being touched.
- **One source of truth:** wiring lives only in `board_pins.h`; the config schema only in `nvs_config`;
  UI geometry only in the UI headers — never duplicated.

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
- Managed deps (LVGL + SH1106 `esp_lcd` panel — Phase 3) pinned via `idf_component.yml`. Input is
  hand-rolled GPIO (§4.3), no input-component dep.

---

## 13. Test & Validation Strategy
- **Per-driver bring-up first:** tiny standalone test apps for (a) OLED "hello", (b) encoder count to
  serial, (c) button events to serial — before any app logic. The SH1106 column offset and encoder
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
| **0 — Bring-up + spike** ✅ | ESP-IDF build, partition table, per-driver tests **+ SoftAP/HTTP spike** | App boots; OLED draws; encoder/buttons emit `EV_*`; **a phone loads a page served by the device over SoftAP** |
| **1 — Core OS** ✅ | Refactor bring-up into the **`taskmaster_core` component** + manifest-driven **app components** (§6.1); **UI task + stub `task_model_t`/mutex skeleton** (§5.2, network stubbed); app_manager lifecycle (`switch_to`) + Home wiring; **raw-rendered Launcher** (LVGL deferred, §4.4) | A demo app component self-registers via `idf_component.yml` and shows in the Launcher; commenting its manifest line removes it (not compiled); encoder navigates; app switch is clean (no races); Home returns from any app; leak-clean teardown cycle (§6A.4) passes |
| **2 — Provisioning portal** ✅ | **Setup/Wi-Fi core app** (non-removable, §6): paste-from-phone form → NVS, schema-driven (§9.2); STA connect + boot-mode branch on `provisioned`; **stand up the §6A.4 debug-build leak harness** (carried over from Phase 1) | Setup app auto-launches when unprovisioned and is reachable from the Launcher; join `TaskMaster-Setup`, paste full config in one form, persist to NVS, associate, survive reboot; **the launch→Home→relaunch leak-clean cycle (§6A.4) passes on a heap-poisoning debug build** |
| **3 — UI foundation + userspace tasks** ✅ | LVGL UI foundation (done, Part A); then **tasks in userspace** (§8): core gains generic `ui_list` + `async_job`, **task model/sync purged from core**; the **Yapp app pulls Todoist directly** (no proxy) + the **Local app** over its LAN contract; offline honors `WIFI_EN` | **Done + verified on hardware:** core is task-free; both source apps show priority-sorted, nested tasks via the generic list; complete + postpone + on-demand description (detail submenu) work; **Wi-Fi-off shows cached tasks + OFFLINE**, writes queue + replay on reconnect; apps hide until configured; §6A.4 leak gate passes on all 4 apps; the Home-mid-fetch cancel crash was found + fixed |
| **4 — Settings + power** ◀ next | Finish the **Settings hub** (§6) as a **schema-driven** menu (§8A.1): a generic knob-editor + confirm dialog (step 0), then device info (incl. **fw version/build date**) / restart / factory reset, startup target, **brightness**, timeout, deep-sleep; per-app `ACFG_KNOB` via the same editor; OTA-stub. Idle timeout + sleep scaffolding; **convert input from 1 ms poll → interrupt/GPIO-wake** (see note ↓). *(Sync interval is app-owned, not here; long-press stays out of core nav.)* | Settings menu navigates; one editor drives every knob (core + per-app); startup target, brightness, deep-sleep toggle, timeout persist and take effect; factory reset/restart work behind a confirm; screen blanks on idle; **system reaches light sleep when idle** |
| **4.5 — OTA path** | `esp_https_ota` + rollback | Device pulls a signed image from `fw_url`, boots the new slot, rolls back on failed confirm |
| **5 — Hardening + post-MVP** | Soak, error states, enclosure; then BLE provisioning / direct Todoist / Pomodoro | 24h soak clean; graceful Wi-Fi-loss + API-error UI; fits enclosure; post-MVP items as separate increments |
| **6 — App-ecosystem hardening** *(when a real third-party ecosystem materializes)* | Make the app platform safe to host untrusted apps: **per-app NVS budget enforcement** + `nvs` partition sizing (§9.3); namespace-**collision hardening** beyond docs; broader app sandboxing as needed | Apps can't exhaust NVS or crowd out core/provisioning; collisions detected, not just documented |
| | *(App-declared config in the form/Settings — formerly here — was pulled forward to Phase 3 (§9.4) as it's foundational to the core⟂userspace split.)* | |

Phases 0–4.5 = MVP. Phase 5 = hardening + post-MVP. Phase 6 = app-ecosystem hardening (deferred until
third-party apps are a real concern).

> **Current state (2026-06-30):** Phases 0–2 complete and **verified on hardware** (XIAO ESP32-C3) on
> ESP-IDF v6.0.1. Phase 2 closed end to end: paste-from-phone form → NVS → reboot → **associates with
> the real AP** (got IP); Setup is a re-enterable core app; re-provision-from-Launcher raises the AP
> beside a live STA without dropping it; the §6A.4 leak harness passes with heap poisoning. **Phase 3
> (sync + Task Manager) is next.**

> **⚠ Phase 4 — input must go interrupt-driven (don't forget).** The Phase-1 input path is a **1 ms
> poll** (§4.3), chosen because Phase 1 is mains-powered. That poll **prevents the system from ever
> reaching light sleep** — a task waking every 1 ms (atop the 1000 Hz tick) blocks ESP-IDF's tickless
> idle (`CONFIG_FREERTOS_USE_TICKLESS_IDLE`), forfeiting the ~hundreds-of-µA light-sleep tier before
> deep sleep is even considered. So Phase 4 **must** convert input to **GPIO-interrupt / wake-source
> driven**: encoder A/B + buttons as GPIO interrupts; deep-sleep wake on a button / encoder push (not
> every rotation edge). The Ben-Buxton decode table ports unchanged — only the *trigger* (poll → ISR)
> changes, so no decode logic is lost. Design this **together with the §8A sleep-state machine** (which
> states exist, what wakes from each), not in isolation. Optionally reevaluate the managed `button`
> component here for its built-in wake-source + long-press support.
>
> **⚠ Add hardware debounce (PCB) for the interrupt switch.** The 1 ms poll currently *masks* contact
> bounce; raw GPIO interrupts on a bouncy EC11 / tactile switch fire **bursts** of edges. Rather than
> reintroduce that complexity as ISR-side software debounce, add a simple **RC filter** (e.g. ~10 kΩ +
> ~100 nF, tuned) on encoder A/B + the buttons so edges are clean in hardware. Then raw interrupts are
> reliable *and* tickless idle unlocks — the cleanest path. Bake this into the battery-build PCB (§3 BOM).

---

## 15. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| SRAM exhaustion (Wi-Fi + TLS + JSON + LVGL) | Med | High | TLS is the big consumer (~40–50KB during a fetch); LVGL 1-bit full-frame buffer (~1KB) + Kconfig-trim; **stream/bound the Todoist parse** (§8.5 step 11); cap task count; tear down TLS after each fetch; XIAO-S3 escape hatch |
| Direct Todoist TLS too heavy on-device | Med | Med | `yapp-server` proxy is the default; direct is stretch only |
| Wrong SH1106 column offset → blank/garbled screen | Med | Low | Phase-0 driver bring-up; set the 2-px offset |
| Task races on active-app / shared model | Med | High | Strict ownership (§5.2); single mutex; app switch on the UI task only |
| App image overflows ~1.9MB OTA slot | Med | Med | App already ~1.1MB w/ LVGL. **Phase-5 BLE is the threat** — Bluedroid is ~500KB+; use **NimBLE** + Kconfig-trim, track image size in CI, drop unused features |
| Direct-Todoist payload blows the heap | Med | High | DOM (cJSON) peaks ~3–5× body; **bound via Todoist filter + hard-capped read buffer + jsmn/SAX field scanner** (§8.5 step 11); proxy fallback documented |
| Killing a blocked worker leaks the TLS session | Med | High | `async_job_cancel()` is **cooperative** — flag + `esp_http_client_close()` to abort the socket; worker frees its own ~40KB session; never `vTaskDelete` mid-`perform()` (§8.5 step 8) |
| Encoder missed steps at speed | Low | Low | Hand-rolled Ben-Buxton state machine (C3 has no PCNT, §4.3); 1 ms poll; tune steps-per-detent; HW RC filter at the battery build (§14) |
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
| 7 | Provisioning | **Paste-from-phone SoftAP portal** as the **Wi-Fi-setup item in the Settings hub** (non-removable; all config in one form); carousel dropped; BLE = a post-MVP (Phase 5) second transport |
| 8 | OTA | **Yes — ESP-IDF native dual-slot** (`esp_https_ota`) + rollback, image from `fw_url` |
| 9 | Settings/power | **Settings app** controls startup target, deep-sleep toggle, inactivity timeout — battery-ready scaffolding (§8A) |
| 10 | IDF version | **v6.0.1** (installed via `eim`), pinned for reproducibility |
| 11 | App model | **Self-registering app components + editable manifest** (§6.1): apps are ESP-IDF components (own dir/repo), added/removed by a line in `main/idf_component.yml`, no core edits, disabled = not compiled. Full registry-publish/CI polish deferred |

No open decisions remain — the plan is build-ready.

---

## 17. Acceptance Criteria (MVP "done")

- [ ] Fresh device raises `TaskMaster-Setup`; user pastes the full config (Wi-Fi, source URLs, tokens)
      from a phone in one form; device associates. **No value is typed on the knob.**
- [ ] All config persists across power cycle; device auto-connects on next boot.
- [ ] Launcher lists registered apps; encoder navigates; Select/click switches app with no crash/leak.
- [ ] Task Manager shows Todoist tasks (via `yapp-server`) within one sync cycle, priority-sorted with
      nesting, mirroring `todomark`; the "Local" app works against a stub source; Wi-Fi + sync state
      shown inline (no OS status bar, §6).
- [ ] Select completes the highlighted task; Postpone (submenu) reschedules; both reflect on next sync.
- [ ] **Home returns to the Launcher from anywhere**, even mid-action, never swallowed by an app.
- [x] **Leak-clean teardown:** launch→Home→relaunch cycles restore the free heap to baseline per app,
      with comprehensive heap poisoning catching no corruption (§6A.4 / §7A.7). *(Hello + Setup pass on
      hardware; mid-fetch Home + 100× scaling fold into Phase 3 once a real fetch exists.)*
- [ ] The contextual **right hint bar** (Home / encoder-split / Select) shows the current control
      labels/glyphs and updates when an app changes mode (e.g. Task Manager list → detail submenu);
      apps can turn it off for full-width screens.
- [ ] Settings changes (startup target, timeout, deep-sleep toggle) persist and take effect.
- [ ] OTA: device pulls a signed image from `fw_url`, boots the new slot, and rolls back on failed confirm.
- [ ] Input→visible-response latency feels instant (<100ms) under active background sync.
- [ ] 24h soak: no heap/stack exhaustion, no stuck state after a Wi-Fi drop/reconnect.
```
