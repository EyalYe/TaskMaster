# Agent memory (Claude Code)

Snapshot of the assistant's persistent project memory, checked into the repo so it
travels between machines. On this machine it lives at:

```
~/.claude/projects/-Users-yeminie-TaskMaster/memory/
```

`MEMORY.md` is the index (one line per memory, loaded into context each session); the
other `.md` files are individual facts with frontmatter.

## Restore on a new machine

After cloning the repo, copy these back into the agent's memory dir for this project
(the path is derived from the project's absolute path, with `/` → `-`):

```bash
DST="$HOME/.claude/projects/-Users-<you>-TaskMaster/memory"   # adjust to your path
mkdir -p "$DST"
cp docs/agent-memory/*.md "$DST"/
```

Then start Claude Code in the repo — it loads `MEMORY.md` automatically. If the absolute
path differs from `/Users/yeminie/TaskMaster`, the memory content still applies (it's
about the project, not the path), but update any `~/TaskMaster` references as needed.

See [`SESSION1.md`](../../SESSION1.md) for the full session-1 handoff, and
[`CONT_PROMPT.md`](CONT_PROMPT.md) for a ready-to-paste prompt that bootstraps the next
agent.
