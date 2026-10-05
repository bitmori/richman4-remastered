#!/usr/bin/env python3
"""Generate the Android launcher icon from the original rich4.exe icon (the die with a red 4).

The 32x32 256-colour icon is upscaled by whole-number factors (nearest neighbour) so it stays a
crisp pixel image at every density, then centred as an adaptive-icon foreground. Output goes to a
generated, gitignored resource directory because the artwork is part of the original game files.

    scripts/make_android_icon.py game-assets/rich4.exe android/app/build/generated/rich4Res
    scripts/make_android_icon.py game-assets/rich4.exe OUT --preview preview.png
"""
import argparse
import struct
import zlib
from pathlib import Path

BACKGROUND = (0x1D, 0x4E, 0x9C)  # deep blue of the title screen's sea
# density: (foreground canvas px = 108dp, integer upscale of the 32px die). The die's far corners
# sit ~0.65 x side from the centre, so the side stays <= ~51dp to survive circular launcher masks
# (72dp visible diameter).
DENSITIES = {"mdpi": (108, 1), "hdpi": (162, 2), "xhdpi": (216, 3), "xxhdpi": (324, 4), "xxxhdpi": (432, 6)}


def pe_icon(exe: bytes) -> bytes:
    """Return the largest RT_ICON image (a BMP without file header) from a PE file."""
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    sections, optional_size = struct.unpack_from("<H", exe, pe + 6)[0], struct.unpack_from("<H", exe, pe + 20)[0]
    optional = pe + 24
    resource_rva = struct.unpack_from("<I", exe, optional + 96 + 2 * 8)[0]
    table = [struct.unpack_from("<8sIIII", exe, optional + optional_size + 40 * i) for i in range(sections)]

    def offset(rva):
        for _, virtual_size, address, raw_size, raw in table:
            if address <= rva < address + max(virtual_size, raw_size):
                return rva - address + raw
        raise ValueError(f"RVA {rva:#x} outside sections")

    base = offset(resource_rva)

    def entries(directory):
        named, ids = struct.unpack_from("<HH", exe, base + directory + 12)
        for i in range(named + ids):
            yield struct.unpack_from("<II", exe, base + directory + 16 + 8 * i)

    def leaf(target):
        while target & 0x80000000:
            target = next(entries(target & 0x7FFFFFFF))[1]
        rva, size = struct.unpack_from("<II", exe, base + target)
        return exe[offset(rva) : offset(rva) + size]

    icons = [leaf(target) for kind, target in entries(0) if kind == 3 for _, target in entries(target & 0x7FFFFFFF)]
    if not icons:
        raise ValueError("no RT_ICON resource")
    return max(icons, key=lambda bmp: struct.unpack_from("<i", bmp, 4)[0])


def decode_bmp_icon(bmp: bytes):
    """Decode an icon BMP (1/4/8/24/32 bpp + AND mask) into rows of RGBA tuples, top row first."""
    header, width, height2, _, bpp = struct.unpack_from("<IiiHH", bmp, 0)
    height = height2 // 2
    colours = struct.unpack_from("<I", bmp, 32)[0] or (1 << bpp if bpp <= 8 else 0)
    palette = [tuple(bmp[header + 4 * i : header + 4 * i + 3][::-1]) for i in range(colours)]
    pixels = header + 4 * colours
    stride = (width * bpp + 31) // 32 * 4
    mask_offset, mask_stride = pixels + stride * height, (width + 31) // 32 * 4
    rows = []
    for y in range(height - 1, -1, -1):  # BMP rows are bottom-up
        row, line = [], pixels + y * stride
        for x in range(width):
            if bpp <= 8:
                index = (bmp[line + x * bpp // 8] >> (8 - bpp - (x * bpp) % 8)) & ((1 << bpp) - 1)
                r, g, b = palette[index]
                a = 255
            else:
                b, g, r = bmp[line + x * bpp // 8 : line + x * bpp // 8 + 3]
                a = bmp[line + x * 4 + 3] if bpp == 32 else 255
            if bpp != 32 and (bmp[mask_offset + y * mask_stride + x // 8] >> (7 - x % 8)) & 1:
                a = 0
            row.append((r, g, b, a))
        rows.append(row)
    return rows


def png(rows) -> bytes:
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    raw = b"".join(b"\0" + bytes(c for pixel in row for c in pixel) for row in rows)
    head = struct.pack(">IIBBBBB", len(rows[0]), len(rows), 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", head) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def foreground(icon, size, scale, background=None):
    canvas = [[background + (255,) if background else (0, 0, 0, 0)] * size for _ in range(size)]
    side = len(icon) * scale
    top = left = (size - side) // 2
    for y in range(side):
        for x in range(side):
            pixel = icon[y // scale][x // scale]
            if pixel[3]:
                canvas[top + y][left + x] = pixel
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("exe", type=Path)
    parser.add_argument("res", type=Path, help="generated Android res directory")
    parser.add_argument("--preview", type=Path, help="also write a flattened 432px preview PNG")
    args = parser.parse_args()
    icon = decode_bmp_icon(pe_icon(args.exe.read_bytes()))
    for density, (size, scale) in DENSITIES.items():
        directory = args.res / f"mipmap-{density}"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "ic_launcher_foreground.png").write_bytes(png(foreground(icon, size, scale)))
    anydpi = args.res / "mipmap-anydpi-v26"
    anydpi.mkdir(parents=True, exist_ok=True)
    (anydpi / "ic_launcher.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '    <background android:drawable="@color/ic_launcher_background" />\n'
        '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
        "</adaptive-icon>\n"
    )
    values = args.res / "values"
    values.mkdir(parents=True, exist_ok=True)
    (values / "ic_launcher_colors.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
        '    <color name="ic_launcher_background">#%02X%02X%02X</color>\n</resources>\n' % BACKGROUND
    )
    if args.preview:
        args.preview.write_bytes(png(foreground(icon, *DENSITIES["xxxhdpi"], background=BACKGROUND)))


if __name__ == "__main__":
    main()
