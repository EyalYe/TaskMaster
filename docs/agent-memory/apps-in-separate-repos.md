---
name: apps-in-separate-repos
description: TaskMaster apps live in their own git repos (not the core repo); core⟂userspace via stable API
metadata: 
  node_type: memory
  type: project
  originSessionId: 1ffe722c-73fa-4024-a1d9-4de2d4a260f2
---

TaskMaster keeps **app-specific code out of the core repo**, in separate git repos, so core and apps version independently and a core change can't break an app ("don't break userspace"). The only coupling is the **stable app API** (`device_app_t` + helper headers) and the **device REST contract** (PLAN §8.1).

**Each task source = one self-contained repo holding BOTH halves** — the device app component (pulled into a firmware build via a `git:` line in `main/idf_component.yml`) AND its host server:
- **`~/yappcloud`** — Todoist source; host server reuses `~/yapp-cli` (`get_api` + `todolst.py`). Created Phase 3 step 6.
- **`~/yapplocal`** — platform-agnostic LAN source; stdlib `http.server` over an in-memory store. Created Phase 3 step 6.

Both host servers are Python stdlib `http.server` implementing GET /tasks (etag, priority-sorted, parent_id nesting, title-truncated), POST complete/postpone, GET /health. The device **app components** are added to these same repos at step 11.

**Consequences:**
- The **core TaskMaster repo** keeps only core apps (Launcher, Setup/Wi-Fi→Settings) + `apps/app_hello` (the canonical example + leak/smoke-test target).
- The Task Manager is **NOT** one component registered twice. Core provides the reusable Task Manager capability (source_client + render helpers); each source repo ships a *thin* app component binding it to a `task_source_t {name, url_key, token_key}`.
- Core must treat the app API as a **stable contract**: additive changes preferred; breaking changes versioned + called out.

Documented in PLAN §11.2. Related: [[taskmaster-c3-project]], [[no-magic-numbers]]
