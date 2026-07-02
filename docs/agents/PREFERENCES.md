# Owner preferences

These are the owner's standing preferences for this project. They override generic defaults.
Follow them without being asked.

## Engineering standards

- **No magic numbers — this is a hard rule.** Every numeric literal (pixel geometry, sizes, stack
  depths, timeouts, counts, buffer lengths) must be a **named `#define`/enum in a header**, never an
  inline number in a `.c`. Only self-evident sentinels (`0`, `1`, `-1`) are exempt. Treat it as a
  professional-code standard, not a nicety.
- **Plan before code.** Document the architecture in `PLAN.md` before implementing a feature or phase.
  Big changes get a design section first; then build step by step, marking steps ✅ as verified.
- **Core is immutable to userspace.** `taskmaster_core` is a sealed contract. Never write docs or code
  that tell an app author to edit core, `gen_glyphs.py`, `main/`, or anything the OS owns. The app API
  (`docs/APP_API.md`) is the complete surface: if a capability isn't exposed, it's off-limits. Removing
  any "edit core" instruction from app-facing docs is expected.
- **One source of truth** per concern: wiring in `board_pins.h`, config in `nvs_config`, UI geometry in
  the UI headers, the app list in `apps.yaml`. Never duplicate.
- **Core ⟂ userspace.** Core never names or references a specific app. Apps live in their own repos and
  depend only on the public app API + display.

## Product / UX taste

- **1-bit aesthetic.** The mono OLED is 1-bit. **No anti-aliasing, no dithering** — the owner rejects
  "dots." Icons are rendered crisp: `gen_glyphs.py` alpha-thresholds `icons/` art to hard on/off pixels.
- **Pixel-perfect UI.** The owner iterates on placement to the pixel ("move it 2px left", "1px down").
  Expose positions as named offsets so nudges are one-line edits, then rebuild + reflash to check.
- **Glyphs over text where a convention exists**, with text as the always-safe fallback (see the hint
  bar). Keep the whole device speaking one visual language.

## Onboarding philosophy (Phase 6)

- **Zero self-hosting.** The maintainer hosts nothing but a GitHub repo. Developers fork a **template**
  (TM-Template), CI builds on push, and device-facing steps are **local Python tools** (`flash.py`,
  `ota_serve.py`). Local OTA runs over the LAN (plain http allowed for that path).
- **Don't over-build platform safety the owner didn't ask for.** Example: GPIO arbitration was
  *dropped* in favor of documenting the risk — apps use free pads at their own risk.

## Workflow

- **Verify on hardware, report honestly.** "Done" means built *and* checked on the device. If a build
  fails or a step was skipped, say so plainly with the evidence. Don't claim success you didn't confirm.
- **Don't build/flash from the core repo in normal work.** The buildable project is the **template**
  (`~/TaskMaster-App-Template`). A one-off verification build in core is acceptable *only* to confirm a
  core change compiles before pushing (so the template doesn't pull broken core) — say so when you do it.
- **Commit/push only when asked.** Don't push proactively.
- **Commit message conventions:**
  - **Core repo (TaskMaster):** end with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
  - **Userspace repos (TM-Template, TM-Yapp*, apps):** end with
    `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- **Branch off the default branch before committing** if you're on it and the task warrants a PR; but
  this project has mostly committed straight to `main` at the owner's direction.

## Communication

- Be **direct and concise**; the owner is technical and moves fast.
- Prefer **recommendations over option-dumps**, but surface a genuine decision (with a clear default)
  when the owner's answer changes the approach.
- When the owner gives terse UI feedback ("too big", "1px up"), treat it as a precise instruction and
  iterate quickly rather than re-designing.

## Context about the owner

- Builds the **hardware/enclosure in Onshape** (offline) — firmware and mechanical are separate tracks.
- Runs on macOS (Apple Silicon); ESP-IDF **v6.0.1**; device on `/dev/cu.usbmodem*`.
- Comfortable at the CLI; expects tools that don't require babysitting.
