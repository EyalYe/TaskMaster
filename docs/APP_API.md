# Writing an app for TaskMaster-C3

An app is a self-contained ESP-IDF component that implements the `device_app_t` interface and
registers itself. The core (`taskmaster_core`) never references your app by name — you add or remove
it by editing one line in [`main/idf_component.yml`](../main/idf_component.yml). This guide is the
practical contract; see [`PLAN.md`](../PLAN.md) §6 / §6A for the full rationale.

## 1. The interface

```c
#include "app.h"

typedef struct device_app {
    const char *name;                 // shown in the Launcher
    void (*init)(void);               // allocate / reset state
    void (*on_event)(uint8_t ev);     // input event (see §3) — Home is never delivered
    void (*render)(void);             // draw the current state
    void (*exit)(void);               // teardown / free everything
} device_app_t;
```

Minimal app:

```c
#include "app.h"
#include "sh1106.h"

static void my_init(void)           { /* reset state */ }
static void my_on_event(uint8_t ev) { /* update state */ }
static void my_render(void)         { sh1106_clear(); sh1106_text_line(0, "HI"); sh1106_flush(); }
static void my_exit(void)           { /* free everything init() could have allocated */ }

static const device_app_t my_app = {
    .name = "MyApp", .init = my_init, .on_event = my_on_event,
    .render = my_render, .exit = my_exit,
};
TASKMASTER_REGISTER_APP(my_app);
```

Add it to the build — one manifest line in `main/idf_component.yml`:

```yaml
dependencies:
  my_app:
    path: ../apps/my_app        # in-tree, or use a `git:` URL to your own repo
```

Your component's `CMakeLists.txt` needs `REQUIRES taskmaster_core` and `WHOLE_ARCHIVE` (so the
self-registration constructor survives `--gc-sections`).

## 2. Lifecycle & the rules

`init` → (`on_event` / `render` loop) → `exit`, **all called from the one UI task** — so you never
need a mutex for your own state. Key rules:

- **`render()` must not block** on network or long I/O — just draw. Heavy work belongs to a core
  service, not your app.
- **Do not spawn tasks** (`xTaskCreate`). Apps are single-task by contract (PLAN §6A.2); this is what
  keeps teardown leak- and crash-safe.
- **`exit()` must be total and idempotent** — free everything `init()` *could* have allocated,
  regardless of how far it got, because **Home can fire at any moment** (see §3).
- **Render is on-demand**, not a frame loop: the platform calls `render()` after each input event and
  whenever platform status you display changes (see §4). Draw from your current state each time.

## 3. Input events (`on_event`)

```c
// input.h
EV_ENCODER_CW, EV_ENCODER_CCW,   // knob rotation
EV_ENCODER_CLICK,                // knob push
EV_SELECT,                       // Select button
// EV_HOME is OS-reserved — it returns to the Launcher and is NEVER delivered to your app.
```

After `on_event` returns, the platform calls your `render()`. The contextual **hint strip** lets you
label the three app-usable controls — call `ui_set_hints()` from `init()` and on mode changes
(PLAN §6).

## 4. Reading platform status — connectivity

Connectivity is platform state; read it from one place, no Wi-Fi handling in your app:

```c
#include "net_status.h"

void my_render(void) {
    net_status_t ns;
    net_status_get(&ns);                 // mutex-guarded snapshot
    if (ns.online) { /* fetch-backed UI */ }
    sh1106_text_line(7, net_state_str(ns.state));  // "OFF"/"---"/"..."/"OK"/"SETUP"
}
```

`ns.state` is one of `NET_WIFI_OFF`, `NET_DISCONNECTED`, `NET_CONNECTING`, `NET_CONNECTED`,
`NET_PORTAL`; `ns.rssi` is dBm when online; `net_is_online()` is the shortcut.

**You don't subscribe to anything.** When connectivity changes, the platform re-runs your `render()`,
so the value you read there is always current. (Internally a `EV_SYS_NET_CHANGED` system event drives
the redraw; system events are handled by the UI and never reach `on_event`.) If you need to *act* on a
transition rather than just redraw, cache the previous `state` in your own struct and compare.

Never call `esp_wifi_*` yourself — the radio is owned by core (the network task, the Setup app, and
the Settings `WIFI_EN` toggle). The same `*_get()` + auto-re-render pattern will expose future status
(battery, sync state) as the platform grows.

## 5. Persisting your own data (`app_store.h`)

Your app can create and persist its **own** variables — no core edits, no shared schema. Each app gets
a **private NVS namespace** keyed by an id you choose:

```c
#include "app_store.h"

static app_store_t store;

static void my_init(void) {
    app_store_open(&store, "myapp");              // unique id, 1..15 chars
    uint32_t runs;
    app_store_get_u32(&store, "runs", &runs, 0);  // default 0 on first run
    app_store_set_u32(&store, "runs", runs + 1);  // persisted immediately
}

static void my_exit(void) {
    app_store_close(&store);                       // release the handle
}
```

Available types: `app_store_get/set_str`, `_u32`, and `_blob` (for a small struct). `get_*` take a
default, so you never special-case "first run". Notes:

- **Pick a stable, unique id** (often your app name, or a repo slug). It's your private island — keys
  can't collide with other apps or with device config.
- **Ids can be any length.** NVS caps a namespace at **15 chars**: ids of 1..15 chars are used
  verbatim; longer ids are **hashed** (64-bit FNV-1a → base36) to a 15-char namespace automatically.
  **Keys** are always limited to **15 chars** by NVS.
- **Collision — be aware (it's an off-chance, not a real risk):** two *distinct* ids mapping to the
  same namespace is astronomically unlikely (64-bit hash), but not impossible — a short literal id
  could coincide with another id's hash, or two long ids could hash alike. If that ever happened, the
  two apps would share a namespace and could read/overwrite each other's keys. **Choosing a
  distinctive id makes this a non-issue.**
- **`tmcfg` is reserved** for core device config (`nvs_config`); `app_store_open()` rejects it.
- **Don't touch `nvs_config.h`** — that's core-owned device config (Wi-Fi creds, tokens, settings)
  that drives the setup form. `app_store` is your app-private tier (PLAN §9.3).
- `app_store_erase_all(&store)` clears just your namespace (a per-app reset).
- **Shared, finite store — be frugal.** All apps *and* device config share **one ~24 KB NVS pool**
  with **no per-app reservation**: an app that stores nothing costs nothing, and there's no quota to
  claim. Keep keys small and store **config/state**, not bulk data — logs, caches, and large blobs
  belong in RAM or elsewhere. Because the pool is shared and unguarded today, a greedy app can crowd
  out others (and even provisioning); per-app budget enforcement is a deferred item (PLAN §14 Phase 6).

Writes commit immediately, so prefer writing on real changes / in `exit()` rather than every frame
(NVS is flash). A user-facing setting that should appear in the device **Settings** UI is a separate,
future facility (PLAN §9.3) — `app_store` is for app-internal state, the common case.
