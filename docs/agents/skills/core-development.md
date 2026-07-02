# Skill: working inside the sealed core

For changes to `taskmaster_core` itself (bug fixes, new OS capabilities). App authors never do this.

## Mental model

- The core is a **component** (`components/taskmaster_core/`) that other projects pull as a **git
  dependency**. Keep it self-contained: it must never reference `main/`, `apps/`, or repo-root files
  (the generated `hint_glyphs.c` is committed *inside* the component; `gen_glyphs.py` is dev-only).
- **Bootstrap** is `taskmaster_run()` in `app/taskmaster.c`. The project's `main/main.c` is a 3-line
  stub. Add boot-time init there.
- **Public vs private headers:** anything an app may use goes in a module folder on the include path and
  is documented in `docs/APP_API.md`. Don't expose internals casually — the API is a contract.

## Common tasks

- **Add a core service** (like `wx`): create `net/foo.c` + `foo.h`, add the `.c` to the `SRCS` list in
  `components/taskmaster_core/CMakeLists.txt`, add any new IDF components to `REQUIRES`/`PRIV_REQUIRES`,
  and start it from `taskmaster_run()`. If it produces data the UI shows, call `net_status_notify()` to
  poke a re-render.
- **Add a config field:** add a row to the schema table in `storage/nvs_config.c` (`CFG_WP_PROVISION`
  shows it in the setup/LAN form; `CFG_WP_SETTINGS` is a device setting).
- **Add a Settings item:** add to the enum + table in `settings/app_settings.c` (kinds TOGGLE / ENUM /
  RANGE / ACTION); values go through `get`/`set`.
- **A managed dependency for core** (e.g. cJSON): declare it in
  `components/taskmaster_core/idf_component.yml` and add its component name to `PRIV_REQUIRES`.

## Verifying a core change

You normally build from the **template**, but a core change must be confirmed to compile **before you
push** (else the template pulls broken core). Acceptable pattern:

1. Make the change in `~/TaskMaster`.
2. **One** verification build in the core repo (`idf.py build`) — say so; it's the exception to
   "don't build from core".
3. Commit + push core (`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`).
4. In the template: `rm -rf managed_components/taskmaster_core dependencies.lock && idf.py reconfigure`
   to pull the new core, then build + flash to verify on hardware.

## Non-negotiables

- **No magic numbers** (named `#define`/enum in a header).
- **Never break the app API** without a reason; if you must, that's what app-API versioning is for
  (Phase 6). Core and apps version independently.
- **async_job cancel is cooperative** — never tear down a non-thread-safe handle from another thread.
- The UI task is the **only** thread that touches LVGL.
