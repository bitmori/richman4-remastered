#!/usr/bin/env python3
"""Reject misleading gameplay evidence without launching Wine or using game data."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import run_playability as runner


RUN_ID = "synthetic-current-run"


def matching_snapshots():
    """Two independently expressed representations of a small game state."""
    decoded = {
        "date": {"year": 2100, "month": 2, "day": 29},
        "map": 2, "stage": 1, "current_player": 0, "player_count": 2,
        "players": [
            {"slot": 0, "node": 52, "last_node": 51, "x": 400, "y": 800,
             "cash": -120, "bank": 100000, "loan": 400, "controller": 1},
            {"slot": 1, "node": 8, "last_node": 7, "x": 120, "y": 900,
             "cash": 32000, "bank": 20000, "loan": 0, "controller": 2},
        ],
    }
    live = {
        "background_music_setting": 0,
        "run_id": RUN_ID, "date": 0x0834021D, "map_id": 2, "stage_id": 1,
        "current_player": 0,
        "players": [
            {"index": 0, "node": 52, "last_node": 51, "x": 400, "y": 800,
             "cash": -120, "bank": 100000, "loan": 400, "controller": 1},
            {"index": 1, "node": 8, "last_node": 7, "x": 120, "y": 900,
             "cash": 32000, "bank": 20000, "loan": 0, "controller": 2},
            # The in-process observer captures all four allocated slots.
            {"index": 2, "controller": 0}, {"index": 3, "controller": 0},
        ],
    }
    return live, decoded


class LiveStateGateTests(unittest.TestCase):
    def assert_rejected(self, live, decoded):
        with self.assertRaises((RuntimeError, ValueError, KeyError, TypeError)):
            runner.verify_live_state(live, decoded, RUN_ID)

    def test_matching_negative_money_and_packed_leap_date(self):
        live, decoded = matching_snapshots()
        self.assertGreaterEqual(runner.verify_live_state(live, decoded, RUN_ID), 20)

    def test_player_order_is_not_used_as_player_identity(self):
        live, decoded = matching_snapshots()
        live["players"].reverse()
        self.assertGreaterEqual(runner.verify_live_state(live, decoded, RUN_ID), 20)

    def test_stale_or_missing_run_id_rejects_matching_state(self):
        for nonce in ("synthetic-old-run", "", None):
            with self.subTest(run_id=nonce):
                live, decoded = matching_snapshots()
                if nonce is None:
                    del live["run_id"]
                else:
                    live["run_id"] = nonce
                self.assert_rejected(live, decoded)

    def test_each_header_field_must_match_live_memory(self):
        for field in ("date", "map_id", "stage_id", "current_player"):
            with self.subTest(field=field):
                live, decoded = matching_snapshots()
                live[field] += 1
                self.assert_rejected(live, decoded)

    def test_each_player_field_must_match_live_memory(self):
        for index in (0, 1):
            for field in ("node", "last_node", "x", "y", "cash", "bank", "loan", "controller"):
                with self.subTest(player=index, field=field):
                    live, decoded = matching_snapshots()
                    live["players"][index][field] += 1
                    self.assert_rejected(live, decoded)

    def test_missing_required_observation_cannot_pass(self):
        live, decoded = matching_snapshots()
        del live["players"][0]["cash"]
        self.assert_rejected(live, decoded)
        live, decoded = matching_snapshots()
        live["players"] = [player for player in live["players"] if player["index"] != 1]
        self.assert_rejected(live, decoded)

    def test_empty_comparison_cannot_pass(self):
        live, decoded = matching_snapshots()
        decoded["players"] = []
        self.assert_rejected(live, decoded)

    def test_duplicate_live_player_cannot_hide_a_conflicting_observation(self):
        live, decoded = matching_snapshots()
        conflicting = copy.deepcopy(live["players"][0])
        conflicting["cash"] = 999999
        live["players"].insert(0, conflicting)
        self.assert_rejected(live, decoded)

    def test_duplicate_decoded_slot_cannot_replace_a_missing_player(self):
        live, decoded = matching_snapshots()
        decoded["players"][1] = copy.deepcopy(decoded["players"][0])
        self.assert_rejected(live, decoded)

    def test_player_count_must_agree_with_decoded_slots(self):
        for count in (0, 1, 3, 5, True):
            with self.subTest(player_count=count):
                live, decoded = matching_snapshots()
                decoded["player_count"] = count
                self.assert_rejected(live, decoded)


def completed_capture(turns=2):
    baseline = {
        "status": "passed", "run_id": RUN_ID, "scope": "original_game_real_process",
        "native_main_loop": False,
        "steps": [{"name": name, "passed": True} for name in (
            "load_save_via_ui", "dice_action_started_observed", "player_movement_observed",
            "other_player_turn_observed", "return_to_human_ready")],
        "facts": {"requested_turns": turns, "completed_turns": turns, "background_music_setting": 0},
        "rounds": [
            {"player": 0, "before_node": 52 + index * 3, "after_node": 55 + index * 3,
             "before_date": 0x07CE0601 + index, "after_date": 0x07CE0602 + index,
             "roll_started": True, "movement": True, "other_player": True}
            for index in range(turns)
        ],
    }
    launcher = {"status": "passed", "run_id": RUN_ID, "game_exit": 0}
    return baseline, launcher


class CompletionGateTests(unittest.TestCase):
    def assert_rejected(self, baseline, launcher, turns=2):
        with self.assertRaises((RuntimeError, ValueError, KeyError, TypeError)):
            runner.verify_completion(baseline, launcher, RUN_ID, turns)

    def test_complete_single_and_multiple_round_evidence(self):
        for turns in (1, 2, 20):
            with self.subTest(turns=turns):
                baseline, launcher = completed_capture(turns)
                runner.verify_completion(baseline, launcher, RUN_ID, turns)

    def test_launcher_failure_overrides_hook_success(self):
        for field, value in (("status", "failed"), ("game_exit", 2), ("game_exit", 0xC0000005)):
            with self.subTest(field=field, value=value):
                baseline, launcher = completed_capture()
                launcher[field] = value
                self.assert_rejected(baseline, launcher)

    def test_failed_hook_cannot_be_overridden_by_launcher(self):
        baseline, launcher = completed_capture()
        baseline["status"] = "failed"
        self.assert_rejected(baseline, launcher)

    def test_both_hook_and_launcher_require_current_nonce(self):
        for report_name in ("baseline", "launcher"):
            for nonce in ("stale-run", None):
                with self.subTest(report=report_name, run_id=nonce):
                    baseline, launcher = completed_capture()
                    report = baseline if report_name == "baseline" else launcher
                    if nonce is None:
                        del report["run_id"]
                    else:
                        report["run_id"] = nonce
                    self.assert_rejected(baseline, launcher)

    def test_missing_duplicate_or_unknown_step_cannot_pass(self):
        for mutation in ("empty", "missing", "duplicate", "unknown"):
            with self.subTest(mutation=mutation):
                baseline, launcher = completed_capture()
                if mutation == "empty":
                    baseline["steps"] = []
                elif mutation == "missing":
                    baseline["steps"].pop()
                elif mutation == "duplicate":
                    baseline["steps"][-1] = copy.deepcopy(baseline["steps"][0])
                else:
                    baseline["steps"][-1]["name"] = "some_unrelated_module_test"
                self.assert_rejected(baseline, launcher)

    def test_all_steps_require_boolean_success(self):
        for index in range(5):
            for value in (False, 1, "true", None):
                with self.subTest(step=index, value=value):
                    baseline, launcher = completed_capture()
                    baseline["steps"][index]["passed"] = value
                    self.assert_rejected(baseline, launcher)

    def test_round_count_must_match_request_and_evidence(self):
        for field, value in (("requested_turns", 1), ("completed_turns", 1), ("completed_turns", 3)):
            with self.subTest(field=field, value=value):
                baseline, launcher = completed_capture()
                baseline["facts"][field] = value
                self.assert_rejected(baseline, launcher)
        for rounds in ([], completed_capture(1)[0]["rounds"], completed_capture(3)[0]["rounds"]):
            with self.subTest(evidence_rounds=len(rounds)):
                baseline, launcher = completed_capture()
                baseline["rounds"] = rounds
                self.assert_rejected(baseline, launcher)

    def test_every_round_needs_its_own_observations(self):
        for index in (0, 1):
            for field in ("roll_started", "movement", "other_player"):
                for value in (False, 1, "true", None):
                    with self.subTest(round=index, field=field, value=value):
                        baseline, launcher = completed_capture()
                        baseline["rounds"][index][field] = value
                        self.assert_rejected(baseline, launcher)

    def test_stationary_date_rejects_claimed_round_completion(self):
        for index in (0, 1):
            with self.subTest(round=index):
                baseline, launcher = completed_capture()
                baseline["rounds"][index]["after_date"] = baseline["rounds"][index]["before_date"]
                self.assert_rejected(baseline, launcher)

    def test_repeated_or_discontinuous_rounds_cannot_count_twice(self):
        baseline, launcher = completed_capture()
        baseline["rounds"][1] = copy.deepcopy(baseline["rounds"][0])
        self.assert_rejected(baseline, launcher)
        for field in ("before_date", "before_node", "player"):
            baseline, launcher = completed_capture()
            baseline["rounds"][1][field] += 1
            self.assert_rejected(baseline, launcher)

    def test_top_level_pass_cannot_replace_missing_details(self):
        for key in ("steps", "facts", "rounds"):
            with self.subTest(missing=key):
                baseline, launcher = completed_capture()
                del baseline[key]
                self.assert_rejected(baseline, launcher)


class NativeAvailabilityGateTests(unittest.TestCase):
    def test_cli_returns_unavailable_without_requiring_or_starting_wine(self):
        with tempfile.TemporaryDirectory() as temporary:
            missing = Path(temporary) / "does-not-exist"
            result = subprocess.run(
                [sys.executable, str(SCRIPTS / "run_playability.py"), "--target", "native",
                 "--game", str(missing), "--prefix", str(missing), "--native-tool", str(missing)],
                capture_output=True, text=True, timeout=15,
            )
        self.assertEqual(result.returncode, 3, result.stdout + result.stderr)
        prefix = "Playability report: "
        reports = [line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)]
        self.assertEqual(len(reports), 1, result.stdout)
        report = json.loads(Path(reports[0]).read_text())
        self.assertEqual(report["target"], "native")
        self.assertEqual(report["status"], "unavailable")
        self.assertIs(report["native_gameplay"]["available"], False)
        self.assertIs(report["native_gameplay"]["verified"], False)
        self.assertIs(report["desktop_ui"], False)
        self.assertNotIn("baseline", report)
        self.assertNotIn("launcher", report)
        self.assertNotIn("command", report)
        self.assertNotIn("run_directory", report)


class PrivateAudioPreferenceTests(unittest.TestCase):
    def test_only_background_music_byte_changes_and_voice_setting_survives(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "RICH4.CFG"
            original = bytes(range(72))
            path.write_bytes(original)
            settings = runner.disable_private_background_music(path)
            expected = bytearray(original)
            expected[2] = 0
            self.assertEqual(path.read_bytes(), expected)
            self.assertEqual(settings["sound_effect_setting"], original[3])
            for malformed in (original[:-1], original + b"x"):
                path.write_bytes(malformed)
                with self.assertRaises(RuntimeError):
                    runner.disable_private_background_music(path)
                self.assertEqual(path.read_bytes(), malformed)

    def test_enabled_or_missing_music_preference_fails_live_and_completion_checks(self):
        for value in (4, None):
            live, decoded = matching_snapshots()
            live["background_music_setting"] = value
            with self.assertRaises(RuntimeError):
                runner.verify_live_state(live, decoded, RUN_ID)
            baseline, launcher = completed_capture()
            baseline["facts"]["background_music_setting"] = value
            with self.assertRaises(RuntimeError):
                runner.verify_completion(baseline, launcher, RUN_ID, 2)


if __name__ == "__main__":
    unittest.main()
