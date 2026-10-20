#!/usr/bin/env bash
# Build the iOS app (io.divby0.rich4, "大富翁4") with CMake's Xcode generator.
# NEVER upload this build anywhere (no App Store / TestFlight); it is for local installs only.
#
#   scripts/build_ios.sh                          # simulator build (arm64, ad-hoc signed)
#   scripts/build_ios.sh --run [SIMULATOR]        # ... then boot, install and launch on a simulator
#                                                 #     (UDID or name; default "iPhone 16 Pro")
#   scripts/build_ios.sh --run SIM -- --auto-rounds 1   # extra arguments after -- go to the game
#   RICH4_IOS_TEAM=ABCDE12345 scripts/build_ios.sh --device [DEVICE_ID]
#                                                 # device build signed for that team (Automatic
#                                                 # signing); installs + launches when DEVICE_ID
#                                                 # (from `xcrun devicectl list devices`) is given
#
# Third-party sources are downloaded once into ios/third_party/ (gitignored) and verified by
# SHA-256 (same pins as scripts/build_android.sh). Original game files are read from game-assets/
# (gitignored; override with RICH4_GAME_ASSETS) and copied into the app bundle at build time.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY="$REPO/ios/third_party"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

MODE=simulator RUN=0 TARGET_DEVICE="" GAME_ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --run) RUN=1; if [[ $# -gt 1 && "$2" != --* ]]; then TARGET_DEVICE="$2"; shift; fi ;;
        --device) MODE=device; if [[ $# -gt 1 && "$2" != --* ]]; then TARGET_DEVICE="$2"; shift; fi ;;
        --) shift; GAME_ARGS=("$@"); break ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ "$MODE" == device && -z "${RICH4_IOS_TEAM:-}" ]]; then
    echo "Device builds need RICH4_IOS_TEAM=<10-character Apple team ID>" >&2
    exit 2
fi

fetch() { # url file sha256
    local url="$1" file="$THIRD_PARTY/downloads/$2" sum="$3"
    mkdir -p "$THIRD_PARTY/downloads"
    if [[ ! -f "$file" ]] || ! echo "$sum  $file" | shasum -a 256 -c --status; then
        # Reuse the Android download cache when it already holds the same verified file.
        local cached="$REPO/android/third_party/downloads/$2"
        if [[ -f "$cached" ]] && echo "$sum  $cached" | shasum -a 256 -c --status; then
            cp "$cached" "$file"
            return
        fi
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
# General MIDI bank for TinySoundFont (TimGM6mb, GPL-2; Debian's pristine upstream tarball).
fetch https://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/timgm6mb-soundfont_1.3.orig.tar.gz \
    timgm6mb-soundfont_1.3.orig.tar.gz af8f3a00e416dfb262bcaa904a1c84df04a51b72bbc1313aed012bc754bdf99b
# iOS keeps its CJK system fonts in private paths, so the app bundles Noto Serif CJK TC (OFL).
# (No libiconv download: the iOS system libiconv converts BIG5; ios/rich4_ios.mm self-tests it.)
fetch https://raw.githubusercontent.com/notofonts/noto-cjk/main/Serif/OTF/TraditionalChinese/NotoSerifCJKtc-Regular.otf \
    NotoSerifCJKtc-Regular.otf 234301038e76e7c35c43113785024700c4e4fe7bdce1d1fbbc42fca7e6683798

unpack SDL3-3.4.16.tar.gz SDL3-3.4.16
unpack freetype-2.14.3.tar.xz freetype-2.14.3
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
FONT="${RICH4_IOS_FONT:-$THIRD_PARTY/downloads/NotoSerifCJKtc-Regular.otf}"

# App icon: the original desktop icon (die with a red 4) from rich4.exe, generated not committed.
GENERATED="$REPO/build/ios-generated"
python3 "$REPO/scripts/make_ios_icon.py" "$GAME_ASSETS/rich4.exe" "$GENERATED/Assets.xcassets"

CMAKE_ARGS=(-G Xcode -S "$REPO/ios" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 "-DRICH4_GAME_ASSETS=$GAME_ASSETS" "-DRICH4_FONT=$FONT"
    "-DRICH4_ASSET_CATALOG=$GENERATED/Assets.xcassets")
BUILD_ARGS=()
if [[ "$MODE" == device ]]; then
    BUILD_DIR="$REPO/build/ios-device"
    # Only the app target is signed (ios/CMakeLists.txt); probes and static libraries are not.
    CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT=iphoneos "-DRICH4_IOS_TEAM=$RICH4_IOS_TEAM"
        -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
    BUILD_ARGS+=(-- -allowProvisioningUpdates)
    PRODUCTS=Release-iphoneos
else
    BUILD_DIR="$REPO/build/ios-simulator"
    CMAKE_ARGS+=(-DCMAKE_OSX_SYSROOT=iphonesimulator)
    PRODUCTS=Release-iphonesimulator
fi
cmake "${CMAKE_ARGS[@]}" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --config Release --target rich4 ${BUILD_ARGS[@]+"${BUILD_ARGS[@]}"}

APP="$BUILD_DIR/$PRODUCTS/rich4.app"
du -sh "$APP"
BUNDLE_ID=io.divby0.rich4

if [[ "$MODE" == device ]]; then
    if [[ -n "$TARGET_DEVICE" ]]; then
        xcrun devicectl device install app --device "$TARGET_DEVICE" "$APP"
        xcrun devicectl device process launch --device "$TARGET_DEVICE" "$BUNDLE_ID" ${GAME_ARGS[@]+"${GAME_ARGS[@]}"} ||
            echo "Installed; launch it from the home screen (first run: trust the developer in Settings)."
    fi
elif [[ "$RUN" == 1 ]]; then
    SIMULATOR="${TARGET_DEVICE:-iPhone 16 Pro}"
    xcrun simctl boot "$SIMULATOR" 2>/dev/null || true # already booted is fine
    xcrun simctl bootstatus "$SIMULATOR" -b >/dev/null
    open -a Simulator || true
    xcrun simctl install "$SIMULATOR" "$APP"
    xcrun simctl launch --terminate-running-process "$SIMULATOR" "$BUNDLE_ID" ${GAME_ARGS[@]+"${GAME_ARGS[@]}"}
fi
