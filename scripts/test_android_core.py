#!/usr/bin/env python3
"""Build and execute portable C++ tests on an existing ARM64 Android device.

This exercises native libraries/CLI tools, not an Android game application.
No application is installed and no existing game or emulator app is stopped.
"""
import argparse
import hashlib
import json
import shlex
import subprocess
import time
import uuid
from pathlib import Path

from oracle_manifest import validate_capture, validate_decoded_files

REPO = Path(__file__).resolve().parent.parent
MODULES = ("assets", "graphics", "save", "calendar", "direction", "inventory", "economy", "terrain", "random")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, help="Explicit adb target, e.g. emulator-5554")
    parser.add_argument("--sdk", type=Path, default=Path.home() / "Library/Android/sdk")
    parser.add_argument("--ndk", type=Path, help="Installed NDK; defaults to newest version in SDK")
    parser.add_argument("--oracle", type=Path, help="Original EXE capture directory containing manifest.json")
    parser.add_argument("--game", type=Path, help="Also compare every MKF byte against --oracle on Android")
    parser.add_argument("--save", type=Path, action="append", default=[], help="Read-only roundtrip a real save (repeatable)")
    args = parser.parse_args()
    if args.game and not args.oracle:
        parser.error("--game requires --oracle for independent expected outputs")
    sdk = args.sdk.expanduser().resolve()
    adb = sdk / "platform-tools/adb"
    ndk = args.ndk.expanduser().resolve() if args.ndk else max(
        (p for p in (sdk / "ndk").iterdir() if (p / "build/cmake/android.toolchain.cmake").is_file()),
        key=lambda p: tuple(int(part) for part in p.name.split(".")))
    build = REPO / "build/android-arm64"
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    remote = "/data/local/tmp/rich4-core-" + run_id
    local = REPO / "artifacts/android-core" / run_id
    local.mkdir(parents=True)
    report = {"serial": args.serial, "ndk": str(ndk), "remote": remote,
              "tests": [], "asset_entries": 0, "passed": False}
    log = (local / "commands.log").open("w")

    def run(command):
        command = [str(value) for value in command]
        log.write("$ " + shlex.join(command) + "\n")
        log.flush()
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, cwd=REPO, timeout=1800)
        log.write(result.stdout)
        log.flush()
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {shlex.join(command)}\n{result.stdout}")
        return result.stdout

    def device(*command):
        # adb joins shell arguments; quote the complete remote command explicitly.
        return run([adb, "-s", args.serial, "shell", shlex.join(str(value) for value in command)])

    def push(source, destination):
        run([adb, "-s", args.serial, "push", source, destination])

    def execute(binary, *extra):
        output = device(remote + "/" + binary, *extra)
        report["tests"].append({"name": binary, "arguments": list(extra), "output": output,
                                "executable_sha256": sha256(build / binary)})
        print(binary + ": passed", flush=True)

    try:
        oracle = args.oracle.expanduser().resolve() if args.oracle else None
        source_directory = args.game.expanduser().resolve() if args.game else None
        manifest = validate_capture(oracle, source_directory) if oracle else None
        if oracle:
            report["oracle_manifest_sha256"] = sha256(oracle / "manifest.json")
        report["abi"] = device("getprop", "ro.product.cpu.abilist").strip()
        report["api"] = int(device("getprop", "ro.build.version.sdk").strip())
        if "arm64-v8a" not in report["abi"] or report["api"] < 28:
            raise RuntimeError("This validation target must support ARM64 and Android API 28 or newer")
        run(["cmake", "-S", REPO, "-B", build,
             "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
             "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-28", "-DANDROID_STL=c++_static",
             "-DRICH4_BUILD_VIEWER=OFF", "-DRICH4_BUILD_GAME=OFF", "-DRICH4_SANITIZERS=OFF", "-DCMAKE_BUILD_TYPE=Release",
             "-DBUILD_TESTING=ON"])
        run(["cmake", "--build", build, "--parallel", "6"])
        device("mkdir", "-p", remote)
        binaries = [name + "_tests" for name in MODULES]
        binaries += ["rich4-tool", "rich4-graphics-oracle"]
        for binary in binaries:
            if not (build / binary).is_file():
                raise RuntimeError("Missing required test binary: " + binary)
            push(build / binary, remote + "/" + binary)
            device("chmod", "700", remote + "/" + binary)
        for module in MODULES:
            execute(module + "_tests")
        if args.save:
            device("mkdir", "-p", remote + "/saves")
            report["saves"] = []
            for index, path in enumerate(args.save):
                source = path.expanduser().resolve()
                captured = local / f"save-{index}.dat"
                captured.write_bytes(source.read_bytes())
                destination = remote + f"/saves/{index}.dat"
                push(captured, destination)
                execute("save_tests", destination)
                report["saves"].append({"source": str(source), "snapshot": str(captured), "sha256": sha256(captured)})

        if oracle:
            push(oracle / "golden", remote + "/golden")
            execute("rich4-graphics-oracle", remote + "/golden")
            execute("calendar_tests", remote + "/golden/calendar.csv", remote + "/golden/calendar_packed.csv")
            execute("direction_tests", remote + "/golden/direction.csv")
            execute("inventory_tests", remote + "/golden/inventory.txt")
            execute("economy_tests", remote + "/golden/economy.txt")
            execute("terrain_tests", remote + "/golden/terrain.jsonl")
            execute("random_tests", remote + "/golden/random.csv")
            if args.game:
                files = {p.name.lower(): p for p in args.game.expanduser().resolve().iterdir() if p.is_file()}
                local_decoded = local / "decoded"
                local_decoded.mkdir()
                device("mkdir", "-p", remote + "/input", remote + "/decoded")
                for archive in manifest["archives"]:
                    source = files[archive["name"]]
                    if sha256(source) != archive["sha256"]:
                        raise RuntimeError("MKF differs from oracle capture: " + str(source))
                    destination = remote + "/decoded/" + archive["name"]
                    push(source, remote + "/input/" + archive["name"])
                    device(remote + "/rich4-tool", "dump", remote + "/input/" + archive["name"], destination)
                    run([adb, "-s", args.serial, "pull", destination, local_decoded / archive["name"]])
                validate_decoded_files(local_decoded, manifest, check_hashes=False)
                for entry in manifest["entries"]:
                    expected = oracle / "decoded" / entry["file"]
                    if sha256(expected) != entry["sha256"]:
                        raise RuntimeError("Original output changed: " + entry["file"])
                    if expected.read_bytes() != (local_decoded / entry["file"]).read_bytes():
                        raise RuntimeError("Android bytes differ from original: " + entry["file"])
                    report["asset_entries"] += 1
                print(f"MKF resources: {report['asset_entries']} byte-for-byte matches", flush=True)
        report["passed"] = True
    finally:
        # Retain logs and decoded outputs locally for review. The unique remote
        # test directory is deliberately kept for debugging; no app data is touched.
        (local / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        log.close()
        print("Android report: " + str(local / "report.json"), flush=True)


if __name__ == "__main__":
    main()
