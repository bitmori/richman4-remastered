#!/usr/bin/env python3
"""Validate fixture saves and write boundaries without copyrighted game data."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

TOOL = None
MAP_START = 0x274B


def synthetic_source():
    """A generated ring with 16 nodes, 12 lands and four facilities."""
    counts = (16, 12, 4, 0, 0)
    strides = (40, 52, 56, 52, 28)
    map_size = 40 + sum((n + 1) * stride for n, stride in zip(counts, strides))
    data = bytearray(MAP_START + map_size + 4 * (0x2718 + map_size))
    def word(offset, value):
        struct.pack_into("<H", data, offset, value)
    def dword(offset, value):
        struct.pack_into("<I", data, offset, value)
    dword(0, 0x26)
    data[4:6] = bytes((15, 6))
    word(6, 1998)
    dword(12, 4)
    dword(0x1B0, 1)
    dword(0x268E, 1)
    dword(0x2747, map_size)
    offset = 40
    offsets = []
    for table, (count, stride) in enumerate(zip(counts, strides)):
        offsets.append(offset)
        dword(MAP_START + table * 8, count)
        dword(MAP_START + table * 8 + 4, offset)
        offset += (count + 1) * stride
    for slot in range(4):
        data[0x10 + slot * 104 + 18] = 1
        data[0x10 + slot * 104 + 21] = 1 if not slot else 2
        data[0x10 + slot * 104 + 19] = slot
    for node in range(1, 17):
        x = 300 + (node - 1) % 4 * 24
        y = 300 + (node - 1) // 4 * 24
        record = MAP_START + offsets[0] + node * 40
        word(record, x)
        word(record + 2, y)
        word(record + 24, node % 16 + 1)
        word(record + 26, (node + 14) % 16 + 1)
        is_land = node <= 12
        identity = node if is_land else node - 12
        word(record + 32, (2000 if is_land else 4000) + identity)
        table = 1 if is_land else 2
        prop = MAP_START + offsets[table] + identity * strides[table]
        word(prop, x)
        word(prop + 2, y)
        data[prop + 4:prop + 9] = b"Place"
    return data


class FixtureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / "original assets"
        self.assets.mkdir()
        self.source = self.assets / 'source "fixture".DAT'
        self.source.write_bytes(synthetic_source())
        self.before = hashlib.sha256(self.source.read_bytes()).digest()
        self.output = self.root / "fixtures"

    def invoke(self, output=None, *extra):
        return subprocess.run([str(TOOL), "--assets", str(self.assets),
                               "--from-save", str(self.source), "--output",
                               str(output or self.output), *extra],
                              capture_output=True, text=True, timeout=30)

    def test_serialized_inventory_matches_manifest_and_recipe(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        recipe = json.loads((Path(__file__).parent / "fixtures/native_interactions.json").read_text())
        for label, cards in recipe["cards_by_fixture"].items():
            with self.subTest(label=label):
                manifest = json.loads((self.output / (label + ".json")).read_text())
                data = (self.output / manifest["save"]).read_bytes()
                self.assertIs(manifest["synthetic"], True)
                self.assertEqual(manifest["source"], str(self.source))
                self.assertEqual(manifest["players"][0]["cards"], cards)
                self.assertEqual(list(data[0x654:0x663]), cards)
                self.assertEqual(list(data[0x690:0x69F]), [3] * 13 + [0, 0])
                self.assertEqual(manifest["players"][0]["tools"], [3] * 13 + [0, 0])
                self.assertEqual(struct.unpack_from("<I", data, 0x2676)[0], 0)
                self.assertEqual(struct.unpack_from("<I", data, 0x1B0)[0], 1)
                self.assertEqual(len({p["node"] for p in manifest["players"]}), 4)
                self.assertGreaterEqual(len(manifest["nearby_targets"]), 8)
                self.assertEqual({p["owner"] for p in manifest["nearby_targets"]}, {0, 1, 2, 3, 4})
                self.assertTrue(any(p["code"] < 4000 and not p["owner"] and not p["level"]
                                    for p in manifest["nearby_targets"]))
                self.assertTrue(any(p["code"] > 4000 and not p["owner"] and not p["level"]
                                    for p in manifest["nearby_targets"]))
                self.assertEqual([(o["id"], o["kind"]) for o in manifest["objects"]],
                                 [(1, 1), (5, 5), (17, 16), (27, 17), (37, 18)])
                self.assertEqual([data[0x204 + i * 24] for i in range(46)],
                                 list(range(1, 15)) + [15] * 2 + [16] * 10 + [17] * 10 + [18] * 10)
                for player in manifest["players"]:
                    slot = player["slot"]
                    self.assertEqual(data[0x10 + slot * 104 + 21], 1 if not slot else 2)
                    self.assertEqual(struct.unpack_from("<H", data, 0x10 + slot * 104 + 12)[0], player["node"])
                    self.assertEqual(struct.unpack_from("<i", data, 0x10 + slot * 104 + 28)[0], player["cash"])
                self.assertEqual((self.output / (label + ".DAT.rng")).read_text(),
                                 f'RICH4-RNG-1 {manifest["seed"]}\n')
        self.assertEqual(hashlib.sha256(self.source.read_bytes()).digest(), self.before)

    def test_repeated_generation_is_deterministic(self):
        self.assertEqual(self.invoke().returncode, 0)
        other = self.root / "second"
        self.assertEqual(self.invoke(other).returncode, 0)
        for path in self.output.iterdir():
            self.assertEqual(path.read_bytes(), (other / path.name).read_bytes())

    def test_bankruptcy_setup_uses_two_players_and_original_single_exit(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = (self.output / "bankruptcy-victory.DAT").read_bytes()
        manifest = json.loads((self.output / "bankruptcy-victory.json").read_text())
        self.assertEqual(struct.unpack_from("<I", data, 12)[0], 2)
        self.assertEqual(len(manifest["players"]), 2)
        self.assertIs(manifest["expected_endgame"]["route_modified"], False)
        self.assertEqual(manifest["players"][1]["cash"], 1)
        self.assertEqual(manifest["players"][1]["bank"], 0)
        self.assertEqual(list(data[0x654:0x672]), [0] * 30)
        self.assertEqual(list(data[0x690:0x6AE]), [0] * 30)
        self.assertEqual(data[0x21F6:0x21F6 + 192], bytes(192))
        for slot in range(2):
            base = 0x10 + slot * 104
            self.assertEqual(struct.unpack_from("<i", data, base + 36)[0], 0)
            self.assertEqual(data[base + 57], 5)
        node_count, node_offset = struct.unpack_from("<II", data, MAP_START)
        original = self.source.read_bytes()
        for index in range(1, node_count + 1):
            record = MAP_START + node_offset + index * 40
            self.assertEqual(data[record:record + 36], original[record:record + 36])
        ai = manifest["players"][1]
        record = MAP_START + node_offset + ai["node"] * 40
        flags = struct.unpack_from("<I", data, record + 36)[0]
        forward = [node for direction, node in enumerate(struct.unpack_from("<4H", data, record + 24))
                   if node and node != ai["last_node"] and not flags & (0x40000000 >> direction)]
        self.assertEqual(forward, [manifest["players"][0]["node"]])

    def test_existing_output_and_original_directory_are_rejected(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("existing file")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertEqual(marker.read_text(), "existing file")
        forbidden = self.assets / "fixtures"
        self.assertNotEqual(self.invoke(forbidden).returncode, 0)
        self.assertFalse(forbidden.exists())
        self.assertEqual(hashlib.sha256(self.source.read_bytes()).digest(), self.before)

    def test_invalid_source_creates_no_output(self):
        self.source.write_bytes(b"invalid save")
        self.assertNotEqual(self.invoke().returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("tool", type=Path)
    args, rest = parser.parse_known_args()
    TOOL = args.tool.resolve()
    unittest.main(argv=[__file__, *rest])
