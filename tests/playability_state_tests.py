#!/usr/bin/env python3
"""Exercise state-readback acceptance boundaries with generated save bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

TOOL = None
MAP_START = 0x274b
LAND_RECORD = 52


def put16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def put32(data, offset, value):
    struct.pack_into("<I", data, offset, value & 0xffffffff)


def synthetic_save():
    # Two synthetic players, two nodes, two ordinary lands, and index-zero
    # records for the other tables. All dates/names/values are generated here.
    counts = (2, 2, 0, 0, 0)
    strides = (40, 52, 56, 52, 28)
    map_size = 40 + sum((count + 1) * stride for count, stride in zip(counts, strides))
    data = bytearray(MAP_START + map_size + 2 * (0x2718 + map_size))
    put32(data, 0, 0x26)
    data[4:6] = bytes((5, 10))
    put16(data, 6, 2026)
    put32(data, 12, 2)
    put32(data, 0x1b0, 1)
    put32(data, 0x268a, 200000)
    put32(data, 0x268e, 1)
    put32(data, 0x2747, map_size)
    offset = 40
    offsets = []
    for table, (count, stride) in enumerate(zip(counts, strides)):
        offsets.append(offset)
        put32(data, MAP_START + table * 8, count)
        put32(data, MAP_START + table * 8 + 4, offset)
        offset += (count + 1) * stride
    for slot in range(2):
        record = 0x10 + slot * 0x68
        put32(data, record, 0xfefefefe)  # Never interpreted as a native pointer.
        put16(data, record + 8, 100 + slot)
        put16(data, record + 10, 200 + slot)
        put16(data, record + 12, slot + 1)
        data[record + 18] = 1
        data[record + 19] = slot
        data[record + 21] = slot + 1
        put32(data, record + 28, 100000)
        put32(data, record + 32, 200000)
    for index in range(1, 3):
        record = MAP_START + offsets[1] + index * LAND_RECORD
        data[record + 4:record + 7] = bytes((0xa4, 0x40 + index, 0))
        for level in range(6):
            put16(data, record + 0x20 + level * 2, 100 + level * 50)
    return data, MAP_START + offsets[1] + LAND_RECORD


class StateReadbackTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.before = self.directory / 'before "quoted".DAT'
        self.after = self.directory / "after.DAT"
        self.data, self.first_land = synthetic_save()
        self.before.write_bytes(self.data)
        self.after.write_bytes(self.data)

    def invoke(self, *extra):
        return subprocess.run([str(TOOL), str(self.before), str(self.after), *map(str, extra)],
                              capture_output=True, text=True, timeout=30)

    def assert_scope(self, report):
        self.assertEqual(report["acceptance_scope"], "captured_save_readback_and_difference")
        self.assertTrue(report["state_readback_passed"])
        for key in ("native_main_loop_available", "native_gameplay_verified", "full_gameplay_verified",
                    "original_turn_rules_verified", "migrated_function_replay_performed"):
            self.assertIs(report[key], False, key)

    def test_unchanged_saves_do_not_claim_playability(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assert_scope(report)
        self.assertEqual(report["changes"]["raw_file_changed_bytes"], 0)
        self.assertEqual(report["changes"]["player_changes"], [])
        self.assertEqual(report["changes"]["land_changes"], [])
        self.assertEqual(report["before"]["path"], str(self.before))
        self.assertTrue(report["before"]["byte_roundtrip_identical"])
        self.assertTrue(report["after"]["byte_roundtrip_identical"])

    def test_observed_fields_are_reported_without_inventing_turn_rules(self):
        changed = bytearray(self.data)
        player = 0x10
        put16(changed, player + 12, 2)
        put32(changed, player + 28, 99000)
        changed[0x654] = 3
        changed[0x690 + 8] = 1
        changed[self.first_land + 0x19] = 1
        changed[self.first_land + 0x1a] = 1
        changed[4] = 6
        put32(changed, 0x2676, 1)
        changed[0x6f2] = 0x97  # Uninterpreted state also survives roundtrip.
        self.after.write_bytes(changed)
        before_hash = hashlib.sha256(self.before.read_bytes()).hexdigest()
        after_hash = hashlib.sha256(self.after.read_bytes()).hexdigest()
        report_path = self.directory / "report.json"
        result = self.invoke("--output", report_path)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(report_path.read_text())
        self.assert_scope(report)
        changes = report["changes"]
        for key in ("players_with_node_changes", "players_with_financial_changes",
                    "players_with_card_changes", "players_with_tool_changes", "land_changes_count"):
            self.assertEqual(changes[key], 1, key)
        self.assertTrue(changes["date_changed"])
        self.assertTrue(changes["current_player_changed"])
        fields = {field["field"]: field for field in changes["player_changes"][0]["fields"]}
        self.assertEqual(fields["cash"], {"field": "cash", "before": 100000, "after": 99000, "delta": -1000})
        self.assertEqual(fields["node"]["after"], 2)
        self.assertEqual(report["after"]["players"][0]["card_count"], 1)
        self.assertEqual(changes["land_changes"][0]["after"]["owner_code"], 1)
        self.assertEqual(changes["land_changes"][0]["after"]["level"], 1)
        self.assertEqual(hashlib.sha256(self.before.read_bytes()).hexdigest(), before_hash)
        self.assertEqual(hashlib.sha256(self.after.read_bytes()).hexdigest(), after_hash)

    def test_native_gameplay_gate_fails_even_when_readback_passes(self):
        result = self.invoke("--require-native-gameplay")
        self.assertEqual(result.returncode, 3, result.stderr)
        report = json.loads(result.stdout)
        self.assert_scope(report)
        self.assertEqual(report["native_gameplay_gate"], "unsupported")

    def test_output_cannot_overwrite_either_input_or_a_hardlink_alias(self):
        alias = self.directory / "save-hardlink.DAT"
        alias.hardlink_to(self.before)
        for target in (self.before, self.after, alias):
            with self.subTest(target=target):
                result = self.invoke("--output", target)
                self.assertEqual(result.returncode, 1)
                self.assertIn("overwrite", result.stderr)
                self.assertEqual(self.before.read_bytes(), self.data)
                self.assertEqual(self.after.read_bytes(), self.data)

    def test_truncated_input_fails_without_success_report(self):
        self.after.write_bytes(self.data[:-1])
        report_path = self.directory / "report.json"
        result = self.invoke("--output", report_path)
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report_path.exists())
        self.assertNotIn('"state_readback_passed":true', result.stdout)
        self.assertEqual(self.before.read_bytes(), self.data)

    def test_map_switch_does_not_compare_unrelated_land_ids(self):
        changed = bytearray(self.data)
        put16(changed, 8, 1)
        changed[self.first_land + 0x19] = 2
        self.after.write_bytes(changed)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        changes = json.loads(result.stdout)["changes"]
        self.assertFalse(changes["land_records_comparable"])
        self.assertEqual(changes["land_changes"], [])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args, test_args = parser.parse_known_args()
    TOOL = args.binary.resolve()
    unittest.main(argv=[__file__, *test_args])
