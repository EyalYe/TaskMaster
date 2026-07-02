# Writing an app for TaskMaster-C3

An app is a self-contained ESP-IDF component that implements the `device_app_t` interface and
registers itself. The core (`taskmaster_core`) never references your app by name — you add or remove
it with one entry in [`apps.yaml`](../apps.yaml) (compiled into the build manifest for you; you never
edit core). This guide is the practical contract; see [`PLAN.md`](../PLAN.md) §6 / §6A for the full
rationale.

> **You never modify the OS.** `taskmaster_core` is a fixed, immutable contract — everything your app
> needs is exposed through the public headers described here (`app.h`, `ui_frame.h`, `ui_list.h`,
> `app_store.h`, `net_status.h`, `async_job.h`, `app_config.h`). If a capability isn't exposed, treat it
> as off-limits rather than reaching into core: your app lives entirely in its own component/repo.

## 1. The interface

```c
#include "app.h"

typedef struct device_app {
    const char *name;                 // shown in the Launcher
    void (*init)(void);               // allocate / reset state
    void (*on_event)(uint8_t ev);     // input event (see §3) — Home is never delivered
    void (*render)(void);             // draw the current state
    void (*exit)(void);               // teardown / free everything
    bool (*available)(void);          // optional: false = hide from the Launcher (§7); NULL = always shown
} device_app_t;
```

`available()` is **optional** (leave it `NULL` to always show). Return `false` to hide your app from
the Launcher until it's usable — e.g. a task source with no server URL configured. It's called while
your app is **not** active, so read config directly (`app_store`, §6) — don't assume `init()` has run.

Minimal app:

```c
#include "app.h"
#include "ui_frame.h"

static void my_init(void)           { /* reset state */ }
static void my_on_event(uint8_t ev) { /* update state */ }
static void my_render(void) {
    lv_obj_clean(ui_frame_content());   // clear the content area
    ui_text_row(0, "Hi there");         // one line of text (row 0)
    ui_frame_set_hints(NULL);           // no hint bar → full width (see §4)
}
static void my_exit(void)           { /* free everything init() could have allocated */ }

static const device_app_t my_app = {
    .name = "MyApp", .init = my_init, .on_event = my_on_event,
    .render = my_render, .exit = my_exit,   // .available optional
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
  whenever platform status you display changes (see §5). Draw from your current state each time.

## 3. Input events (`on_event`)

```c
// input.h
EV_ENCODER_CW, EV_ENCODER_CCW,   // knob rotation
EV_ENCODER_CLICK,                // knob push
EV_SELECT,                       // Select button
// EV_HOME is OS-reserved — it returns to the Launcher and is NEVER delivered to your app.
```

After `on_event` returns, the platform calls your `render()`. To label what the controls do, use the
**control hint bar** — see §4.

## 4. Screen & the control hint bar

The display is a **128×64 monochrome** OLED, driven by **LVGL**. Your app draws in `render()` by
building LVGL widgets into the OS-owned **content area** (`ui_frame.h`) — you don't touch the panel or
`sh1106` directly (the OS pumps LVGL onto the framebuffer for you).

### Drawing

```c
#include "ui_frame.h"

lv_obj_clean(ui_frame_content());     // clear the content area (start of every render)
ui_text_row(row, "Hello");            // one text line at row 0..UI_ROWS-1
ui_text(x, y, "Hello");               // text at a pixel (x, y) in the content area
ui_text_row_scroll(row, txt);         // full-width line that auto-scrolls if too long (titles/URLs)
ui_text_wrap(row, txt);               // word-wrapped multi-line body (e.g. a description)
```

- Rows are laid out for you: `UI_ROWS` lines fit the panel, `UI_ROW_H` px each (constants in
  `ui_frame.h`). Use `ui_text_row()` and forget pixel math.
- The content font is proportional **DejaVu Sans 11** — full upper/lower case, digits, punctuation.
- `ui_frame_content()` returns the LVGL container if you need raw `lv_*` widgets; the OS clears it
  between app switches (`ui_frame_reset_content()`), so `exit()` needn't free widgets.

For a scrollable/selectable list (menus, task lists), use the generic **`ui_list`** (`ui_list.h`):
`ui_list_init/set_count/move/sel/draw` — it owns the scroll window + selection cursor; you supply each
row's text via a callback. (The Launcher, Settings, and the task apps are all built on it.)

### The hint bar (opt-in)

The OS draws a **vertical control hint bar** down the right edge so the user always sees what the
knob/Select do *right now* — you just declare the labels and call one function:

- **Show it** (`ui_frame_set_hints(&HINTS)`) → the OS draws the right column; content narrows to the
  left of it. Draw your rows first, then set the hints (setting hints sizes the content width).
- **Full screen** (`ui_frame_set_hints(NULL)`) → you own the whole 128 px width.

**Three equal boxes, one per button**, top → bottom: **Home** (OS-fixed, always "back to Launcher"),
**Encoder-push** (your `.click`), **Select** (your `.select`). Encoder *rotation* is **not** shown —
it always scrolls, so it needs no hint (`.rotate` is ignored, kept only for source compatibility).
Declare labels (≤3 chars) with `control_hints_t` (`hint_bar.h`):

```c
#include "ui_frame.h"   // pulls in hint_bar.h (control_hints_t)

static const control_hints_t HINTS = {
    .click  = "MNU",   // encoder push  → middle box (NULL = empty)
    .select = "DON",   // Select button → bottom box (NULL = empty)
};

static void my_render(void) {
    lv_obj_clean(ui_frame_content());
    ui_text_row(0, "My content");
    ui_frame_set_hints(&HINTS);   // or NULL for full width
}
```

**Home still works** as the physical escape hatch regardless of the bar. Re-publish hints whenever your
app changes mode (e.g. a list vs. a detail submenu) so the bar always matches the current controls.

**If `.click` and `.select` are the same string**, the OS drops the redundant middle box and shows the
action once (on Select) — so a screen where the encoder-push and Select do the same thing reads as just
**Home + Select**.

#### Glyphs vs. text

The OS renders a **1-bit glyph** for a set of conventional tokens, and falls back to your **≤3-char
text** for anything else (so any label is safe — it just renders as text if there's no glyph):

| Token | Glyph | Meaning |
|---|---|---|
| `DON`, `OK` | ✓ check | complete / confirm |
| `OPN`, `SEL` | select | open / select |
| `MNU` | ☰ menu | menu / detail |
| `RST` | ↺ reset | reset |
| `BAK` | ← back | back |

Prefer these tokens when they fit your action, so the whole device speaks one visual language; use plain
text (e.g. `"SYN"`, `"+/-"`) otherwise. The glyph set is **owned by the OS** — you only ever supply a
string, and the OS decides how to draw it.

## 5. Reading platform status — connectivity

Connectivity is platform state; read it from one place, no Wi-Fi handling in your app:

```c
#include "net_status.h"
#include "ui_frame.h"

void my_render(void) {
    net_status_t ns;
    net_status_get(&ns);                 // mutex-guarded snapshot
    if (ns.online) { /* fetch-backed UI */ }
    ui_text_row(UI_ROWS - 1, net_state_str(ns.state));  // "OFF"/"---"/"..."/"OK"/"SETUP"
}
```

`ns.state` is one of `NET_WIFI_OFF`, `NET_DISCONNECTED`, `NET_CONNECTING`, `NET_CONNECTED`,
`NET_PORTAL`; `ns.rssi` is dBm when online; `net_is_online()` is the shortcut.

**You don't subscribe to anything.** When connectivity changes, the platform re-runs your `render()`,
so the value you read there is always current. (Internally a `EV_SYS_NET_CHANGED` system event drives
the redraw; system events are handled by the UI and never reach `on_event`.) If you need to *act* on a
transition rather than just redraw, cache the previous `state` in your own struct and compare.

Never call `esp_wifi_*` yourself — the radio is owned by core (`wifi_mgr`, the Settings app's Wi-Fi
setup portal, and its `WIFI_EN` toggle). The same `*_get()` + auto-re-render pattern will expose future status
(battery, sync state) as the platform grows.

## 6. Persisting your own data (`app_store.h`)

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
(NVS is flash). `app_store` is for app-**internal** state. To get user-supplied config (a server URL,
a token, a preference) **into** your namespace, declare it — see §7.

## 7. User config — let the OS fill it for you (`app_config.h`)

Your app shouldn't hardcode a server URL or token, and **core must not know about them** (that's what
keeps core and apps independent). Instead you **declare** the config you need, and the OS adds it to
the provisioning form / Settings automatically — assembling Wi-Fi (core) + every installed app's
fields. The values land in **your `app_store` namespace**, so you read them with the `app_store_get_*`
from §6.

```c
#include "app_config.h"

static const app_cfg_field_t MYAPP_CFG[] = {
    { .key="url",   .label="Server URL", .type=ACFG_STR, .input=ACFG_PASTE, .max_len=96  },
    { .key="token", .label="API token",  .type=ACFG_STR, .input=ACFG_PASTE, .secret=true, .max_len=128 },
    { .key="every", .label="Sync min",   .type=ACFG_U16, .input=ACFG_KNOB,  .min=1, .max=60 },
};
TASKMASTER_REGISTER_APP_CONFIG("myapp", "MyApp", MYAPP_CFG);   // ns, display name, fields
```

Then read it where you need it (the `ns` is the same one you pass to `app_store_open`):

```c
char url[97];
app_store_get_str(&store, "url", url, sizeof(url), "");   // "" until the user sets it
if (!url[0]) { /* not configured yet — show a hint, stay out of the way */ }
```

**Two input methods** (the device never lets you type strings on the knob):
- **`ACFG_PASTE`** — strings/secrets (URLs, tokens). Appear in the **paste-from-phone form**; the user
  pastes them. Re-editable only by re-opening that form.
- **`ACFG_KNOB`** — scalars (`ACFG_U8/U16/BOOL`) with `min`/`max`. **Knob-editable in Settings.**

Notes:
- `secret: true` masks the field (password input). Use it for tokens.
- The `ns`/`key` strings must not contain `.` (the form encodes fields as `cfg.<ns>.<key>`).
- A field the user hasn't set reads as your default — handle "not configured yet" gracefully (e.g. a
  source app with no URL simply stays hidden in the Launcher).

## 8. Background work — fetch off the UI task (`async_job.h`)

Your `render()`/`on_event()` run on the **UI task**. A multi-second network fetch there would freeze
the screen and input. You also **must not spawn your own task** (it would break Home teardown). So do
background I/O through the core **`async_job`** service: your `work` runs on a core worker, your `done`
runs back on the UI task with the result.

```c
#include "async_job.h"

typedef struct {            // your job context — core COPIES this at submit
    char  url[96];          //  inputs (filled before submit)
    int   count;            //  outputs (filled by work, read by done)
} fetch_ctx_t;

static async_job_t *s_job;  // track the in-flight job so exit() can cancel it

static bool fetch_work(async_job_t *job, void *ctx) {     // WORKER task
    fetch_ctx_t *c = ctx;
    // The client handle is a LOCAL — worker-owned, never a shared/static the UI task
    // could touch (esp_http_client is NOT thread-safe).
    esp_http_client_handle_t client =
        esp_http_client_init(&(esp_http_client_config_t){ .url = c->url });
    char *body = malloc(BODY_MAX);
    if (body && esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int total = 0, r;
        // Poll the cancel flag each iteration so Home mid-fetch bails cooperatively.
        while (total < BODY_MAX - 1 && !async_job_cancelled(job) &&
               (r = esp_http_client_read(client, body + total, BODY_MAX - 1 - total)) > 0) {
            total += r;
        }
        esp_http_client_close(client);
        if (!async_job_cancelled(job)) { /* parse body into c->outputs */ }
    }
    free(body);
    esp_http_client_cleanup(client);   // the worker frees its own client
    return !async_job_cancelled(job);
}

static void fetch_done(void *ctx, bool ok) {              // UI task (skipped if cancelled)
    fetch_ctx_t *c = ctx;
    if (ok) { /* copy c->outputs into your app model, re-render */ }
    s_job = NULL;
}

static void my_sync(void) {
    fetch_ctx_t c = {0};
    snprintf(c.url, sizeof c.url, "%s", my_url);
    s_job = async_job_submit(fetch_work, fetch_done, &c, sizeof c);   // c is copied
}

static void my_exit(void) {
    if (s_job) { async_job_cancel(s_job); s_job = NULL; }   // cooperative — see rules
}
```

Rules that keep teardown safe (§6A):
- **`work` must only touch `ctx`** (the core-owned copy) — never your app's live state. That's why
  core copies `ctx`: even if the user hits Home mid-fetch and your app is torn down, the worker only
  touches the copy. Put both inputs and outputs in the `ctx` struct; `done` copies outputs into your
  model.
- **Cancel is cooperative (flag-only).** Keep the client handle **worker-local**, poll
  `async_job_cancelled(job)` in your read loop, and let the worker free its own client. Call
  `async_job_cancel(job)` in your `exit()` — it sets the flag and returns immediately (it does **not**
  join the worker). A cancelled job's `done` is skipped, so a worker still finishing after you exit
  can't write into freed state; it unwinds within its I/O timeout.
- **⚠️ Never tear down a non-thread-safe handle from another task.** Do **not** close/free the
  `esp_http_client` from the UI thread (e.g. via an `async_job_on_cancel` abort hook) while the worker
  is inside `read()`/`open()` — that's a data race that **crashes** (it did; that's why this pattern
  changed). The `async_job_on_cancel` hook exists only for genuinely thread-safe wakeups.
- One job at a time (single worker): `async_job_submit` returns `NULL` if one is already running —
  skip this sync and try again later.

## 9. Driving your own hardware (GPIO — convention, not yet enforced)

There is **no sandbox**: your app can `#include "driver/gpio.h"` (and ADC/SPI/I²C) and call it directly
on any pin. That means you *can* drive your own peripherals — but nothing stops you from breaking the
device, so follow these rules. **They are convention today** (an enforced core `app_gpio` claim/release
service arrives in Phase 6, PLAN §14); until then it's the honor system.

**Pins that are yours** on the XIAO ESP32-C3 (full map in `board_pins.h`):

| Pad | GPIO | Notes |
|---|---|---|
| **D6** | 21 | free |
| **D7** | 20 | free |
| **D8** | 8 | free but **strapping** (boot mode) — don't hold it low at reset |
| **D9** | 9 | free but **strapping** — same caveat |

Plus the **shared I²C bus** (SDA=GPIO6 / SCL=GPIO7) for extra I²C devices at *other* addresses.

**Rules:**
- **Never touch a core-owned pin** — OLED `GPIO6/7`, encoder `GPIO2/3/4`, Select `GPIO5`, Home `GPIO10`.
  Driving those breaks the UI. Don't hardcode them; they live in core's `board_pins.h`.
- **Claim in `init()`, fully release in `exit()`.** Configure your pins in `init()` and reset them
  (`gpio_reset_pin`, remove any ISR handler) in `exit()`. **Home can fire at any moment** — a
  left-configured pin or a dangling ISR breaks the clean-teardown guarantee (§2 / §6A).
- **Don't spawn a task** to poll hardware (§2). Read it in `on_event`/`render`, do a one-shot in an
  `async_job`, or register an ESP-IDF timer/ISR in `init()` and remove it in `exit()`.
- **No arbitration yet** — two apps both grabbing D6 will conflict. The Phase-6 `app_gpio` service will
  publish the free pins and enforce claims (rejecting core pins + double-claims); design your app to
  claim/release so it slots in cleanly when that lands.
