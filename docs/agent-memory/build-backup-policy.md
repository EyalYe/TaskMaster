---
name: build-backup-policy
description: Every successful TaskMaster build must be backed up, timestamped, to a gitignored dir
metadata:
  type: feedback
---

On TaskMaster, **every successful build must be backed up** into a gitignored
`build_backups/<YYYY-MM-DD_HH-MM-SS>/` directory, automatically.

**Why:** the user wants a timestamped history of flashable builds (for reflash/rollback) without
committing build artifacts to git.

**How to apply:** Implemented as a **CMake POST_BUILD hook** in the top-level `CMakeLists.txt` that
runs `tools/backup_build.sh` after `${PROJECT_NAME}.elf` — so it fires on any `idf.py build`. The
script copies the flashable set (app .bin, bootloader.bin, partition-table.bin, ota_data_initial.bin,
flasher_args.json, elf/map) + a `build-info.txt` (timestamp, git commit, idf version, size); it
dedups against the newest backup and is best-effort (never fails the build). `build_backups/` is in
`.gitignore`. Don't remove this hook. Relates to [[taskmaster-c3-project]].
