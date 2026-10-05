#!/usr/bin/env python3
"""Prevent partial native replays from being reported as playable."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from run_native_playability import verify_result


def complete():
    return dict(status="passed", target="native", native_gameplay_verified=True,
                wine_runtime=False, desktop_input=False, video_driver="dummy", audio_driver="dummy",
                reload_bytes_identical=True, reload_random_identical=True, unsupported_events=[], requested_days=30,
                completed_days=30, match_finished=False, loads=1, saves=1, sdl_input_events=100,
                rendered_frames=250, dice_actions=100, movement_steps=400, audio_frames=1000, audio_effects=120)


class GateTests(unittest.TestCase):
    def test_complete_gameplay(self):
        verify_result(complete(), 30)

    def test_every_required_fact_is_checked(self):
        for name in complete():
            if name == "match_finished":
                continue
            with self.subTest(name=name):
                result = complete()
                del result[name]
                with self.assertRaises((RuntimeError, KeyError)):
                    verify_result(result, 30)

    def test_no_skipped_rules_or_desktop_devices(self):
        for name, value in (("unsupported_events", ["shop not implemented"]),
                            ("wine_runtime", True), ("desktop_input", True),
                            ("video_driver", "cocoa"), ("audio_driver", "coreaudio"),
                            ("reload_bytes_identical", False), ("movement_steps", 0),
                            ("completed_days", 29), ("saves", True)):
            with self.subTest(name=name):
                result = complete()
                result[name] = value
                with self.assertRaises(RuntimeError):
                    verify_result(result, 30)

    def test_completed_match_may_end_before_duration(self):
        result = complete()
        result.update(completed_days=20, match_finished=True)
        verify_result(result, 30)


if __name__ == "__main__":
    unittest.main()
