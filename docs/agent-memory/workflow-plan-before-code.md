---
name: workflow-plan-before-code
description: Document architecture in PLAN.md before writing implementation code
metadata:
  type: feedback
---

On TaskMaster, when asked to build a new mechanism/feature, the user wants it written into
`PLAN.md` as a proper architectural section **first**, then implemented — not code-first.

**Why:** PLAN.md is the source of truth; the user reviews the architecture there before committing
to code. (Demonstrated repeatedly, e.g. the in-tree→component app-registry design.)

**How to apply:** For a new subsystem, add/extend the relevant PLAN.md section (interfaces, file
layout, build semantics, trade-offs) and let it settle, then implement. Relates to
[[taskmaster-c3-project]].
