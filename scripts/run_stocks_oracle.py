#!/usr/bin/env python3
"""Capture synthetic stock cases from the verified original EXE, without starting the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

from audit_assets import EXPECTED_EXE

REPO = Path(__file__).resolve().parent.parent
RUNTIME = Path.home() / "Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, nargs="?", default=Path.home() / "Downloads/rich4-original-game")
    parser.add_argument("--native-test", type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    files = {p.name.lower(): p for p in args.game.expanduser().iterdir() if p.is_file()}
    source = files.get("rich4.exe")
    if source is None or sha256(source) != EXPECTED_EXE:
        raise SystemExit("Stock oracle requires the verified Rich4 v3.11 original EXE")
    engine = REPO / ".local/boxedwine/Boxedwine.app/Contents/MacOS/Boxedwine"
    wine = RUNTIME / "FileSystems2/TinyCore15Wine11.0.zip"
    template = RUNTIME / "Rich4/root"
    if not all(p.exists() for p in (engine, wine, template)):
        raise SystemExit("Configured isolated Boxedwine runtime is required")
    run = RUNTIME / "Rich4-stocks-oracle" / str(time.time_ns())
    work = run / "work"
    work.mkdir(parents=True)
    shutil.copytree(template, run / "root", symlinks=True)
    vulkan = run / "root/lib/libvulkan.so.1"
    vulkan.parent.mkdir(parents=True, exist_ok=True)
    vulkan.unlink(missing_ok=True)
    vulkan.write_bytes(b"")
    shutil.copy2(source, work / "rich4.exe")
    adapter = work / "original_stocks.c"
    shutil.copy2(REPO / "tools/oracle/original_stocks.c", adapter)
    helper = work / "original_stocks.exe"
    build = ["i686-w64-mingw32-gcc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror", "-mwindows",
             "-Wl,--image-base,0x10000000", "-Wl,--disable-dynamicbase", str(adapter), "-o", str(helper)]
    subprocess.run(build, check=True)
    command = [str(engine), "-novideo", "-nosound", "-root", str(run / "root"), "-zip", str(wine),
               "-mount_drive", str(work), "d", "-w", "/mnt/drive_d", "-env", "WINEDEBUG=-all",
               "-env", "WINEDLLOVERRIDES=winemenubuilder.exe,mscoree,mshtml,winevulkan,vulkan-1=",
               "-env", "WINE_D3D_CONFIG=renderer=no3d", "/bin/wine", r"D:\original_stocks.exe"]
    environment = os.environ.copy()
    host = dict(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software", SDL_MAC_BACKGROUND_APP="1")
    environment.update(host)
    print(f"Capturing original stock instructions in private windowless process: {run}", flush=True)
    with (run / "wine.log").open("wb") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment, start_new_session=True)
        try:
            code = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise SystemExit(f"Stock oracle timed out: {run / 'wine.log'}")
    completion = work / "complete.txt"
    if code not in (0, 1) or not completion.exists() or completion.read_text().strip() != "original-stocks-v1-self-checked":
        raise SystemExit(f"Original stock capture failed; inspect {run}")
    outputs = {}
    for name, expected_rows in (("prices.csv", 100618), ("sales.csv", 104), ("updates.csv", 2184)):
        path = work / name
        rows = len(path.read_text().splitlines()) - 1
        if rows != expected_rows:
            raise SystemExit(f"Incomplete oracle output: {path}, expected {expected_rows}, got {rows}")
        outputs[name] = {"rows": rows, "sha256": sha256(path)}
    manifest = {"schema": "original-stocks-v1", "oracle": "original-rich4-v3.11-machine-code",
                "exe_sha256": EXPECTED_EXE, "helper_sha256": sha256(helper), "adapter_sha256": sha256(adapter),
                "build_command": build, "command": command, "host_environment": host, "exit_code": code,
                "functions": {"price": "0x428ec5", "sell": "0x428e23", "forced_update": "0x429040"},
                "x87_control_word": "0x037f", "self_check": "1000 + 10 percent = 1100",
                "in_memory_ui_stub": {"address": "0x40a4e1", "replacement": "ret", "reason": "suppress minimap pixel refresh only"},
                "outputs": outputs}
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    local = REPO / ".local/stocks-oracle"
    local.mkdir(exist_ok=True)
    latest = local / "latest"
    if latest.is_symlink():
        latest.unlink()
    if latest.exists():
        raise SystemExit(f"Refusing to replace a real directory: {latest}")
    latest.symlink_to(work, target_is_directory=True)
    print(json.dumps(outputs, indent=2))
    print(f"Evidence: {latest / 'manifest.json'}")
    if args.native_test:
        subprocess.run([str(args.native_test.resolve()), str(work)], check=True)


if __name__ == "__main__":
    main()
