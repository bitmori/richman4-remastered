#!/usr/bin/env python3
"""Exercise the ARM64 SDL game through its real event queue without a desktop window."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

REPO = Path(__file__).resolve().parent.parent
DEFAULT_BINARY = REPO / "build/native/rich4-native.app/Contents/MacOS/rich4-native"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def protected_files(directory):
    return {p.name: sha256(p) for p in directory.iterdir()
            if p.is_file() and p.suffix.lower() in (".dat", ".cfg", ".exe", ".mkf", ".mid")}


def verify_result(result, turns):
    """A state decoder, empty replay or skipped event is never gameplay evidence."""
    required = {"status": "passed", "target": "native", "native_gameplay_verified": True,
                "wine_runtime": False, "desktop_input": False, "video_driver": "dummy",
                "audio_driver": "dummy", "reload_bytes_identical": True,
                "reload_random_identical": True, "unsupported_events": []}
    for name, expected in required.items():
        if name not in result or type(result[name]) is not type(expected) or result[name] != expected:
            raise RuntimeError(f"Native gameplay gate rejected {name}: {result.get(name)!r}")
    if result.get("requested_days") != turns:
        raise RuntimeError("Replay used a different duration")
    if result.get("completed_days", 0) < turns and result.get("match_finished") is not True:
        raise RuntimeError("Replay stopped before its requested duration")
    for name in ("loads", "saves", "sdl_input_events", "rendered_frames", "dice_actions", "movement_steps", "audio_frames", "audio_effects"):
        if type(result.get(name)) is not int or result[name] <= 0:
            raise RuntimeError(f"No actual progress for {name}")


def run(*, binary=DEFAULT_BINARY, game=Path.home() / "Downloads/rich4-original-game", turns=30,
        seed=42, map_id=0, timeout=180, load=None, output=None, exercise_card=0, exercise_tool=0,
        exercise_recovery=False, exercise_trading=False):
    run_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    output = Path(output) if output else REPO / "artifacts/native-playability" / run_id
    output = output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary, game = Path(binary).expanduser().resolve(), Path(game).expanduser().resolve()
    report = {"schema": 1, "run_id": run_id, "target": "native", "status": "failed",
              "desktop_ui": False, "runner_source_sha256": sha256(__file__),
              "native_gameplay": {"available": binary.is_file(), "verified": False}}
    initial = None
    code = 1
    try:
        if not binary.is_file():
            report["status"] = "unavailable"
            report["native_gameplay"]["reason"] = "Native game executable has not been built"
            code = 3
            return code
        initial = protected_files(game)
        report.update(binary=str(binary), binary_sha256=sha256(binary), protected_before=initial)
        app_output = output / "game"
        command = [str(binary), "--assets", str(game), "--headless", "--auto-rounds", str(turns),
                   "--seed", str(seed), "--map", str(map_id), "--output", str(app_output),
                   "--save-dir", str(output / "saves")]
        if load:
            command += ["--load", str(Path(load).expanduser().resolve())]
        else:
            command += ["--start-new"]
        if exercise_card:
            command += ["--exercise-card", str(exercise_card)]
        if exercise_tool:
            command += ["--exercise-tool", str(exercise_tool)]
        if exercise_recovery:
            command += ["--exercise-recovery"]
        if exercise_trading:
            command += ["--exercise-trading"]
        environment = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
        report["command"] = command
        with (output / "application.log").open("w") as log:
            completed = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        report["exit_code"] = completed.returncode
        if (app_output / "report.json").exists():
            report["gameplay"] = json.loads((app_output / "report.json").read_text())
        if completed.returncode != 0:
            raise RuntimeError(f"Native application exited with {completed.returncode}; see application.log and gameplay evidence")
        verify_result(report["gameplay"], turns)
        for requested, key in ((exercise_card, "card_ids_used"), (exercise_tool, "tool_ids_used")):
            if requested and requested not in report["gameplay"].get(key, []):
                raise RuntimeError(f"Requested interaction {key} ID {requested} was not performed")
        if exercise_recovery and not all(report["gameplay"].get(key) is True for key in
                                         ("invalid_load_recovered", "batched_cancel_recovered")):
            raise RuntimeError("Invalid-load and batched-cancel recovery were not verified")
        if exercise_trading:
            for key in ("offers_posted", "offers_cancelled", "offers_bought"):
                if type(report["gameplay"].get(key)) is not int or report["gameplay"][key] < 1:
                    raise RuntimeError(f"Player trading action was not verified: {key}")
        for name in ("before.DAT", "after.DAT", "before.ppm", "after.ppm", "events.jsonl"):
            path = app_output / name
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"Missing native evidence: {name}")
        if (app_output / "before.DAT").read_bytes() == (app_output / "after.DAT").read_bytes():
            raise RuntimeError("Replay did not change the game state")
        report["evidence_sha256"] = {p.name: sha256(p) for p in app_output.iterdir() if p.is_file()}
        report["native_gameplay"]["verified"] = True
        report["status"] = "passed"
        code = 0
    except (OSError, RuntimeError, KeyError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    finally:
        if initial is not None:
            report["protected_after"] = protected_files(game)
            report["original_files_unchanged"] = initial == report["protected_after"]
            if not report["original_files_unchanged"]:
                report["status"] = "failed"
                report["native_gameplay"]["verified"] = False
                report["error"] = "An original game file changed"
                code = 1
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(f"Report: {output / 'report.json'}")
        print(f"Native gameplay: {report['status']}")
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=DEFAULT_BINARY)
    parser.add_argument("--game", type=Path, default=Path.home() / "Downloads/rich4-original-game")
    parser.add_argument("--load", type=Path)
    parser.add_argument("--turns", type=int, default=30)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--map", type=int, choices=range(8), default=0)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--exercise-card", type=int, choices=range(1, 31), default=0)
    parser.add_argument("--exercise-tool", type=int, choices=range(1, 14), default=0)
    parser.add_argument("--exercise-recovery", action="store_true")
    parser.add_argument("--exercise-trading", action="store_true")
    args = parser.parse_args()
    if args.turns < 1 or args.timeout < 1:
        parser.error("turns and timeout must be positive")
    return run(binary=args.binary, game=args.game, turns=args.turns, seed=args.seed, map_id=args.map,
               timeout=args.timeout, load=args.load, output=args.output,
               exercise_card=args.exercise_card, exercise_tool=args.exercise_tool,
               exercise_recovery=args.exercise_recovery, exercise_trading=args.exercise_trading)


if __name__ == "__main__":
    raise SystemExit(main())
