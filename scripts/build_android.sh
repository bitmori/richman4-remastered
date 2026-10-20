#!/usr/bin/env bash
# Build the debug-signed Rich4 APK for sideloading (arm64-v8a only).
# NEVER upload this APK to any Play track; it is a local packaging spike.
#
#   scripts/build_android.sh                  # build only
#   scripts/build_android.sh emulator-5554    # build and install to that adb serial
#
# Third-party sources are downloaded once into android/third_party/ (gitignored)
# and verified by SHA-256. Original game files are read from game-assets/
# (gitignored; override with RICH4_GAME_ASSETS) and packaged into the APK, then
# extracted to internal storage on first launch (android/app/src/main/cpp/rich4_android.cpp).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ANDROID_DIR="$REPO/android"
THIRD_PARTY="$ANDROID_DIR/third_party"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
SERIAL="${1:-}"

fetch() { # url file sha256
    local url="$1" file="$THIRD_PARTY/downloads/$2" sum="$3"
    mkdir -p "$THIRD_PARTY/downloads"
    if [[ ! -f "$file" ]] || ! echo "$sum  $file" | shasum -a 256 -c --status; then
        echo "Downloading $2"
        curl -fsSL --retry 3 -o "$file.part" "$url"
        echo "$sum  $file.part" | shasum -a 256 -c --status || { echo "Checksum mismatch: $2" >&2; exit 1; }
        mv "$file.part" "$file"
    fi
}
unpack() { # file directory
    if [[ ! -d "$THIRD_PARTY/$2" ]]; then
        tar -xf "$THIRD_PARTY/downloads/$1" -C "$THIRD_PARTY"
    fi
}

fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz \
    SDL3-3.4.16.tar.gz 7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
fetch https://download.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.xz \
    freetype-2.14.3.tar.xz 36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
# Bionic iconv has no BIG5 converter, so GNU libiconv is linked statically.
fetch https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz \
    libiconv-1.18.tar.gz 3b08f5f4f9b4eb82f151a7040bfd6fe6c6fb922efe4b1659c66ea933276965e8
# General MIDI bank for TinySoundFont (TimGM6mb, GPL-2; Debian's pristine upstream tarball).
fetch https://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/timgm6mb-soundfont_1.3.orig.tar.gz \
    timgm6mb-soundfont_1.3.orig.tar.gz af8f3a00e416dfb262bcaa904a1c84df04a51b72bbc1313aed012bc754bdf99b

unpack SDL3-3.4.16.tar.gz SDL3-3.4.16
unpack freetype-2.14.3.tar.xz freetype-2.14.3
unpack libiconv-1.18.tar.gz libiconv-1.18
if [[ ! -f "$THIRD_PARTY/downloads/TimGM6mb.sf2" ]]; then
    tar -xzf "$THIRD_PARTY/downloads/timgm6mb-soundfont_1.3.orig.tar.gz" -C "$THIRD_PARTY/downloads" \
        --strip-components 1 timgm6mb-soundfont_1.3/TimGM6mb.sf2
fi

# Original game files (never committed). Override with RICH4_GAME_ASSETS, e.g. from a worktree.
GAME_ASSETS="${RICH4_GAME_ASSETS:-$REPO/game-assets}"
if [[ ! -f "$GAME_ASSETS/Data.mkf" ]]; then
    echo "Missing original game files in $GAME_ASSETS (set RICH4_GAME_ASSETS)" >&2
    exit 1
fi
# Optional bundled Traditional Chinese font (OFL). Without it the app uses the system Noto CJK font.
FONT="${RICH4_ANDROID_FONT:-$REPO/.local/NotoSerifCJKtc-Regular.otf}"

# Launcher icon: the original desktop icon (die with a red 4) from rich4.exe, generated not committed.
python3 "$REPO/scripts/make_android_icon.py" "$GAME_ASSETS/rich4.exe" "$ANDROID_DIR/app/build/generated/rich4Res"

export ANDROID_HOME="$SDK"
echo "sdk.dir=$SDK" > "$ANDROID_DIR/local.properties"
GRADLE_ARGS=(--no-daemon "-Prich4.gameAssets=$GAME_ASSETS")
[[ -f "$FONT" ]] && GRADLE_ARGS+=("-Prich4.font=$FONT")
(cd "$ANDROID_DIR" && ./gradlew "${GRADLE_ARGS[@]}" :app:assembleDebug)

APK="$ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
ls -lh "$APK"
if [[ -n "$SERIAL" ]]; then
    "$SDK/platform-tools/adb" -s "$SERIAL" install -r "$APK"
fi
