# Agent onboarding — start here

This folder is the **complete context for an AI agent (or new contributor)** working on
TaskMaster-C3. It's checked into the repo so it travels with the code across machines and
sessions (unlike a tool's local memory). Read it before making changes.

## Read order

1. **[CONTEXT.md](CONTEXT.md)** — what the project is, the repo topology, the architecture,
   and exactly where things stand.
2. **[PREFERENCES.md](PREFERENCES.md)** — the owner's non-negotiable standards + how they like
   to work. Follow these; they override defaults.
3. **[skills/](skills/)** — task recipes, read as needed:
   - [build-and-flash.md](skills/build-and-flash.md) — build, flash, serial, all the gotchas
   - [add-an-app.md](skills/add-an-app.md) — the userspace app workflow (never touches core)
   - [glyphs.md](skills/glyphs.md) — the 1-bit icon pipeline (`gen_glyphs.py`)
   - [onboarding-and-ota.md](skills/onboarding-and-ota.md) — the template repo, CI, local flash/OTA tools
   - [core-development.md](skills/core-development.md) — working *inside* the sealed core
   - [debugging.md](skills/debugging.md) — serial capture, resets, panics

## The five golden rules (details in PREFERENCES.md)

1. **Core is immutable to app authors.** `taskmaster_core` is a sealed contract; apps use the
   public headers and never reach into it.
2. **No magic numbers.** Every literal is a named `#define`/enum in a header.
3. **Plan before code.** Document architecture in `PLAN.md` before implementing.
4. **Verify on hardware.** "Done" means built *and* checked on the device, honestly reported.
5. **Zero self-hosting.** Onboarding is a fork template + GitHub CI + local Python tools.

## Where the authoritative detail lives

- **[`../../PLAN.md`](../../PLAN.md)** — the full spec + phase-by-phase build log (source of truth
  for design). This folder summarizes; PLAN.md is exhaustive.
- **[`../APP_API.md`](../APP_API.md)** — the app-author contract (the public surface of the OS).
- **[`../agent-memory/`](../agent-memory/)** — older session-handoff notes (SESSION1, CONT_PROMPT);
  this `agents/` folder supersedes them as the durable knowledge base.
