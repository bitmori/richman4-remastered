#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test acceptance orchestration and rejection gates without launching the game."""
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import run_native_acceptance as acceptance


def complete_gameplay(options):
    return dict(status="passed", target="native", native_gameplay_verified=True,
                wine_runtime=False, desktop_input=False, video_driver="dummy", audio_driver="dummy",
                reload_bytes_identical=True, reload_random_identical=True, unsupported_events=[],
                requested_days=options["turns"], completed_days=options["turns"], match_finished=False,
                loads=1, saves=1, sdl_input_events=100, rendered_frames=250, dice_actions=100,
                movement_steps=400, audio_frames=1000, audio_effects=120,
                seed=options["seed"], map=options["map_id"], card_ids_used=[], tool_ids_used=[],
                invalid_load_recovered=False, batched_cancel_recovered=False,
                offers_posted=0, offers_cancelled=0, offers_bought=0)


class MatrixTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.game = self.root / "original assets"
        self.game.mkdir()
        (self.game / "Map.mkf").write_bytes(b"synthetic test asset")
        (self.game / "notes.txt").write_text("Also protected despite its extension")
        self.binary = self.root / "native game"
        self.binary.write_bytes(b"fake native binary; never executed")
        self.fixture_binary = self.root / "fixture generator"
        self.fixture_binary.write_bytes(b"fake fixture binary; never executed")
        self.output = self.root / "acceptance"
        self.lock = threading.Lock()
        self.calls = []
        self.active = self.maximum_active = 0
        self.barrier = None
        self.modify = None
        self.raise_case = None
        self.fixture_exit = 0
        self.fixture_modify = None

    def generate(self, command, **kwargs):
        self.assertEqual(command[0], str(self.fixture_binary))
        self.assertFalse(Path(command[command.index("--output") + 1]).exists())
        self.assertEqual(kwargs["env"]["SDL_VIDEODRIVER"], "dummy")
        if self.fixture_exit:
            return subprocess.CompletedProcess(command, self.fixture_exit)
        destination = Path(command[command.index("--output") + 1])
        destination.mkdir()
        for name in acceptance.ALL_FIXTURES:
            (destination / (name + ".DAT")).write_bytes(b"Synthetic fixture: " + name.encode())
            (destination / (name + ".DAT.rng")).write_text("RICH4-RNG-1 1380533064\n")
            metadata = dict(schema=1, synthetic=True, save=name + ".DAT")
            if name == "bankruptcy-victory":
                metadata["expected_endgame"] = dict(winner_slot=0, bankrupt_slot=1)
            if name == "player-trading":
                metadata["expected_trading"] = dict(buyer_slot=0, seller_slot=1, offer_index=0,
                                                    kind=3, item=5, price=1000, post_cancel_buy=True)
            if self.fixture_modify:
                self.fixture_modify(name, metadata)
            (destination / (name + ".json")).write_text(json.dumps(metadata))
        return subprocess.CompletedProcess(command, 0)

    def replay(self, **options):
        name = Path(options["output"]).name
        with self.lock:
            self.calls.append(options)
            index = len(self.calls)
            self.active += 1
            self.maximum_active = max(self.maximum_active, self.active)
        try:
            if self.barrier and index <= 2:
                self.barrier.wait(timeout=3)
            if name == self.raise_case:
                raise subprocess.TimeoutExpired("mock native process", options["timeout"])
            destination = Path(options["output"])
            destination.mkdir(parents=True, exist_ok=False)
            evidence = destination / "game"
            evidence.mkdir()
            gameplay = complete_gameplay(options)
            if name == "inventory-cards-01-15":
                gameplay["card_ids_used"] = list(range(1, 16))
            if name == "inventory-cards-16-30":
                gameplay["card_ids_used"] = [16, 17, 21, *range(22, 31)]
            if name == "inventory-tools-01-13":
                gameplay["tool_ids_used"] = list(range(1, 14))
            if options["exercise_card"]:
                gameplay["card_ids_used"] = [options["exercise_card"]]
            if options["exercise_tool"]:
                gameplay["tool_ids_used"] = [options["exercise_tool"]]
            if options["exercise_recovery"]:
                gameplay.update(invalid_load_recovered=True, batched_cancel_recovered=True)
            if options["exercise_trading"]:
                gameplay.update(offers_posted=1, offers_cancelled=1, offers_bought=1)
            events = [{"event": 2, "player": 0, "message": "move"}]
            if name == "bankruptcy-victory":
                gameplay.update(completed_days=0, match_finished=True)
                events = [{"event": 9, "player": 1, "message": "玩家2破产"},
                          {"event": 10, "player": 0, "message": "玩家1获胜"}]
            command = [str(options["binary"]), "--headless"]
            if options["load"]:
                command += ["--load", str(options["load"])]
            else:
                command += ["--start-new"]
            report = dict(status="passed", original_files_unchanged=True,
                          native_gameplay={"verified": True}, binary_sha256=acceptance.sha256(self.binary),
                          command=command, gameplay=gameplay)
            if self.modify:
                self.modify(name, report, events, options)
            (evidence / "before.DAT").write_bytes(b"before state")
            (evidence / "after.DAT").write_bytes(b"after state")
            for image in ("before.ppm", "after.ppm"):
                (evidence / image).write_bytes(b"P6\n1 1\n255\n\x00\x00\x00")
            (evidence / "events.jsonl").write_text("".join(json.dumps(event) + "\n" for event in events))
            report["evidence_sha256"] = {path.name: acceptance.sha256(path) for path in evidence.iterdir()}
            (destination / "report.json").write_text(json.dumps(report))
            return 0
        finally:
            with self.lock:
                self.active -= 1

    def invoke(self, **overrides):
        options = dict(binary=self.binary, fixture_binary=self.fixture_binary, game=self.game,
                       output=self.output, workers=2, turns=30, timeout=7)
        options.update(overrides)
        with patch.object(acceptance.subprocess, "run", side_effect=self.generate), \
             patch.object(acceptance.playability, "run", side_effect=self.replay), redirect_stdout(io.StringIO()):
            code = acceptance.run(**options)
        return code, json.loads((Path(options["output"]) / "summary.json").read_text())

    def test_matrix_has_all_required_categories_and_fresh_inputs(self):
        cases = acceptance.matrix(30)
        self.assertEqual(len(cases), 19)
        self.assertEqual(len({case.name for case in cases}), 19)
        natural = [case for case in cases if not case.synthetic]
        self.assertEqual([case.map_id for case in natural], list(range(8)))
        self.assertEqual(len({case.seed for case in natural}), 8)
        self.assertTrue(all(case.days == 30 and case.fixture is None for case in natural))
        inventory = [case for case in cases if case.category == "synthetic_inventory"]
        self.assertEqual({case.fixture for case in inventory}, set(acceptance.INVENTORY_FIXTURES))
        self.assertTrue(all(case.days == 30 for case in inventory))
        targets = [case for case in cases if case.category == "synthetic_target"]
        self.assertEqual({case.exercise_card for case in targets}, {13, 22, 26, 28})
        self.assertTrue(any(case.exercise_card == 13 and case.exercise_tool == 11 for case in targets))
        self.assertTrue(all(case.days == 3 and case.synthetic for case in cases[12:]))
        self.assertTrue(any(case.exercise_card == 6 and case.exercise_recovery for case in cases))
        self.assertEqual(sum(case.exercise_trading for case in cases), 1)

    def test_complete_matrix_has_two_workers_private_inputs_and_honest_summary(self):
        self.barrier = threading.Barrier(2)
        code, summary = self.invoke()
        self.assertEqual(code, 0)
        self.assertEqual(summary["status"], "passed")
        self.assertTrue(summary["native_acceptance_verified"])
        self.assertFalse(summary["original_full_game_equivalence_verified"])
        self.assertEqual(self.maximum_active, 2)
        self.assertEqual(len(self.calls), 19)
        self.assertEqual(len({str(call["output"]) for call in self.calls}), 19)
        loads = [Path(call["load"]) for call in self.calls if call["load"]]
        self.assertEqual(len(set(loads)), 11)
        self.assertTrue(all(self.output / "inputs" in path.parents for path in loads))
        self.assertTrue(all(item["status"] == "passed" and item["binary_sha256"] == summary["binary_sha256"]
                            for item in summary["runs"]))
        self.assertEqual(summary["coverage"]["active_cards_used"], sorted(acceptance.ACTIVE_CARDS))
        self.assertEqual(summary["coverage"]["tools_used"], list(range(1, 14)))
        self.assertEqual(summary["coverage"]["reactive_cards_observed"], [21])
        self.assertTrue(summary["coverage"]["trading_verified"])
        self.assertTrue(summary["original_files_unchanged"])
        self.assertTrue(summary["generated_fixtures_unchanged"])
        self.assertTrue(summary["binaries_unchanged"])
        self.assertEqual(summary["fixture_generation"]["command"][0], str(self.fixture_binary))

    def test_missing_action_union_fails_even_when_individual_runs_pass(self):
        def remove(name, report, events, options):
            report["gameplay"]["card_ids_used"] = [value for value in report["gameplay"]["card_ids_used"] if value != 30]
            report["gameplay"]["tool_ids_used"] = [value for value in report["gameplay"]["tool_ids_used"] if value != 13]
        self.modify = remove
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertTrue(summary["coverage"]["all_runs_passed"])
        self.assertEqual(summary["coverage"]["active_cards_missing"], [30])
        self.assertEqual(summary["coverage"]["tools_missing"], [13])

    def test_timeout_is_failure_and_remaining_cases_still_finish(self):
        self.raise_case = "natural-map-3"
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertEqual(len(summary["runs"]), 19)
        failed = [item for item in summary["runs"] if item["status"] == "failed"]
        self.assertEqual([item["name"] for item in failed], [self.raise_case])
        self.assertIn("TimeoutExpired", failed[0]["error"])

    def test_generic_game_over_is_not_victory(self):
        def generic(name, report, events, options):
            if name == "bankruptcy-victory":
                events[-1]["message"] = "游戏结束"
        self.modify = generic
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertFalse(summary["coverage"]["synthetic_victory_verified"])

    def test_recovery_and_trading_require_real_booleans_and_integer_counts(self):
        for key in ("invalid_load_recovered", "batched_cancel_recovered", "offers_posted", "offers_cancelled", "offers_bought"):
            with self.subTest(key=key):
                def incomplete(name, report, events, options):
                    if options["exercise_recovery"] or options["exercise_trading"]:
                        report["gameplay"][key] = True if key.startswith("offers_") else 1
                self.modify = incomplete
                code, summary = self.invoke(output=self.root / key)
                self.assertEqual(code, 1)
                self.assertFalse(summary["native_acceptance_verified"])

    def test_original_non_game_extension_mutation_is_detected_globally(self):
        def modify_original(name, report, events, options):
            if name == "natural-map-0":
                (self.game / "notes.txt").write_text("changed unexpectedly")
        self.modify = modify_original
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertFalse(summary["original_files_unchanged"])
        self.assertTrue(summary["coverage"]["all_runs_passed"])

    def test_mutated_private_fixture_fails_without_corrupting_master(self):
        def modify_input(name, report, events, options):
            if name == "inventory-cards-01-15":
                Path(options["load"]).write_bytes(b"changed")
        self.modify = modify_input
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertTrue(summary["generated_fixtures_unchanged"])
        failed = next(item for item in summary["runs"] if item["name"] == "inventory-cards-01-15")
        self.assertFalse(failed["fixture_input_unchanged"])

    def test_changed_evidence_fails_even_when_runner_report_claims_success(self):
        real_replay = self.replay
        def changed_evidence(**options):
            code = real_replay(**options)
            if Path(options["output"]).name == "natural-map-0":
                (Path(options["output"]) / "game/before.ppm").write_bytes(b"changed after report")
            return code
        self.replay = changed_evidence
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        failed = next(item for item in summary["runs"] if item["name"] == "natural-map-0")
        self.assertEqual(failed["status"], "failed")
        self.assertIn("before.ppm", failed["error"])

    def test_failed_or_mislabeled_generation_never_starts_replays(self):
        self.fixture_exit = 2
        code, summary = self.invoke()
        self.assertEqual(code, 1)
        self.assertEqual(self.calls, [])
        self.assertEqual(summary["fixture_generation"]["exit_code"], 2)
        self.fixture_exit = 0
        self.fixture_modify = lambda name, metadata: metadata.update(synthetic=False)
        code, summary = self.invoke(output=self.root / "mislabeled")
        self.assertEqual(code, 1)
        self.assertEqual(self.calls, [])
        self.assertIn("provenance", summary["error"])

    def test_output_boundaries_refuse_overwrite_and_original_asset_children(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("keep")
        with self.assertRaises(FileExistsError):
            self.invoke()
        self.assertEqual(marker.read_text(), "keep")
        with self.assertRaises(ValueError):
            self.invoke(output=self.game / "unwanted")
        self.assertFalse((self.game / "unwanted").exists())
        for options in ({"workers": 3}, {"turns": 0}, {"timeout": 0}):
            with self.assertRaises(ValueError):
                self.invoke(output=self.root / "invalid", **options)
        self.assertFalse((self.root / "invalid").exists())


if __name__ == "__main__":
    unittest.main()
