#!/usr/bin/env python3
"""Coverage/integrity regressions using tiny generated MKFs, never game data."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
from oracle_manifest import (EXPECTED_EXE, ORACLE, REQUIRED, REQUIRED_GOLDENS,
                             ManifestError, validate_capture, validate_decoded_files)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def tiny_archive(payloads):
    data = bytearray(4)
    offsets = []
    for payload in payloads:
        offsets.append(len(data))
        data += struct.pack("<4I", len(payload), len(payload), 0, len(payload)) + payload
    table = len(data)
    for offset in offsets:
        data += struct.pack("<I", offset)
    struct.pack_into("<I", data, 0, table)
    return bytes(data)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "capture"
        (self.root / "input").mkdir(parents=True)
        (self.root / "golden").mkdir()
        self.manifest = {
            "schema": 2, "oracle": ORACLE, "exe_sha256": EXPECTED_EXE,
            "self_checks_passed": True,
            "self_checks": {"abc_eos": True, "known_smp_signature": True},
            "archives": [], "entries": [], "golden": [],
        }
        for name in REQUIRED:
            payloads = [b"synthetic " + name.encode(), b""]
            raw = tiny_archive(payloads)
            (self.root / "input" / name).write_bytes(raw)
            (self.root / "decoded" / name).mkdir(parents=True)
            self.manifest["archives"].append({"name": name, "entries": len(payloads), "sha256": digest(raw)})
            for index, payload in enumerate(payloads):
                relative = f"{name}/{index:06}.bin"
                (self.root / "decoded" / relative).write_bytes(payload)
                self.manifest["entries"].append({
                    "archive": name, "index": index, "file": relative,
                    "size": len(payload), "packed_size": len(payload), "pixel_offset": 0,
                    "pixel_size": len(payload), "sha256": digest(payload),
                })
        self.manifest["entry_count"] = len(self.manifest["entries"])
        for relative in sorted(REQUIRED_GOLDENS | {"golden/terrain-000000.bin"}):
            data = (b'{"file":"terrain-000000.bin"}\n' if relative.endswith("terrain.jsonl") else b"synthetic\n")
            (self.root / relative).write_bytes(data)
            self.manifest["golden"].append({"file": relative, "bytes": len(data), "sha256": digest(data)})
        self.write_manifest()

    def write_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest))

    def rejected(self):
        self.write_manifest()
        with self.assertRaises(ManifestError):
            validate_capture(self.root)

    def test_complete_synthetic_capture_and_cli(self):
        checked = validate_capture(self.root)
        self.assertEqual(checked["entry_count"], 14)  # Not bound to the commercial archive count.
        result = subprocess.run([sys.executable, str(SCRIPTS / "oracle_manifest.py"), str(self.root)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("14 entries", result.stdout)

    def test_relocated_source_and_case_insensitive_archive_names(self):
        moved = self.root.parent / "user resources"
        shutil.move(self.root / "input", moved)
        for path in moved.iterdir():
            path.rename(path.with_name(path.name.upper()))
        self.assertEqual(validate_capture(self.root, moved)["entry_count"], 14)

    def test_provenance_and_required_self_checks(self):
        original = copy.deepcopy(self.manifest)
        for key, value in (("schema", 3), ("schema", True), ("oracle", "rewritten-C"),
                           ("exe_sha256", "0" * 64), ("self_checks_passed", False),
                           ("self_checks", {"abc_eos": True}),
                           ("self_checks", {"abc_eos": True, "known_smp_signature": 1})):
            with self.subTest(key=key, value=value):
                self.manifest = copy.deepcopy(original)
                self.manifest[key] = value
                self.rejected()

    def test_empty_or_missing_archive_cannot_pass(self):
        self.manifest["archives"] = []
        self.manifest["entries"] = []
        self.manifest["entry_count"] = 0
        self.rejected()

    def test_duplicate_archive(self):
        self.manifest["archives"][-1] = copy.deepcopy(self.manifest["archives"][0])
        self.rejected()

    def test_consistently_omitted_entry_detected_against_original_index(self):
        deleted = self.manifest["entries"].pop(1)
        self.manifest["archives"][0]["entries"] = 1
        self.manifest["entry_count"] -= 1
        (self.root / "decoded" / deleted["file"]).unlink()
        self.rejected()

    def test_duplicate_or_gapped_entry_indices(self):
        self.manifest["entries"][1] = copy.deepcopy(self.manifest["entries"][0])
        self.rejected()
        self.manifest["entries"][1]["index"] = 2
        self.manifest["entries"][1]["file"] = "data.mkf/000002.bin"
        self.rejected()

    def test_wrong_total_count(self):
        self.manifest["entry_count"] += 1
        self.rejected()

    def test_declared_metadata_must_match_input_header(self):
        self.manifest["entries"][0]["packed_size"] += 1
        self.rejected()

    def test_declared_output_size_and_hash_are_checked(self):
        entry = self.manifest["entries"][0]
        path = self.root / "decoded" / entry["file"]
        data = path.read_bytes()
        path.write_bytes(b"x" * len(data))
        self.rejected()
        path.write_bytes(data + b"x")
        self.rejected()

    def test_actual_decoded_directory_must_match_manifest_exactly(self):
        extra = self.root / "decoded/data.mkf/999999.bin"
        extra.write_bytes(b"unexpected")
        self.rejected()
        extra.unlink()
        (self.root / "decoded" / self.manifest["entries"][0]["file"]).unlink()
        self.rejected()

    def test_omitted_required_golden_even_if_file_remains(self):
        self.manifest["golden"] = [item for item in self.manifest["golden"] if item["file"] != "golden/random.csv"]
        self.rejected()
        (self.root / "golden/random.csv").unlink()
        self.rejected()

    def test_duplicate_golden_or_wrong_declared_size(self):
        self.manifest["golden"].append(copy.deepcopy(self.manifest["golden"][0]))
        self.rejected()
        self.manifest["golden"].pop()
        self.manifest["golden"][0]["bytes"] += 1
        self.rejected()

    def test_changed_golden_hash(self):
        path = self.root / self.manifest["golden"][0]["file"]
        path.write_bytes(b"x" * path.stat().st_size)
        self.rejected()

    def test_missing_terrain_payload_manifest_record(self):
        self.manifest["golden"] = [item for item in self.manifest["golden"] if item["file"] != "golden/terrain-000000.bin"]
        self.rejected()

    def test_terrain_manifest_cannot_be_empty_even_with_updated_hash(self):
        data = b"\n"
        (self.root / "golden/terrain.jsonl").write_bytes(data)
        item = next(item for item in self.manifest["golden"] if item["file"] == "golden/terrain.jsonl")
        item.update(bytes=len(data), sha256=digest(data))
        self.rejected()

    def test_unlisted_golden_file(self):
        (self.root / "golden/unlisted.bin").write_bytes(b"extra")
        self.rejected()

    def test_source_archive_hash_changes(self):
        with (self.root / "input/data.mkf").open("ab") as stream:
            stream.write(b"extra")
        self.rejected()

    def test_bad_present_executable(self):
        (self.root / "input/rich4.exe").write_bytes(b"not an executable")
        self.rejected()

    def test_output_inventory_check_without_hash_comparison(self):
        validate_decoded_files(self.root / "decoded", self.manifest, check_hashes=False)
        (self.root / "decoded" / self.manifest["entries"][0]["file"]).unlink()
        with self.assertRaises(ManifestError):
            validate_decoded_files(self.root / "decoded", self.manifest, check_hashes=False)

    def test_relative_paths_cannot_escape_capture(self):
        self.manifest["golden"][0]["file"] = "golden/../../outside"
        self.rejected()

    def test_cli_failure_is_nonzero(self):
        self.manifest["golden"] = []
        self.write_manifest()
        result = subprocess.run([sys.executable, str(SCRIPTS / "oracle_manifest.py"), str(self.root)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("Oracle validation failed", result.stderr)


if __name__ == "__main__":
    unittest.main()
