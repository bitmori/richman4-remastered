#!/usr/bin/env python3
"""Validate oracle provenance, complete coverage, and every declared local file.

The capture's original input MKFs provide independent index counts and entry
metadata. No fixed commercial resource count or absolute path is assumed.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path, PurePosixPath

from audit_assets import EXPECTED_EXE, REQUIRED

SCHEMA = 2
ORACLE = "original-rich4-v3.11-machine-code"
REQUIRED_GOLDENS = frozenset({
    "golden/blit.jsonl", "golden/spr.jsonl", "golden/calendar.csv",
    "golden/calendar_packed.csv", "golden/direction.csv", "golden/inventory.txt",
    "golden/economy.txt", "golden/random.csv", "golden/terrain.jsonl",
    *(f"golden/pixels-{mode}.bin" for mode in range(4)),
})


class ManifestError(ValueError):
    """An oracle capture is incomplete, inconsistent, or has changed."""


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition, message):
    if not condition:
        raise ManifestError(message)


def integer(value, label, minimum=0, maximum=0xffffffff):
    require(type(value) is int and minimum <= value <= maximum, f"Invalid {label}")
    return value


def digest_value(value, label):
    require(isinstance(value, str) and len(value) == 64 and
            all(character in "0123456789abcdef" for character in value), f"Invalid {label} SHA-256")
    return value


def relative_path(value, label):
    require(isinstance(value, str) and value and "\\" not in value, f"Invalid {label} path")
    path = PurePosixPath(value)
    require(not path.is_absolute() and all(part not in (".", "..") for part in path.parts) and
            path.as_posix() == value, f"Noncanonical {label} path: {value}")
    return value


def checked_file(root, relative):
    root = Path(root).resolve()
    path = root / relative
    require(path.resolve().is_relative_to(root) and path.is_file(), f"Missing or outside capture: {relative}")
    return path


def actual_files(directory):
    directory = Path(directory)
    require(directory.is_dir(), f"Missing capture directory: {directory}")
    return {path.relative_to(directory).as_posix() for path in directory.rglob("*") if path.is_file()}


def check_inventory(actual, expected, label):
    missing, extra = expected - actual, actual - expected
    require(not missing and not extra,
            f"Incomplete {label}: missing={sorted(missing)[:5]}, extra={sorted(extra)[:5]}")


def validate_decoded_files(directory, manifest, *, check_hashes=True):
    """Check all decoded files; callers first validate the manifest structure."""
    expected = {entry["file"] for entry in manifest["entries"]}
    require(expected, "The decoded comparison must not be empty")
    check_inventory(actual_files(directory), expected, "decoded file inventory")
    for entry in manifest["entries"]:
        path = checked_file(directory, entry["file"])
        require(path.stat().st_size == entry["size"], f"Decoded size differs: {entry['file']}")
        if check_hashes:
            require(sha256(path) == entry["sha256"], f"Decoded hash differs: {entry['file']}")


def source_files(directory):
    require(Path(directory).is_dir(), f"Missing original MKF inputs: {directory}")
    files = {}
    for path in Path(directory).iterdir():
        if path.is_file() and path.name.lower() in (*REQUIRED, "rich4.exe"):
            name = path.name.lower()
            require(name not in files, f"Ambiguous source filename: {name}")
            files[name] = path
    require(set(REQUIRED) <= files.keys(), "Original MKF inputs must include all seven archives")
    if "rich4.exe" in files:
        require(sha256(files["rich4.exe"]) == EXPECTED_EXE, "Original EXE hash differs")
    return files


def archive_index(path):
    data = path.read_bytes()
    require(len(data) >= 4, f"Truncated MKF header: {path.name}")
    table = struct.unpack_from("<I", data)[0]
    require(4 <= table <= len(data) and (len(data) - table) % 4 == 0,
            f"Invalid MKF index: {path.name}")
    count = (len(data) - table) // 4
    require(count > 0, f"Empty MKF index: {path.name}")
    entries = []
    for index in range(count):
        offset = struct.unpack_from("<I", data, table + index * 4)[0]
        require(4 <= offset <= table - 16, f"Invalid MKF entry header: {path.name}/{index}")
        size, packed, pixels, pixel_size = struct.unpack_from("<4I", data, offset)
        require(offset + 16 + packed <= table and pixels + pixel_size <= size,
                f"Invalid MKF entry boundary: {path.name}/{index}")
        entries.append((size, packed, pixels, pixel_size))
    return hashlib.sha256(data).hexdigest(), entries


def validate_capture(capture, source_directory=None):
    """Return a fully validated schema-2 manifest or raise ManifestError.

    Source archives default to capture/input. Providing another source directory
    is supported for relocated captures, but its hashes must match the capture.
    """
    capture = Path(capture).resolve()
    try:
        manifest = json.loads((capture / "manifest.json").read_text())
        require(isinstance(manifest, dict), "Manifest must be a JSON object")
        require(type(manifest.get("schema")) is int and manifest["schema"] == SCHEMA,
                "Unsupported oracle manifest schema (expected 2)")
        require(manifest.get("oracle") == ORACLE and manifest.get("exe_sha256") == EXPECTED_EXE,
                "Unexpected original EXE oracle provenance")
        require(manifest.get("self_checks_passed") is True and isinstance(manifest.get("self_checks"), dict) and
                all(manifest["self_checks"].get(key) is True for key in ("abc_eos", "known_smp_signature")),
                "Original EXE self-checks were not completed")

        archives = manifest.get("archives")
        entries = manifest.get("entries")
        golden = manifest.get("golden")
        require(isinstance(archives, list) and isinstance(entries, list) and isinstance(golden, list),
                "Manifest archives, entries and golden must be lists")
        archive_counts, archive_hashes = {}, {}
        for archive in archives:
            name = archive["name"]
            require(name in REQUIRED and name not in archive_counts, f"Unexpected or duplicate archive: {name}")
            archive_counts[name] = integer(archive["entries"], f"{name} entry count", 1)
            archive_hashes[name] = digest_value(archive["sha256"], name)
        require(set(archive_counts) == set(REQUIRED), "Manifest must contain exactly the seven required archives")
        require(integer(manifest.get("entry_count"), "total entry count", 1) == len(entries) == sum(archive_counts.values()),
                "Manifest entry_count does not match all archive counts and records")

        by_archive = {name: {} for name in REQUIRED}
        for entry in entries:
            name = entry["archive"]
            require(name in by_archive, f"Unknown entry archive: {name}")
            index = integer(entry["index"], "entry index")
            require(index < archive_counts[name] and index not in by_archive[name], f"Duplicate/out-of-range entry: {name}/{index}")
            require(entry["file"] == f"{name}/{index:06}.bin", f"Unexpected decoded filename: {entry['file']}")
            for key in ("size", "packed_size", "pixel_offset", "pixel_size"):
                integer(entry[key], f"{name}/{index} {key}")
            require(entry["pixel_offset"] + entry["pixel_size"] <= entry["size"], f"Invalid decoded pixel range: {entry['file']}")
            digest_value(entry["sha256"], entry["file"])
            by_archive[name][index] = entry
        for name, indexed in by_archive.items():
            require(len(indexed) == archive_counts[name], f"Missing indices in {name}")

        files = source_files(source_directory if source_directory is not None else capture / "input")
        for name in REQUIRED:
            source_hash, original_entries = archive_index(files[name])
            require(source_hash == archive_hashes[name], f"Source archive hash differs: {name}")
            require(len(original_entries) == archive_counts[name], f"Manifest omits original MKF entries: {name}")
            for index, fields in enumerate(original_entries):
                entry = by_archive[name][index]
                declared = tuple(entry[key] for key in ("size", "packed_size", "pixel_offset", "pixel_size"))
                require(fields == declared, f"Manifest differs from original MKF entry header: {entry['file']}")
        validate_decoded_files(capture / "decoded", manifest)

        golden_paths = set()
        for item in golden:
            relative = relative_path(item["file"], "golden")
            require(relative.startswith("golden/") and relative not in golden_paths,
                    f"Unexpected/duplicate golden path: {relative}")
            golden_paths.add(relative)
            path = checked_file(capture, relative)
            require(path.stat().st_size == integer(item["bytes"], f"{relative} size", 1), f"Golden size differs: {relative}")
            require(sha256(path) == digest_value(item["sha256"], relative), f"Golden hash differs: {relative}")
        require(REQUIRED_GOLDENS <= golden_paths,
                f"Missing required golden records: {sorted(REQUIRED_GOLDENS - golden_paths)}")
        check_inventory({"golden/" + path for path in actual_files(capture / "golden")}, golden_paths, "golden file inventory")
        terrain_files = set()
        for line in (capture / "golden/terrain.jsonl").read_text().splitlines():
            row = json.loads(line)
            filename = relative_path(row["file"], "terrain")
            require("/" not in filename and filename.startswith("terrain-") and filename.endswith(".bin"),
                    f"Unexpected terrain golden filename: {filename}")
            relative = "golden/" + filename
            require(relative in golden_paths and relative not in terrain_files, f"Missing/duplicate terrain payload: {filename}")
            terrain_files.add(relative)
        require(terrain_files, "Terrain golden corpus must not be empty")
        require(terrain_files == {name for name in golden_paths if name.startswith("golden/terrain-") and name.endswith(".bin")},
                "Terrain golden payloads are not all referenced by terrain.jsonl")
        return manifest
    except ManifestError:
        raise
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        raise ManifestError(f"Invalid oracle capture: {error}") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--source-directory", type=Path, help="Original MKFs, default: CAPTURE/input")
    args = parser.parse_args()
    try:
        manifest = validate_capture(args.capture, args.source_directory)
    except ManifestError as error:
        parser.exit(1, f"Oracle validation failed: {error}\n")
    print(f"Oracle validated: {len(manifest['archives'])} archives, {len(manifest['entries'])} entries, {len(manifest['golden'])} golden files")


if __name__ == "__main__":
    main()
