#!/usr/bin/env python3
"""Generate the iOS app icon asset catalog from the original rich4.exe icon (the die with a red 4).

Same artwork and title-screen blue as scripts/make_android_icon.py: the 32x32 icon is upscaled by a
whole-number factor (nearest neighbour) onto an opaque 1024x1024 canvas, which serves as the single
universal iOS icon (Xcode 14+ derives every size from it) and the marketing icon. Output goes to a
generated, gitignored asset catalog because the artwork is part of the original game files.

    scripts/make_ios_icon.py game-assets/rich4.exe build/ios-generated/Assets.xcassets
"""
import argparse
import json
from pathlib import Path

from make_android_icon import BACKGROUND, decode_bmp_icon, foreground, pe_icon, png

SIZE = 1024
SCALE = 24  # 768px die: its corners stay well inside the iOS rounded-rectangle mask


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=Path)
    parser.add_argument("catalog", type=Path, help="generated .xcassets directory")
    args = parser.parse_args()
    icon = decode_bmp_icon(pe_icon(args.exe.read_bytes()))
    # Opaque RGB-equivalent canvas: App Store rules reject icons with transparency.
    canvas = foreground(icon, SIZE, SCALE, background=BACKGROUND)
    canvas = [[pixel[:3] + (255,) for pixel in row] for row in canvas]
    iconset = args.catalog / "AppIcon.appiconset"
    iconset.mkdir(parents=True, exist_ok=True)
    (iconset / "AppIcon-1024.png").write_bytes(png(canvas))
    info = {"author": "xcode", "version": 1}
    (args.catalog / "Contents.json").write_text(json.dumps({"info": info}, indent=2) + "\n")
    images = [{"filename": "AppIcon-1024.png", "idiom": "universal", "platform": "ios", "size": "1024x1024"}]
    (iconset / "Contents.json").write_text(json.dumps({"images": images, "info": info}, indent=2) + "\n")


if __name__ == "__main__":
    main()
