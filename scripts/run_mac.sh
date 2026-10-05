#!/usr/bin/env bash
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_DIR="$HOME/Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine"
GAME_DIR="$RUNTIME_DIR/Rich4/game"
ENGINE="$REPO_DIR/.local/boxedwine/Boxedwine.app/Contents/MacOS/Boxedwine"
WINE_ZIP="$RUNTIME_DIR/FileSystems2/TinyCore15Wine11.0.zip"
EXE="${1:-rich4_mscrt.exe}"
case "$EXE" in rich4.exe|rich4_mscrt.exe) ;; *) echo "Usage: $0 [rich4.exe|rich4_mscrt.exe]" >&2; exit 1;; esac
[[ -x "$ENGINE" && -f "$WINE_ZIP" && -f "$GAME_DIR/$EXE" ]] || {
  echo "Runtime not ready. See README.md for setup." >&2; exit 1;
}
mkdir -p "$REPO_DIR/artifacts"
exec "$ENGINE" -root "$RUNTIME_DIR/Rich4/root" -zip "$WINE_ZIP" \
  -mount_drive "$GAME_DIR" d -w /mnt/drive_d -resolution 640x480 -bpp 16 -scale 100 -scale_quality 0 \
  -env 'WINEDLLOVERRIDES=mscoree,mshtml=' -env LANG=zh_TW.UTF-8 \
  /bin/wine "D:\\$EXE" > "$REPO_DIR/artifacts/mac-runtime.log" 2>&1
