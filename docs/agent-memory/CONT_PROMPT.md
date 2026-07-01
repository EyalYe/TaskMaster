# Continuation prompt — paste this to the next agent

> Copy everything below the line as your first message in a fresh Claude Code session
> opened in the TaskMaster repo. It bootstraps the agent with full context.

---

You are continuing work on **TaskMaster-C3**, an ESP-IDF v6.0.1 firmware for a XIAO
ESP32-C3 desktop task appliance (SH1106 OLED + EC11 encoder + Select/Home buttons). This
is a resumed session on a new machine.

**Before doing anything, read these in order and load context:**
1. `SESSION1.md` (repo root) — the full session-1 handoff: what the project is, the three
   repos + exact commit state, Phase 0–4 status, architecture, key decisions, the
   build/flash/OTA workflow, gotchas, and how to resume.
2. `PLAN.md` — the authoritative spec. Phase 3 build order is §8.5; Phase 4 is §8A.1; the
   roadmap table is near the end.
3. `docs/agent-memory/` — my persistent memory. If it hasn't been restored into this
   machine's `~/.claude/projects/.../memory/` yet, read the `.md` files here directly and
   follow `docs/agent-memory/README.md` to restore them.

**Current state:** Phases 0–4 are complete and verified on hardware (Settings hub with a
schema-driven editor, device info, startup, brightness, timeout+screen-blank, deep/light
sleep, delete-per-app-data, restart, factory reset, per-app knobs, and OTA). All three
repos are pushed. **Next up is Phase 5 — core UX completion** (PLAN §6C / §6C.1): the
Launcher status bar (connectivity glyph + NTP time + weather) + the glyph hint bar (the
`icons/` assets) + a cohesion pass. BLE provisioning is **parked** (see the roadmap).

**Hard rules (do not violate):**
- **No magic numbers** — every numeric literal is a named `#define`/enum in a header
  (only `0`/`1`/`-1` sentinels exempt). Applies to new and touched code.
- **Plan before code** — for a new subsystem, write the architecture into `PLAN.md`
  first, let it settle, then implement.
- **Core ⟂ userspace** — core never names a specific app; task apps live in their own
  repos (`TM-YappLocal`, `TM-YappCloud`), pulled via `main/idf_component.yml`. Only the
  stable app API + REST contract couple them.
- **Async cancel is cooperative** — the HTTP client handle is worker-local; cancel is a
  flag the worker polls. Never tear down a non-thread-safe handle from the UI thread
  (that crashed; see `components/taskmaster_core/app/async_job.h`).
- **Commits:** only when the user asks; branch off `main` for PRs. Core commit messages
  end with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`; app-repo commits use
  `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.

**Build/flash workflow (see SESSION1.md §7 for the full gotcha list):**
```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.1.sh"
rm -f managed_components/lvgl__lvgl/CMakePresets.json   # LVGL preset wedge
idf.py build && idf.py -p "$(ls /dev/cu.usbmodem* | head -1)" flash
```
- After changing an app repo: `rm -rf managed_components/tm_yapp managed_components/tm_local dependencies.lock` before building.
- Serial capture needs the IDF venv python (`.../python/v6.0.1/venv/bin/python`) for pyserial.
- If **Deep sleep** is on, flashing needs manual download mode (hold BOOT, tap RESET).
- Deleting `sdkconfig` resets target to esp32 → re-run `idf.py set-target esp32c3`.

**How to proceed:** confirm the build is green on this machine, then ask the user whether
to start Phase 5 (core UX completion — status bar + glyphs) or address something else. Work step-by-step and
verify on hardware (the user tests interactively and expects each step flashed + checked).
Don't restate this prompt back — just get oriented and continue.
