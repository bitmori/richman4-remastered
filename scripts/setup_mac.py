#!/usr/bin/env python3
"""Install a pinned, sandboxed Boxedwine runtime and a private copy of Rich4."""
import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CACHE = REPO / ".local"
RUNTIME = Path.home() / "Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine"
GAME = RUNTIME / "Rich4/game"
ROOT = RUNTIME / "Rich4/root"


def fetch(name, url, checksum):
    path = CACHE / name
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != checksum:
        temporary = path.with_suffix(path.suffix + ".part")
        subprocess.run(["curl", "-fL", "--retry", "2", "-o", str(temporary), url], check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != checksum:
            raise RuntimeError(f"Checksum mismatch: {name}")
        temporary.replace(path)
    return path


def verify_registry(family, font_name):
    prefix = ROOT / "home/username/.wine"
    system = (prefix / "system.reg").read_text()
    user = (prefix / "user.reg").read_text()
    expected = f'"{family} (TrueType)"="{font_name}"'
    alias = r'"\x7d30\x660e\x9ad4"=' + f'"{family}"'
    if expected not in system or alias not in system or '"FontSmoothing"="0"' not in user or '"DirectDrawRenderer"="gdi"' not in user:
        raise RuntimeError("Wine font or graphics configuration verification failed")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("game", type=Path, nargs="?", default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument("--font", type=Path, help="Local MingLiU TTC file (never redistributed)")
    args = parser.parse_args()
    if subprocess.run(["pgrep", "-x", "Boxedwine"], stdout=subprocess.DEVNULL).returncode == 0:
        raise SystemExit("Close Boxedwine before setup so the Wine registry is not modified while in use.")
    subprocess.run(["python3", str(REPO / "scripts/audit_assets.py"), str(args.game)], check=True)
    CACHE.mkdir(exist_ok=True)
    GAME.mkdir(parents=True, exist_ok=True)
    ROOT.mkdir(parents=True, exist_ok=True)
    mac = fetch("Boxedwine26R1Mac.zip",
                "https://github.com/danoon2/Boxedwine/releases/download/26R1.0/Boxedwine26R1Mac.zip",
                "47e46f24992206793e66b5e0548c9d0eddc301d176021284c731c327f8e2ec5f")
    engine = CACHE / "boxedwine/Boxedwine.app/Contents/MacOS/Boxedwine"
    if not engine.exists():
        # ditto preserves executable modes and the app's signed framework layout.
        subprocess.run(["ditto", "-xk", str(mac), str(CACHE / "boxedwine")], check=True)
    wine = fetch("TinyCore15Wine11.0.zip", "https://boxedwine.org/v2/6/TinyCore15Wine11.0.zip",
                 "8cb669813037589597a2f08c30fb7b8dc965a53e1344757f69ce5cb0f1958820")
    target_wine = RUNTIME / "FileSystems2" / wine.name
    target_wine.parent.mkdir(parents=True, exist_ok=True)
    if not target_wine.exists() or hashlib.sha256(target_wine.read_bytes()).hexdigest() != hashlib.sha256(wine.read_bytes()).hexdigest():
        shutil.copy2(wine, target_wine)
    candidates = [args.font] if args.font else [
        Path(f"/Applications/Microsoft {app}.app/Contents/Resources/DFonts/mingliu.ttc")
        for app in ("Word", "Excel", "PowerPoint")
    ]
    font = next((p for p in candidates if p.is_file()), None)
    if args.font and font is None:
        raise SystemExit(f"Font not found: {args.font}")
    family = "MingLiU"
    if font is None:
        family = "Noto Serif CJK TC"
        font = fetch("NotoSerifCJKtc-Regular.otf",
                     "https://raw.githubusercontent.com/notofonts/noto-cjk/main/Serif/OTF/TraditionalChinese/NotoSerifCJKtc-Regular.otf",
                     "234301038e76e7c35c43113785024700c4e4fe7bdce1d1fbbc42fca7e6683798")
    fonts = ROOT / "home/username/.wine/drive_c/windows/Fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    shutil.copy2(font, fonts / font.name)
    for source in args.game.iterdir():
        if source.is_file() and (source.suffix.lower() in {".mkf", ".mid", ".avi", ".wav"} or source.name.lower() == "rich4.exe"):
            destination = GAME / source.name
            if not destination.exists():
                shutil.copy2(source, destination)
    built = REPO / "artifacts/rich4_mscrt.exe"
    if built.exists():
        shutil.copy2(built, GAME / built.name)
    registry = r'''Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\Fonts]
"FONT_FAMILY (TrueType)"="FONT_FILE"

[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes]
"細明體"="FONT_FAMILY"
"新細明體"="FONT_FAMILY"
"MingLiU"=-
"PMingLiU"=-

[HKEY_CURRENT_USER\Control Panel\Desktop]
"FontSmoothing"="0"

[HKEY_CURRENT_USER\Software\Wine\Direct3D]
"DirectDrawRenderer"="gdi"
'''
    registry = registry.replace("FONT_FAMILY", family).replace("FONT_FILE", font.name)
    (GAME / "chinese-font.reg").write_text(registry, encoding="utf-16")
    completed = subprocess.run([str(engine), "-root", str(ROOT), "-zip", str(target_wine),
                    "-mount_drive", str(GAME), "d", "-env", "WINEDLLOVERRIDES=mscoree,mshtml=",
                    "/bin/wine", "regedit", "/S", r"D:\chinese-font.reg"], timeout=120)
    # This Boxedwine release can return 1 after a successful Wine shutdown.
    # Confirm the actual registry writes instead of hiding arbitrary failures.
    if completed.returncode not in (0, 1):
        raise RuntimeError(f"Wine configuration did not complete (exit {completed.returncode})")
    verify_registry(family, font.name)
    print(f"Ready. Run: {REPO / 'scripts/run_mac.sh'}")
    print(f"Game files and saves: {GAME}")
    print(f"Font: {family} ({font})")


if __name__ == "__main__":
    main()
