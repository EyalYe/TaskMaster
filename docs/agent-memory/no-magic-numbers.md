---
name: no-magic-numbers
description: TaskMaster code style — no explicit numeric literals in code; use named defines/enums in headers
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 1ffe722c-73fa-4024-a1d9-4de2d4a260f2
---

In TaskMaster, **no explicit "magic" numbers anywhere in the code**. Every numeric literal (pixel coordinates, sizes, stack depths, timeouts, counts, buffer lengths, etc.) must be a **named `#define` or enum** with a meaningful name, declared in the relevant **header file**.

**Why:** the user considers this a crucial part of professional code — it makes intent self-documenting, changes single-sourced, and geometry/config tunable in one place. Stated 2026 (during Phase 3 LVGL UI work, after pixel coordinates were scattered as literals in ui_frame.c).

**How to apply:**
- When writing/editing any C: replace literals with named constants in the header (e.g. `HINT_BAR_W`, `UI_ROW_H`, `UI_TASK_STACK`), not inline numbers.
- Exceptions are only the truly self-evident (`0`, `1`, `-1` as sentinels/indices, `2` for halving). Everything semantic gets a name.
- Applies to new code and to code you touch — clean up literals as you go.
- This is a documented project standard: PLAN.md (§11 conventions) and README.md.

Related: [[taskmaster-c3-project]]
