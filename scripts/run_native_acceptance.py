#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the native gameplay acceptance matrix with private inputs and outputs.

Natural starts and deliberately synthetic interaction/endgame cases are reported
separately. This checks native flows, not complete original-game equivalence.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import asdict, dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

import run_native_playability as playability

REPO = Path(__file__).resolve().parent.parent
DEFAULT_FIXTURE_BINARY = REPO / "build/native/rich4-native-fixture"
ACTIVE_CARDS = set(range(1, 18)) | set(range(22, 31))
TOOLS = set(range(1, 14))
INVENTORY_FIXTURES = ("cards-01-15", "cards-16-30", "tools-01-13", "cards-facility")
ALL_FIXTURES = (*INVENTORY_FIXTURES, "bankruptcy-victory", "player-trading")


@dataclass(frozen=True)
class Case:
    name: str
    category: str
    days: int
    seed: int
    map_id: int = 0
    fixture: str | None = None
    exercise_card: int = 0
    exercise_tool: int = 0
    exercise_recovery: bool = False
    exercise_trading: bool = False

    @property
    def synthetic(self):
        return self.fixture is not None


def matrix(turns):
    cases = [Case(f"natural-map-{i}", "natural_start", turns, 42 + i * 97, map_id=i)
             for i in range(8)]
    for name, seed in zip(INVENTORY_FIXTURES, (3, 7, 11, 5)):
        cases.append(Case(f"inventory-{name}", "synthetic_inventory", turns, seed, fixture=name))
    cases.append(Case("target-card-13-tool-11", "synthetic_target", 3, 3,
                      fixture="cards-01-15", exercise_card=13, exercise_tool=11))
    for card in (17, 22, 26, 28):
        cases.append(Case(f"target-card-{card}", "synthetic_target", 3, 7,
                          fixture="cards-16-30", exercise_card=card))
    cases.append(Case("bankruptcy-victory", "synthetic_endgame", 3, 3,
                      fixture="bankruptcy-victory"))
    cases.append(Case("invalid-load-batched-cancel", "synthetic_recovery", 3, 3,
                      fixture="cards-01-15", exercise_card=6, exercise_recovery=True))
    cases.append(Case("player-trading", "synthetic_trading", 3, 3,
                      fixture="player-trading", exercise_trading=True))
    return cases


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def file_tree(directory):
    """Hash every ordinary file, not just the game runner's protected extensions."""
    result = {}
    for path in sorted(Path(directory).rglob("*")):
        relative = path.relative_to(directory).as_posix()
        if path.is_symlink():
            entry = {"symlink": os.readlink(path)}
            if path.is_file():
                entry["sha256"] = sha256(path)
            result[relative] = entry
        elif path.is_file():
            result[relative] = {"sha256": sha256(path)}
    return result


def write_summary(output, summary):
    temporary = output / "summary.json.tmp"
    temporary.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(output / "summary.json")


def generate_fixtures(binary, game, output, timeout):
    destination = output / "fixtures"
    command = [str(binary), "--output", str(destination), "--assets", str(game)]
    metadata = {"command": command, "binary_sha256": sha256(binary), "status": "failed"}
    try:
        with (output / "fixture-generation.log").open("w") as log:
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                       timeout=timeout, env={**os.environ, "SDL_VIDEODRIVER": "dummy",
                                                             "SDL_AUDIODRIVER": "dummy"})
        metadata["exit_code"] = completed.returncode
        if completed.returncode != 0:
            raise RuntimeError(f"Fixture generator exited with {completed.returncode}")
        manifests = {}
        for name in ALL_FIXTURES:
            for suffix in (".DAT", ".DAT.rng", ".json"):
                file = destination / (name + suffix)
                if not file.is_file() or not file.stat().st_size:
                    raise RuntimeError(f"Missing generated fixture {file.name}")
            manifest = json.loads((destination / (name + ".json")).read_text())
            if manifest.get("synthetic") is not True or manifest.get("save") != name + ".DAT":
                raise RuntimeError(f"Fixture provenance is invalid: {name}")
            manifests[name] = manifest
        expected = manifests["bankruptcy-victory"].get("expected_endgame", {})
        if expected.get("winner_slot") != 0 or expected.get("bankrupt_slot") != 1:
            raise RuntimeError("Endgame fixture does not describe the expected winner and loser")
        trading = manifests["player-trading"].get("expected_trading", {})
        expected_offer = {"buyer_slot": 0, "seller_slot": 1, "offer_index": 0,
                          "kind": 3, "item": 5, "price": 1000}
        if any(trading.get(key) != value for key, value in expected_offer.items()) or trading.get("post_cancel_buy") is not True:
            raise RuntimeError("Trading fixture does not describe the expected rival offer")
        metadata.update(status="passed", manifests=manifests, files=file_tree(destination))
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        metadata["error"] = str(error)
    return metadata


def ids_used(gameplay, key, maximum):
    values = gameplay.get(key)
    if not isinstance(values, list) or any(type(value) is not int or not 1 <= value <= maximum for value in values):
        raise RuntimeError(f"Invalid executed-action list: {key}")
    return sorted(set(values))


def verify_case(case, code, report, output, binary_hash):
    if type(code) is not int or code != 0 or report.get("status") != "passed":
        raise RuntimeError(f"Gameplay run did not pass: {report.get('error', code)}")
    if report.get("original_files_unchanged") is not True:
        raise RuntimeError("Gameplay run did not preserve original files")
    if report.get("binary_sha256") != binary_hash:
        raise RuntimeError("Gameplay executable changed during the acceptance matrix")
    if report.get("native_gameplay", {}).get("verified") is not True:
        raise RuntimeError("Runner did not verify native gameplay")
    gameplay = report["gameplay"]
    playability.verify_result(gameplay, case.days)
    if gameplay.get("seed") != case.seed or gameplay.get("map") != case.map_id:
        raise RuntimeError("Run used a different map or automation seed")
    cards = ids_used(gameplay, "card_ids_used", 30)
    tools = ids_used(gameplay, "tool_ids_used", 13)
    if case.exercise_card and case.exercise_card not in cards:
        raise RuntimeError(f"Requested card {case.exercise_card} was not used")
    if case.exercise_tool and case.exercise_tool not in tools:
        raise RuntimeError(f"Requested tool {case.exercise_tool} was not used")
    if case.exercise_recovery and not all(gameplay.get(key) is True for key in
                                         ("invalid_load_recovered", "batched_cancel_recovered")):
        raise RuntimeError("Invalid-load or batched-cancel recovery was not verified")
    trading = {key: gameplay.get(key, 0) for key in ("offers_posted", "offers_cancelled", "offers_bought")}
    if case.exercise_trading and any(type(value) is not int or value < 1 for value in trading.values()):
        raise RuntimeError("Player trading needs a posted, cancelled and purchased offer")
    evidence = report.get("evidence_sha256", {})
    for name in ("before.DAT", "after.DAT", "before.ppm", "after.ppm", "events.jsonl"):
        path = output / "game" / name
        if not path.is_file() or not path.stat().st_size or evidence.get(name) != sha256(path):
            raise RuntimeError(f"Missing or changed gameplay evidence: {name}")
    if (output / "game/before.DAT").read_bytes() == (output / "game/after.DAT").read_bytes():
        raise RuntimeError("Gameplay evidence contains no state change")
    victory = None
    if case.category == "synthetic_endgame":
        events = [json.loads(line) for line in (output / "game/events.jsonl").read_text().splitlines() if line.strip()]
        # GameEventKind schema 1: player_bankrupt=9, game_finished=10. A generic
        # game_finished notification is insufficient to prove a human victory.
        bankrupt = any(event.get("event") == 9 and event.get("player") == 1 for event in events)
        wins = [event for event in events if event.get("event") == 10 and event.get("player") == 0
                and event.get("message", "").endswith(("获胜", "獲勝"))]
        if not bankrupt or not wins or gameplay.get("match_finished") is not True:
            raise RuntimeError("Synthetic endgame lacks bankruptcy, winner event or finished state")
        victory = {"bankrupt_slot": 1, "winner_slot": 0, "victory_events": len(wins),
                   "match_finished": True, "reload_bytes_identical": True, "reload_random_identical": True}
    return {"card_ids_used": cards, "tool_ids_used": tools, "victory": victory,
            "completed_days": gameplay["completed_days"], "match_finished": gameplay["match_finished"],
            "reload_bytes_identical": True, "reload_random_identical": True,
            "invalid_load_recovered": gameplay.get("invalid_load_recovered", False),
            "batched_cancel_recovered": gameplay.get("batched_cancel_recovered", False),
            "trading": trading}


def execute_case(case, *, binary, game, output, timeout, binary_hash):
    destination = output / "runs" / case.name
    record = {**asdict(case), "synthetic": case.synthetic, "status": "failed", "output": str(destination)}
    started = time.monotonic()
    try:
        load = None
        initial_input = None
        if case.fixture:
            directory = output / "inputs" / case.name
            directory.mkdir(parents=True, exist_ok=False)
            for suffix in (".DAT", ".DAT.rng", ".json"):
                shutil.copyfile(output / "fixtures" / (case.fixture + suffix), directory / (case.fixture + suffix))
            initial_input = file_tree(directory)
            record["input_files"] = initial_input
            load = directory / (case.fixture + ".DAT")
        record["load"] = str(load) if load else None
        code = playability.run(binary=binary, game=game, turns=case.days, seed=case.seed,
                               map_id=case.map_id, timeout=timeout, load=load, output=destination,
                               exercise_card=case.exercise_card, exercise_tool=case.exercise_tool,
                               exercise_recovery=case.exercise_recovery, exercise_trading=case.exercise_trading)
        record["exit_code"] = code
        report_path = destination / "report.json"
        report = json.loads(report_path.read_text())
        record["report_sha256"] = sha256(report_path)
        record["command"] = report.get("command")
        record["binary_sha256"] = report.get("binary_sha256")
        record["gameplay"] = report.get("gameplay")
        record.update(verify_case(case, code, report, destination, binary_hash))
        command = report.get("command", [])
        if load:
            if command.count("--load") != 1 or command[command.index("--load") + 1] != str(load):
                raise RuntimeError("Synthetic run did not load its private fixture")
            record["fixture_input_unchanged"] = file_tree(load.parent) == initial_input
            if not record["fixture_input_unchanged"]:
                raise RuntimeError("Run modified its input fixture")
        elif "--start-new" not in command or "--load" in command:
            raise RuntimeError("Natural-start run did not create a fresh game")
        record["status"] = "passed"
    except Exception as error:
        record["error"] = f"{type(error).__name__}: {error}"
    record["elapsed_seconds"] = round(time.monotonic() - started, 3)
    return record


def coverage(cases, results):
    passed = [result for result in results if result.get("status") == "passed"]
    cards = set().union(*(set(result.get("card_ids_used", [])) for result in passed))
    tools = set().union(*(set(result.get("tool_ids_used", [])) for result in passed))
    natural = [result for result in passed if result["category"] == "natural_start"]
    synthetic = [result for result in passed if result["synthetic"]]
    victory = any(result.get("victory") for result in passed)
    recovery = any(result.get("exercise_recovery") and result.get("invalid_load_recovered") is True and
                   result.get("batched_cancel_recovered") is True for result in passed)
    trading = any(result.get("exercise_trading") and all(type(value) is int and value >= 1
                  for value in result.get("trading", {}).values()) and len(result.get("trading", {})) == 3
                  for result in passed)
    result = {"runs_required": len(cases), "runs_passed": len(passed),
              "all_runs_passed": len(results) == len(cases) and len(passed) == len(cases),
              "natural_maps_passed": sorted(result["map_id"] for result in natural),
              "synthetic_runs_passed": len(synthetic),
              "active_cards_required": sorted(ACTIVE_CARDS), "active_cards_used": sorted(cards & ACTIVE_CARDS),
              "reactive_cards_observed": sorted(cards - ACTIVE_CARDS), "active_cards_missing": sorted(ACTIVE_CARDS - cards),
              "tools_required": sorted(TOOLS), "tools_used": sorted(tools), "tools_missing": sorted(TOOLS - tools),
              "synthetic_victory_verified": victory, "recovery_verified": recovery, "trading_verified": trading,
              "all_reload_bytes_and_rng_identical": bool(passed) and all(
                  item.get("reload_bytes_identical") is True and item.get("reload_random_identical") is True for item in passed)}
    result["passed"] = (result["all_runs_passed"] and result["natural_maps_passed"] == list(range(8)) and
                        not result["active_cards_missing"] and not result["tools_missing"] and
                        victory and recovery and trading and result["all_reload_bytes_and_rng_identical"])
    return result


def run(*, binary=playability.DEFAULT_BINARY, fixture_binary=DEFAULT_FIXTURE_BINARY,
        game=(Path(__file__).resolve().parents[1] / "game-assets"), output=None, workers=2, turns=30, timeout=180):
    if workers not in (1, 2) or turns < 1 or timeout < 1:
        raise ValueError("workers must be 1 or 2; turns and timeout must be positive")
    binary, fixture_binary, game = (Path(value).expanduser().resolve() for value in (binary, fixture_binary, game))
    if output is None:
        label = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
        output = REPO / "artifacts/native-acceptance" / label
    output = Path(output).expanduser().resolve()
    if output == game or game in output.parents:
        raise ValueError("Acceptance output must be outside the original asset directory")
    output.mkdir(parents=True, exist_ok=False)
    cases = matrix(turns)
    summary = {"schema": 1, "status": "running", "native_acceptance_verified": False,
               "original_full_game_equivalence_verified": False,
               "scope": "Native SDL event-queue flows; natural starts and synthetic scenarios are separate",
               "desktop_input": False, "workers": workers, "requested_natural_days": turns,
               "default_duration_or_longer": turns >= 30, "timeout_per_run_seconds": timeout,
               "binary": str(binary), "fixture_binary": str(fixture_binary), "game": str(game),
               "script_sha256": sha256(__file__), "playability_runner_sha256": sha256(playability.__file__),
               "plan": [{**asdict(case), "synthetic": case.synthetic} for case in cases], "runs": []}
    started = time.monotonic()
    originals = None
    try:
        if not binary.is_file() or not fixture_binary.is_file() or not game.is_dir():
            raise RuntimeError("Native game, fixture generator and original asset directory must exist")
        summary["binary_sha256"] = sha256(binary)
        summary["fixture_binary_sha256"] = sha256(fixture_binary)
        originals = file_tree(game)
        if not originals:
            raise RuntimeError("Original asset directory is empty")
        summary["original_files_before"] = originals
        write_summary(output, summary)
        summary["fixture_generation"] = generate_fixtures(fixture_binary, game, output, timeout)
        if summary["fixture_generation"]["status"] != "passed":
            raise RuntimeError(summary["fixture_generation"].get("error", "Fixture generation failed"))
        (output / "runs").mkdir()
        with ThreadPoolExecutor(max_workers=workers) as executor:
            futures = {executor.submit(execute_case, case, binary=binary, game=game, output=output,
                                       timeout=timeout, binary_hash=summary["binary_sha256"]): case for case in cases}
            finished = {}
            for future in as_completed(futures):
                case = futures[future]
                finished[case.name] = future.result()
                summary["runs"] = [finished[item.name] for item in cases if item.name in finished]
                write_summary(output, summary)
                print(f"Acceptance {len(finished)}/{len(cases)}: {case.name} {finished[case.name]['status']}", flush=True)
        summary["coverage"] = coverage(cases, summary["runs"])
        summary["generated_fixtures_unchanged"] = file_tree(output / "fixtures") == summary["fixture_generation"]["files"]
        summary["binaries_unchanged"] = (sha256(binary) == summary["binary_sha256"] and
                                           sha256(fixture_binary) == summary["fixture_binary_sha256"])
        summary["runners_unchanged"] = (sha256(__file__) == summary["script_sha256"] and
                                          sha256(playability.__file__) == summary["playability_runner_sha256"])
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        summary["error"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            summary["original_files_after"] = file_tree(game) if originals is not None else None
            summary["original_files_unchanged"] = originals is not None and originals == summary["original_files_after"]
        except OSError as error:
            summary["original_files_unchanged"] = False
            summary["integrity_error"] = str(error)
        success = (not summary.get("error") and summary.get("coverage", {}).get("passed") is True and
                   all(summary.get(key) is True for key in ("original_files_unchanged", "generated_fixtures_unchanged",
                                                            "binaries_unchanged", "runners_unchanged")))
        summary["native_acceptance_verified"] = success
        summary["status"] = "passed" if success else "failed"
        summary["elapsed_seconds"] = round(time.monotonic() - started, 3)
        write_summary(output, summary)
        print(f"Acceptance summary: {output / 'summary.json'} ({summary['status']})")
    return 0 if summary["native_acceptance_verified"] else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=playability.DEFAULT_BINARY)
    parser.add_argument("--fixture-binary", type=Path, default=DEFAULT_FIXTURE_BINARY)
    parser.add_argument("--game", type=Path, default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--workers", type=int, choices=(1, 2), default=2)
    parser.add_argument("--turns", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args(argv)
    if args.turns < 1 or args.timeout < 1:
        parser.error("turns and timeout must be positive")
    try:
        return run(**vars(args))
    except (OSError, ValueError) as error:
        print(f"Acceptance could not start: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
