# Skill: add an app (userspace — never touches core)

An app is a self-contained ESP-IDF component that fills in a `device_app_t` and self-registers. The OS
calls your hooks on one UI task (no threads/mutexes). Full contract: `docs/APP_API.md`.

## The only file you edit to add/remove apps: `apps.yaml`

```yaml
core:                                   # the sealed OS — pin a version, never fork/edit it
  git: https://github.com/EyalYe/TaskMaster.git
  path: components/taskmaster_core
  version: main                         # or a release tag
apps:
  - name: app_skeleton                  # in-tree app (this repo's apps/)
    path: apps/app_skeleton
  - name: tm_yapp                        # an app from its own repo
    git: https://github.com/EyalYe/TM-YappCloud.git
    path: app                            # the app's subdir inside that repo
    version: main                        # branch, tag, or commit (aliases: branch/tag/ref)
```

A hook in the root `CMakeLists.txt` compiles this into `main/idf_component.yml` (generated,
git-ignored). You never touch that file, `main/`, or the core.

## Writing the app

Copy `apps/app_skeleton/` to a new folder, rename, and add an `apps.yaml` entry. Minimum:

```c
#include "app.h"        // device_app_t + TASKMASTER_REGISTER_APP
#include "ui_frame.h"   // ui_text_row / ui_frame_content / control_hints_t
#include "input.h"      // EV_* events

static void my_render(void) {
    lv_obj_clean(ui_frame_content());
    ui_text_row(0, "Hello");
    static const control_hints_t H = { .click = "OK", .select = "OK" };
    ui_frame_set_hints(&H);             // right-bar hints; NULL = full width
}
static const device_app_t app = { .name="My App", .render=my_render };  // + init/on_event/exit
TASKMASTER_REGISTER_APP(app);
```

Its `CMakeLists.txt` needs `REQUIRES taskmaster_core` and **`WHOLE_ARCHIVE`** (so the self-registration
constructor isn't dropped by the linker).

## What the OS gives you (public headers only)

- **`ui_frame.h`** — `ui_frame_content()`, `ui_text_row/ui_text/ui_image`, the 3-box hint bar
  (`control_hints_t`: `.click`/`.select`; rotation always scrolls, not hinted). Tokens
  `DON`/`OK`→check, `OPN`/`SEL`→select, `MNU`→menu, `RST`→reset, `BAK`→back render as glyphs; other
  ≤3-char text renders as-is.
- **`ui_list.h`** — a generic scrollable/selectable list.
- **`app_store.h`** — your own private, persistent NVS namespace (survives reboot/app-switch).
- **`app_config.h`** — declare config fields (`ACFG_PASTE` = typed in the setup form; `ACFG_KNOB` =
  adjustable in Settings) collected into your namespace, with no core edits.
- **`net_status.h`** — `net_is_online()` / `net_status_get()`. The UI re-renders you on connectivity
  change, so just read it in `render()`.
- **`async_job.h`** — run blocking I/O off the UI task; the result callback runs back on the UI task.
  Cancel is **cooperative** (poll `async_job_cancelled()`); never tear down a client handle cross-thread.

## Rules

- **Teardown must be total + idempotent** in `exit()` (free widgets/heap, close stores, cancel jobs) —
  the OS reuses the screen for the next app.
- **`available()` returning false hides the app** from the Launcher (e.g. until it's configured).
- **Home is OS-reserved** — you never see it; it returns to the Launcher.
- If you need something the headers don't expose, it's **off-limits** — don't reach into core.

## GPIO (your risk)

Pads **D6–D9 + the shared I²C** are free for app hardware. There is **no arbitration service** — a pin
conflict, or grabbing a core-owned pin (see `board_pins.h`), is on you. D8/D9 are strapping pins.
