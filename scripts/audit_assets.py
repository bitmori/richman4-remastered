#!/usr/bin/env python3
"""Read-only audit of the user's Rich4 data; never copies assets into Git."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_EXE = "5a90aee28ee5f7a5c3ba5cb935c9e55751a529c25fcd91208748a66293569550"
REQUIRED = ("data.mkf", "effect.mkf", "panel.mkf", "speaking.mkf", "help.mkf", "jump.mkf", "map.mkf")


def audit(directory):
    files = {p.name.lower(): p for p in directory.iterdir() if p.is_file()}
    result = {"directory": str(directory), "archives": [], "errors": [], "warnings": []}
    exe = files.get("rich4.exe")
    if exe is None:
        result["errors"].append("Missing rich4.exe")
    result["exe_sha256"] = hashlib.sha256(exe.read_bytes()).hexdigest() if exe else None
    result["matches_upstream_executable"] = result["exe_sha256"] == EXPECTED_EXE
    if not result["matches_upstream_executable"]:
        result["warnings"].append("rich4.exe does not match the version targeted by mytbk/rich4")
    for name in REQUIRED:
        if name not in files:
            result["errors"].append(f"Missing {name}")
            continue
        data = files[name].read_bytes()
        try:
            table = struct.unpack_from("<I", data)[0]
            if not 4 <= table <= len(data) or (len(data) - table) % 4:
                raise ValueError("invalid index table")
            count = (len(data) - table) // 4
            for i in range(count):
                offset = struct.unpack_from("<I", data, table + i * 4)[0]
                if not 4 <= offset <= table - 16:
                    raise ValueError(f"entry {i}: invalid header position")
                size, packed, pixels, pixel_size = struct.unpack_from("<4I", data, offset)
                if offset + 16 + packed > table:
                    raise ValueError(f"entry {i}: payload exceeds archive boundary")
                if pixels + pixel_size > size:
                    raise ValueError(f"entry {i}: pixels exceed decoded size")
            result["archives"].append({"name": name, "entries": count, "bytes": len(data),
                                       "sha256": hashlib.sha256(data).hexdigest()})
        except (ValueError, struct.error) as error:
            result["errors"].append(f"{name}: {error}")
    videos = [p for p in files.values() if p.suffix.lower() == ".avi"]
    groups = {}
    for video in videos:
        digest = hashlib.sha256(video.read_bytes()).hexdigest()
        groups.setdefault(digest, []).append(video.name)
    result["video_count"] = len(videos)
    result["distinct_video_count"] = len(groups)
    result["duplicate_videos"] = [names for names in groups.values() if len(names) > 1]
    if len(videos) > 1 and len(groups) == 1:
        result["warnings"].append("All AVI files are identical; original cutscenes are missing")
    result["scope"] = "Archive boundaries only; not proof of decoded contents or full gameplay"
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path, nargs="?", default=(Path(__file__).resolve().parents[1] / "game-assets"))
    args = parser.parse_args()
    result = audit(args.directory.expanduser().resolve())
    print(json.dumps(result, ensure_ascii=False, indent=2))
    raise SystemExit(bool(result["errors"]))
