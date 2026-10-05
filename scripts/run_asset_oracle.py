#!/usr/bin/env python3
"""Dump original v3.11 MKF decode results in an isolated Boxedwine prefix."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import time
from pathlib import Path

from audit_assets import EXPECTED_EXE, REQUIRED, audit
from oracle_manifest import SCHEMA, validate_capture, validate_decoded_files

REPO = Path(__file__).resolve().parent.parent
RUNTIME = Path.home() / "Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare_native(tool, work, game):
    work = Path(work).resolve()  # Freeze a "latest" symlink for this comparison.
    manifest = validate_capture(work, game)
    files = {p.name.lower(): p for p in game.iterdir() if p.is_file()}
    destination = REPO / ".local/oracle" / ("native-" + str(time.time_ns()))
    destination.mkdir(parents=True)
    for archive in manifest["archives"]:
        source = files[archive["name"]]
        if sha256(source) != archive["sha256"]:
            raise SystemExit(f"Source changed since oracle capture: {source}")
        subprocess.run([str(tool), "dump", str(source), str(destination / archive["name"])], check=True)
    validate_decoded_files(destination, manifest, check_hashes=False)
    counts = {"compressed": 0, "uncompressed": 0, "empty": 0}
    mismatches = []
    for entry in manifest["entries"]:
        original = work / "decoded" / entry["file"]
        native = destination / entry["file"]
        original_bytes = original.read_bytes()
        if hashlib.sha256(original_bytes).hexdigest() != entry["sha256"]:
            raise SystemExit(f"Original output changed after capture: {original}")
        kind = "empty" if entry["size"] == 0 else (
            "uncompressed" if entry["size"] == entry["packed_size"] else "compressed")
        counts[kind] += 1
        # Compare actual bytes, not just digests; hashes keep the report reproducible.
        if not native.is_file() or native.read_bytes() != original_bytes:
            mismatches.append(entry["file"])
    report = {"oracle_manifest": str(work / "manifest.json"), "native_tool": str(tool),
              "native_tool_sha256": sha256(tool), "entries": len(manifest["entries"]),
              "categories": counts, "mismatches": mismatches, "compared_byte_for_byte": True}
    report_path = destination / "comparison.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print(f"Comparison report: {report_path}")
    if mismatches:
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", nargs="?", type=Path,
                        default=Path.home() / "Downloads/rich4-original-game")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--native-tool", type=Path, help="Native rich4-tool executable to compare byte-for-byte")
    parser.add_argument("--reuse-oracle", action="store_true", help="Compare a previous capture at .local/oracle/latest")
    args = parser.parse_args()
    game = args.game.expanduser().resolve()
    checked = audit(game)
    if checked["errors"] or checked["exe_sha256"] != EXPECTED_EXE:
        raise SystemExit("Oracle requires the verified original v3.11 EXE and valid MKF indexes")
    if args.reuse_oracle:
        if not args.native_tool:
            raise SystemExit("--reuse-oracle requires --native-tool")
        compare_native(args.native_tool.expanduser().resolve(), REPO / ".local/oracle/latest", game)
        return
    engine = REPO / ".local/boxedwine/Boxedwine.app/Contents/MacOS/Boxedwine"
    wine = RUNTIME / "FileSystems2/TinyCore15Wine11.0.zip"
    if not engine.is_file() or not wine.is_file():
        raise SystemExit("Missing existing Boxedwine/Wine runtime; see README.md")
    if not shutil.which("i686-w64-mingw32-gcc"):
        raise SystemExit("Install MinGW: brew install mingw-w64")

    # Both the Wine prefix and drive D are private to this validation run.
    # Never point at Rich4/root or the user's active game directory.
    run_id = time.strftime("%Y%m%d-%H%M%S") + f"-{time.time_ns() % 1_000_000:06}"
    run = RUNTIME / "Rich4-oracle" / run_id
    work = run / "work"
    (work / "input").mkdir(parents=True)
    (run / "root").mkdir()
    sources = {p.name.lower(): p for p in game.iterdir() if p.is_file()}
    for name in ("rich4.exe", *REQUIRED):
        shutil.copy2(sources[name], work / "input" / name)
    # Capture the exact adapter sources before compiling, even if another
    # collaborator edits the repository while this run is in progress.
    adapter_source = work / "adapter-source"
    adapter_source.mkdir()
    for source in (REPO / "tools/oracle").iterdir():
        if source.suffix in (".c", ".h"):
            shutil.copy2(source, adapter_source / source.name)
    helper = work / "original_mkf.exe"
    compiler = subprocess.check_output(["i686-w64-mingw32-gcc", "--version"], text=True).splitlines()[0]
    build_command = ["i686-w64-mingw32-gcc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                     "-Wl,--image-base,0x10000000", "-Wl,--disable-dynamicbase",
                     str(adapter_source / "original_mkf.c"), "-o", str(helper)]
    subprocess.run(build_command, check=True)
    log = run / "wine.log"
    print(f"Calling original decoder in isolated prefix: {run}", flush=True)
    command = [str(engine), "-root", str(run / "root"), "-zip", str(wine),
               "-mount_drive", str(work), "d", "-w", "/mnt/drive_d",
               "-env", "WINEDLLOVERRIDES=mscoree,mshtml=", "-env", "WINEDEBUG=-all",
               "/bin/wine", r"D:\original_mkf.exe"]
    with log.open("wb") as output:
        completed = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                   timeout=args.timeout)
    # Boxedwine can return 1 after successful Wine shutdown. Trust only the
    # helper's final marker together with a complete, size-checked output set.
    marker = work / "complete.txt"
    if completed.returncode not in (0, 1) or not marker.exists() or marker.read_text().strip() != "original-mkf-v2-self-checked":
        raise SystemExit(f"Original decoder did not complete; inspect {log}")
    if (work / "decoded/data.mkf/000000.bin").read_bytes()[:4] != b"SMP\0":
        raise SystemExit("Original decoder failed independent known-resource signature check")
    result = {"schema": SCHEMA, "self_checks_passed": True,
              "self_checks": {"abc_eos": True, "known_smp_signature": True},
              "oracle": "original-rich4-v3.11-machine-code",
              "exe_sha256": EXPECTED_EXE, "function_va": "0x455040",
              "image_base": "0x400000", "abi": "cdecl(void *dst, const void *src)",
              "scope": "raw decoded MKF payloads before pixel conversion and pointer relocation",
              "helper_sha256": sha256(helper),
              "helper_source_sha256": sha256(adapter_source / "original_mkf.c"),
              "helper_sources": {p.name: sha256(p) for p in sorted(adapter_source.iterdir())},
              "compiler": compiler, "optimization": "-O0", "build_command": build_command,
              "archives": [], "entries": []}
    for name in REQUIRED:
        source = work / "input" / name
        data = source.read_bytes()
        table = struct.unpack_from("<I", data)[0]
        count = (len(data) - table) // 4
        result["archives"].append({"name": name, "sha256": sha256(source), "entries": count})
        for index in range(count):
            offset = struct.unpack_from("<I", data, table + index * 4)[0]
            size, packed, pixel_offset, pixel_size = struct.unpack_from("<4I", data, offset)
            relative = Path(name) / f"{index:06}.bin"
            decoded = work / "decoded" / relative
            if not decoded.is_file() or decoded.stat().st_size != size:
                raise SystemExit(f"Missing/wrong-size original output: {relative}")
            result["entries"].append({"archive": name, "index": index, "size": size,
                                      "packed_size": packed, "pixel_offset": pixel_offset,
                                      "pixel_size": pixel_size, "file": str(relative),
                                      "sha256": sha256(decoded)})
    result["entry_count"] = len(result["entries"])
    result["golden"] = [{"file": str(path.relative_to(work)), "bytes": path.stat().st_size,
                         "sha256": sha256(path)} for path in sorted((work / "golden").iterdir())]
    result["pixel_oracle"] = {"function_va": "0x451801", "format_va": "0x47637c",
                              "formats": [0, 1, 2, 3], "inputs_per_format": 65536,
                              "single_and_paired_paths_agree": True}
    result["blit_oracle"] = {"opaque_va": "0x455b3a", "transparent_va": "0x455c52",
                             "cases": 1344, "source_width": 5, "source_height": 4,
                             "source_pixels": [0 if i % 4 == 0 else 0x101 + i for i in range(20)],
                             "destination_width": 7, "destination_height": 6,
                             "destination_fill": 0x7bde, "draw_area_enabled": False}
    manifest = work / "manifest.json"
    manifest.write_text(json.dumps(result, indent=2) + "\n")
    # Do not publish a capture link until all required golden files and every
    # original MKF index entry have independently passed complete validation.
    validate_capture(work)
    local = REPO / ".local/oracle"
    local.mkdir(parents=True, exist_ok=True)
    latest = local / "latest"
    if latest.is_symlink():
        latest.unlink()
    if latest.exists():
        raise SystemExit(f"Refusing to replace non-symlink: {latest}")
    latest.symlink_to(work, target_is_directory=True)
    print(f"Exported {result['entry_count']} original payloads: {manifest}")
    print(f"Local link: {latest}")
    if args.native_tool:
        compare_native(args.native_tool.expanduser().resolve(), work, game)


if __name__ == "__main__":
    main()
