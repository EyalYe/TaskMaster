#!/usr/bin/env bash
#
# backup_build.sh — snapshot the flashable artifacts of a successful build into
# build_backups/<YYYY-MM-DD_HH-MM-SS>/ (gitignored). Wired as a CMake POST_BUILD
# hook (see top-level CMakeLists.txt), so it runs after every successful build.
#
# Best-effort: never fails the build (always exits 0). Skips if the app image is
# identical to the most recent backup (no churn on no-op relinks).

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$PROJ/build"
BACKUPS="$PROJ/build_backups"
APP_BIN="$BUILD/taskmaster_c3.bin"

[ -f "$APP_BIN" ] || { echo "[backup] no app image yet, skipping"; exit 0; }

# Dedup against the newest existing backup.
LATEST="$(ls -1dt "$BACKUPS"/*/ 2>/dev/null | head -1)"
if [ -n "$LATEST" ] && [ -f "${LATEST}taskmaster_c3.bin" ] && cmp -s "$APP_BIN" "${LATEST}taskmaster_c3.bin"; then
    echo "[backup] image unchanged since $(basename "$LATEST"), skipping"
    exit 0
fi

TS="$(date '+%Y-%m-%d_%H-%M-%S')"
DEST="$BACKUPS/$TS"
mkdir -p "$DEST" || exit 0

# Flashable set (anything missing is simply skipped).
cp "$APP_BIN"                                  "$DEST/" 2>/dev/null
cp "$BUILD/taskmaster_c3.elf"                  "$DEST/" 2>/dev/null
cp "$BUILD/taskmaster_c3.map"                  "$DEST/" 2>/dev/null
cp "$BUILD/bootloader/bootloader.bin"          "$DEST/" 2>/dev/null
cp "$BUILD/partition_table/partition-table.bin" "$DEST/" 2>/dev/null
cp "$BUILD/ota_data_initial.bin"               "$DEST/" 2>/dev/null
cp "$BUILD/flasher_args.json"                  "$DEST/" 2>/dev/null

# size helper (macOS vs GNU stat)
bytes() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null; }

{
    echo "timestamp:  $TS"
    echo "git_commit: $(git -C "$PROJ" rev-parse --short HEAD 2>/dev/null) ($(git -C "$PROJ" symbolic-ref --short HEAD 2>/dev/null))"
    echo "git_dirty:  $(git -C "$PROJ" status --porcelain 2>/dev/null | wc -l | tr -d ' ') changed file(s)"
    echo "idf:        ${IDF_VERSION:-unknown}"
    echo "app_size:   $(bytes "$APP_BIN") bytes"
} > "$DEST/build-info.txt" 2>/dev/null

echo "[backup] saved -> build_backups/$TS"
exit 0
