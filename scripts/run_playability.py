#!/usr/bin/env python3
"""Verify native SDL gameplay or the isolated, windowless original Wine baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

from audit_assets import EXPECTED_EXE

REPO = Path(__file__).resolve().parent.parent
RUNTIME = Path.home() / "Library/Containers/org.boxedwine/Data/Library/Application Support/Boxedwine"
EVIDENCE = ("before.DAT", "after.DAT", "before.json", "after.json", "before.bmp", "after.bmp",
            "events.jsonl", "completion.json", "launcher.json", "failure.json", "failure.bmp")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def protected_files(directory):
    return {path.name: sha256(path) for path in directory.iterdir()
            if path.is_file() and path.suffix.lower() in (".dat", ".cfg", ".exe")}


def disable_private_background_music(path):
    """Change only the original BGM preference in a disposable CFG copy."""
    data = bytearray(path.read_bytes())
    if len(data) != 72:
        raise RuntimeError("Expected the original 72-byte RICH4.CFG")
    settings = {"background_music_before": data[2], "sound_effect_setting": data[3],
                "background_music_after": 0, "changed_offset": 2}
    data[2] = 0
    path.write_bytes(data)
    return settings


def verify_live_state(live, decoded, run_id):
    """Compare independently observed game memory with portable save decoding."""
    if live.get("run_id") != run_id:
        raise RuntimeError("Live capture belongs to another run")
    if live.get("background_music_setting") != 0:
        raise RuntimeError("Private background music preference did not take effect")
    date = decoded["date"]
    expected = {"date": date["day"] | date["month"] << 8 | date["year"] << 16,
                "map_id": decoded["map"], "stage_id": decoded["stage"],
                "current_player": decoded["current_player"]}
    checks = 0
    for field, value in expected.items():
        if live[field] != value:
            raise RuntimeError(f"Live/save mismatch: {field}: {live[field]} != {value}")
        checks += 1
    count = decoded["player_count"]
    if type(count) is not int or not 1 <= count <= 4:
        raise RuntimeError("Invalid decoded player count")
    if sorted(player["slot"] for player in decoded["players"]) != list(range(count)):
        raise RuntimeError("Decoded players do not cover the active slots exactly once")
    players = {player["index"]: player for player in live["players"]}
    if len(players) != len(live["players"]) or not set(range(count)).issubset(players):
        raise RuntimeError("Live players are duplicated or missing active slots")
    for player in decoded["players"]:
        observed = players[player["slot"]]
        for field in ("node", "last_node", "x", "y", "cash", "bank", "loan", "controller"):
            if observed[field] != player[field]:
                raise RuntimeError(f"Live/save mismatch: player {player['slot']} {field}")
            checks += 1
    return checks


def verify_completion(baseline, launcher, run_id, turns):
    if (baseline.get("status") != "passed" or baseline.get("run_id") != run_id
            or launcher.get("status") != "passed" or launcher.get("run_id") != run_id
            or launcher.get("game_exit") != 0):
        raise RuntimeError("Original-game operation verification did not pass")
    required_steps = {"load_save_via_ui", "dice_action_started_observed", "player_movement_observed",
                      "other_player_turn_observed", "return_to_human_ready"}
    steps = baseline["steps"]
    if ({step["name"] for step in steps} != required_steps or len(steps) != len(required_steps)
            or not all(step["passed"] is True for step in steps)):
        raise RuntimeError("Incomplete gameplay step evidence")
    facts = baseline["facts"]
    if facts.get("background_music_setting") != 0:
        raise RuntimeError("Background music was enabled in the private test")
    rounds = baseline["rounds"]
    if facts["requested_turns"] != turns or facts["completed_turns"] != turns or len(rounds) != turns:
        raise RuntimeError("Gameplay round count differs from the request")
    if any(not all(record[field] is True for field in ("roll_started", "movement", "other_player"))
           or record["before_date"] == record["after_date"] for record in rounds):
        raise RuntimeError("Incomplete per-round gameplay evidence")
    for index, record in enumerate(rounds):
        if type(record["player"]) is not int or not 0 <= record["player"] < 4:
            raise RuntimeError("Invalid round player")
        if index and (record["before_date"] != rounds[index - 1]["after_date"]
                      or record["before_node"] != rounds[index - 1]["after_node"]
                      or record["player"] != rounds[index - 1]["player"]):
            raise RuntimeError("Round evidence is repeated or discontinuous")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("wine", "native"), required=True)
    parser.add_argument("--game", type=Path, default=RUNTIME / "Rich4/game")
    parser.add_argument("--prefix", type=Path, default=RUNTIME / "Rich4/root")
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--turns", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--native-tool", type=Path, default=REPO / "build/native/rich4-state-check")
    parser.add_argument("--native-game", type=Path, default=REPO / "build/native/rich4-native.app/Contents/MacOS/rich4-native")
    args = parser.parse_args()
    if not 0 <= args.slot <= 5 or not 1 <= args.turns <= 20 or not 30 <= args.timeout <= 600:
        parser.error("slot must be 0..5, turns 1..20, timeout 30..600 seconds; slots 8/9 are reserved for capture")
    if args.target == "native":
        import run_native_playability
        return run_native_playability.run(binary=args.native_game, game=args.game,
                                          turns=args.turns, timeout=args.timeout)
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    artifacts = REPO / "artifacts/playability" / run_id
    artifacts.mkdir(parents=True)
    report = {"schema": 1, "run_id": run_id, "target": args.target, "status": "failed", "desktop_ui": False,
              "runner_source_sha256": sha256(__file__),
              "native_gameplay": {"available": False, "verified": False,
                  "reason": "No native game entrypoint or turn state machine has been implemented"}}
    source = args.game.expanduser().resolve()
    initial = None
    work = None
    exit_code = 1
    try:
        prefix = args.prefix.expanduser().resolve()
        engine = REPO / ".local/boxedwine/Boxedwine.app/Contents/MacOS/Boxedwine"
        wine = RUNTIME / "FileSystems2/TinyCore15Wine11.0.zip"
        native_tool = args.native_tool.expanduser().resolve()
        if not all(path.exists() for path in (source, prefix, engine, wine, native_tool)):
            raise RuntimeError("Missing configured Wine baseline or native state checker; see README.md")
        files = {path.name.lower(): path for path in source.iterdir() if path.is_file()}
        if "rich4.exe" not in files or sha256(files["rich4.exe"]) != EXPECTED_EXE:
            raise RuntimeError("Gameplay hooks require the verified original v3.11 executable")
        save_name = f"save{args.slot}.dat"
        if save_name not in files:
            raise RuntimeError(f"Missing original baseline save: {save_name}")
        initial = protected_files(source)
        run = RUNTIME / "Rich4-playability" / run_id
        work = run / "work"
        run.mkdir(parents=True)
        shutil.copytree(prefix, run / "root", symlinks=True)
        shutil.copytree(source, work)
        for name in EVIDENCE:
            (work / name).unlink(missing_ok=True)
        # The Wine zip's Unix Vulkan loader otherwise panics in Boxedwine when
        # SDL video is disabled. Shadow it in this disposable prefix only.
        vulkan = run / "root/lib/libvulkan.so.1"
        vulkan.parent.mkdir(parents=True, exist_ok=True)
        vulkan.unlink(missing_ok=True)
        vulkan.write_bytes(b"")
        report.update({"run_directory": str(run), "source_exe_sha256": EXPECTED_EXE,
                       "source_save_sha256": sha256(work / files[save_name].name),
                       "native_tool_sha256": sha256(native_tool),
                       "slot": args.slot, "requested_turns": args.turns})
        if "rich4.cfg" not in files:
            raise RuntimeError("Missing baseline RICH4.CFG; run setup_mac.py first")
        # SDL dummy preserves PCM completion timing, but macOS MIDI bypasses
        # SDL. Disable only BGM using the original game's private preference.
        report["private_audio_preferences"] = disable_private_background_music(work / files["rich4.cfg"].name)
        adapter = work / "adapter-source"
        adapter.mkdir()
        for name in ("automation.c", "playability_hook.c"):
            shutil.copy2(REPO / "tools/playability" / name, adapter / name)
        build_commands = [
            ["i686-w64-mingw32-gcc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
             "-mwindows", str(adapter / "automation.c"), "-o", str(work / "automation.exe")],
            ["i686-w64-mingw32-gcc", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror", "-shared",
             str(adapter / "playability_hook.c"), "-o", str(work / "playability_hook.dll"), "-luser32", "-lgdi32"],
        ]
        with (artifacts / "build.log").open("wb") as log:
            for command in build_commands:
                subprocess.run(command, check=True, cwd=REPO, stdout=log, stderr=subprocess.STDOUT)
        report["adapter_sources"] = {path.name: sha256(path) for path in adapter.iterdir()}
        report["build_commands"] = build_commands
        command = [str(engine), "-novideo", "-root", str(run / "root"), "-zip", str(wine),
                   "-mount_drive", str(work), "d", "-w", "/mnt/drive_d",
                   "-resolution", "640x480", "-bpp", "16",
                   "-env", "WINEDLLOVERRIDES=winemenubuilder.exe,mscoree,mshtml,winevulkan,vulkan-1=",
                   "-env", "WINEDEBUG=-all", "-env", "WINE_D3D_CONFIG=renderer=no3d",
                   "-env", "RICH4_TEST_RUN_ID=" + run_id,
                   "/bin/wine", r"D:\automation.exe", "--timeout", str(args.timeout),
                   "--turns", str(args.turns), "--slot", str(args.slot)]
        environment = os.environ.copy()
        host_environment = dict(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                                SDL_RENDER_DRIVER="software", SDL_MAC_BACKGROUND_APP="1")
        environment.update(host_environment)
        report["command"] = command
        report["host_environment"] = host_environment
        print(f"Windowless original-game verification: {run}", flush=True)
        with (artifacts / "wine.log").open("wb") as log:
            result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout + 90)
        report["host_exit_code"] = result.returncode
        completion = work / "completion.json"
        if not completion.is_file():
            raise RuntimeError("Hook did not produce completion.json; inspect wine.log")
        report["baseline"] = json.loads(completion.read_text())
        report["launcher"] = json.loads((work / "launcher.json").read_text())
        if result.returncode not in (0, 1):
            raise RuntimeError("Boxedwine did not shut down normally")
        verify_completion(report["baseline"], report["launcher"], run_id, args.turns)
        for name in ("before.DAT", "after.DAT", "before.json", "after.json", "events.jsonl", "completion.json"):
            path = work / name
            if not path.is_file():
                raise RuntimeError("Missing hook evidence: " + name)
            shutil.copy2(path, artifacts / name)
        checked = subprocess.run([str(native_tool), str(artifacts / "before.DAT"),
                                  str(artifacts / "after.DAT")], capture_output=True, text=True)
        (artifacts / "native-state.json").write_text(checked.stdout)
        (artifacts / "native-state.stderr").write_text(checked.stderr)
        report["native_state_exit_code"] = checked.returncode
        if checked.returncode != 0:
            raise RuntimeError("Native state readback failed; inspect native-state.stderr")
        report["native_state_check"] = json.loads(checked.stdout)
        report["live_save_field_checks"] = sum(verify_live_state(
            json.loads((work / f"{name}.json").read_text()), report["native_state_check"][name], run_id)
            for name in ("before", "after"))
        if protected_files(source) != initial:
            raise RuntimeError("Original source save/config/executable changed during verification")
        report["status"] = "passed"
        print("Wine operation checks passed; native game playability remains unavailable.")
        exit_code = 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, RuntimeError) as error:
        report["error"] = str(error)
        print("Playability verification failed: " + str(error), flush=True)
        exit_code = 1
    finally:
        if work is not None:
            for name in EVIDENCE:
                if (work / name).is_file():
                    shutil.copy2(work / name, artifacts / name)
        if initial is not None:
            report["original_game_unchanged"] = protected_files(source) == initial
            if not report["original_game_unchanged"]:
                report["status"] = "failed"
                report["error"] = "Original source save/config/executable changed during verification"
        (artifacts / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Playability report: " + str(artifacts / "report.json"), flush=True)
        if initial is not None and not report["original_game_unchanged"]:
            exit_code = 1
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
