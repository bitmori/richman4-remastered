#!/usr/bin/env bash
set -euo pipefail
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM_DIR="$REPO_DIR/.local/upstream"
REVISION=ec7d70e693059d1e781d9d1cc2ad7f36d360d39e
for tool in git make nasm i686-w64-mingw32-gcc; do
  command -v "$tool" >/dev/null || { echo "Missing $tool. On Mac: brew install mingw-w64 nasm" >&2; exit 1; }
done
mkdir -p "$REPO_DIR/.local" "$REPO_DIR/artifacts"
if [[ ! -d "$UPSTREAM_DIR/.git" ]]; then
  git clone https://github.com/mytbk/rich4 "$UPSTREAM_DIR"
  git -C "$UPSTREAM_DIR" checkout --detach "$REVISION"
fi
[[ "$(git -C "$UPSTREAM_DIR" rev-parse HEAD)" == "$REVISION" ]] || {
  echo "Unexpected upstream revision; keep local changes and check manually." >&2; exit 1;
}
make -C "$UPSTREAM_DIR/asm" -j8
cp "$UPSTREAM_DIR/asm/rich4_mscrt.exe" "$REPO_DIR/artifacts/rich4_mscrt.exe"
file "$REPO_DIR/artifacts/rich4_mscrt.exe"
