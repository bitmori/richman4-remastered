#!/usr/bin/env python3
"""Capture original Rich4 tool AI predicates with isolated selection boundaries.

No Windows, Wine, audio, or host windows. Projection, path preview, preferences,
all 13 predicates, and libc rand execute original EXE instructions. The rendered
anchor list is a deterministic input. Selection-buffer refresh is replaced by
EDX=0/RET: its register-dependent ownership corner case is explicitly excluded.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import time

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
from run_stocks_precision_oracle import Original, ROOT, STACK, STOP
from audit_assets import EXPECTED_EXE

NODES, LANDS, FACILITIES = 0x10001000, 0x10002000, 0x10003000
SCAN = [*range(2001, 2009), 4001, 4002, 0x8001, 0x8002, 0x8004, 0x8008]


def configure(original, scenario, seed):
    o = original
    for address, size in [(NODES, 13 * 40), (LANDS, 9 * 52), (FACILITIES, 3 * 56),
                          (0x496b68, 4 * 104), (0x496d08, 46 * 24),
                          (0x496b30, 8), (0x496b60, 8), (0x48be64, 16)]:
        o.clear(address, size)
    for address, value in [(0x498e80, NODES), (0x498e84, LANDS), (0x498e88, FACILITIES),
                           (0x498e9c, 12), (0x498e98, 8), (0x49910c, 0), (0x499114, 4),
                           (0x4990e8, 1 + scenario % 5), (0x499088, scenario % 8),
                           (0x48b2ac, 1184), (0x48b2b0, 1024), (0x474938, 0x10020000),
                           (0x1000400c, seed)]:
        o.write(address, value)
    # Numeric addresses independently checked against original symbolic source.
    o.write(0x498e8c, 2)
    o.write(0x48bae0, 11, "H")
    o.write(0x48bae2, 12, "H")
    for i in range(1, 13):
        base = NODES + i * 40
        o.write(base, 960 + i * 32, 'h')
        o.write(base + 2, 1024, 'h')
        neighbors = [1 if i == 12 else i + 1, 12 if i == 1 else i - 1, 0, 0]
        if scenario % 7 == 0 and i == 8:
            neighbors[2] = 3
        for j, neighbor in enumerate(neighbors):
            o.write(base + 24 + j * 2, neighbor, 'H')
        code = 2000 + i if i <= 8 else 4000 + i - 8 if i <= 10 else 0
        o.write(base + 32, code, 'H')
        flags = 4 if i == 11 else 5 if i == 12 else 0
        if scenario % 11 == 0 and i == 9:
            flags |= 0x40000000
        if scenario % 3 == 0 and i == 8:
            flags |= 1 << 16
        o.write(base + 36, flags)
    for i in range(4):
        base = 0x496b68 + i * 104
        for offset, value in [(8, 1184 + i * 160), (10, 1024), (12, 7), (14, 6), (48, 300)]:
            o.write(base + offset, value, 'H')
        for offset, value in [(17, scenario % 4), (21, 2), (22, 2), (23, scenario % 3),
                              (53, int(scenario % 19 == 0)), (57, int(scenario % 17 == 0)),
                              (63, 7 if scenario % 23 == 0 else 0)]:
            o.write(base + offset, value, 'B')
        o.write(base + 28, 8000 + scenario * 911)
        o.write(base + 32, 5000)
        o.write(base + 70, -1 if scenario % 13 == 0 else 0, 'h')
        o.write(base + 76 + ((i + 1) % 4) * 4, 5 if scenario % 2 == 0 else 0)
    for i in range(1, 9):
        base = LANDS + i * 52
        o.write(base, 960 + i * 32 + (700 if scenario % 9 == 0 else 0), 'H')
        o.write(base + 2, 1024 + (700 if scenario % 9 == 0 else 32), 'H')
        o.cpu.mem_write(base + 4, b'First\0' if i < 5 else b'Second\0')
        o.write(base + 24, int(scenario % 10 == 0 and i == 5), 'B')
        o.write(base + 25, (i + scenario) % 4, 'B')
        o.write(base + 26, (i + scenario) % 6, 'B')
        o.write(base + 28, 1000 + i * 800, 'H')
        o.write(base + 30, 500 + i * 200, 'H')
        for j in range(6):
            o.write(base + 32 + j * 2, 500 + i * 100 + j * 1800, 'H')
    for i in range(1, 3):
        base = FACILITIES + i * 56
        o.write(base, 1248 + i * 32, 'H')
        o.write(base + 2, 1056, 'H')
        o.write(base + 24, (scenario + i) % 5, 'B')
        o.write(base + 25, (scenario + i) % 3, 'B')
        o.write(base + 26, (scenario + i) % 6, 'B')
        o.write(base + 34, 2000, 'H')
        for j in range(6):
            o.write(base + 36 + j * 2, 1000 + j * 1000, 'H')
    if scenario % 3 == 0:
        o.write(0x496d08, [5, 8, 11, 16, 17, 18, 10][(scenario // 3) % 7], 'B')
    if scenario >= 128:
        o.write(0x496b68 + 23, 2, 'B')
        for i in range(1, 9):
            o.write(LANDS + i * 52, 1700 + i * 32, 'H')
            o.write(LANDS + i * 52 + 2, 1724, 'H')
            o.write(LANDS + i * 52 + 25, 2, 'B')
    o.write(0x496b30, int(scenario % 5 == 0), 'B')
    o.write(0x496b61, int(scenario % 6 == 0), 'B')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument('--scenarios', type=int, default=160)
    args = parser.parse_args()
    executable = next(p for p in args.game.iterdir() if p.name.lower() == 'rich4.exe').read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXPECTED_EXE:
        raise SystemExit('Original Rich4 v3.11 EXE required')
    o = Original(executable)
    o.cpu.mem_write(0x10004100, b'\xb8\x00\x40\x00\x10\xc3')
    o.write(0x488f4c, 0x10004100)
    o.cpu.mem_write(0x409b18, b'\x31\xd2\xc3')
    def scan(cpu, address, size, user_data):
        cpu.mem_write(0x48b8c4, struct.pack('<' + 'H' * len(SCAN), *SCAN))
        cpu.reg_write(UC_X86_REG_EAX, len(SCAN))
        cpu.reg_write(UC_X86_REG_EDX, 0)
        stack = cpu.reg_read(UC_X86_REG_ESP)
        cpu.reg_write(UC_X86_REG_EIP, struct.unpack('<I', cpu.mem_read(stack, 4))[0])
        cpu.reg_write(UC_X86_REG_ESP, stack + 4)
    o.cpu.hook_add(UC_HOOK_CODE, scan, begin=0x40a45c, end=0x40a45c)
    output = ROOT / '.local/tools-ai-oracle' / str(time.time_ns())
    output.mkdir(parents=True)
    counts = [0] * 13
    with (output / 'predicates.csv').open('w') as file:
        writer = csv.writer(file)
        writer.writerow(['scenario', 'tool', 'seed', 'used', 'target', 'final_seed'])
        for scenario in range(args.scenarios):
            for tool in range(1, 14):
                seed = 123 + scenario * 971 + tool * 17
                configure(o, scenario, seed)
                o.cpu.reg_write(UC_X86_REG_ESP, STACK)
                o.cpu.mem_write(STACK, struct.pack('<II', STOP, tool))
                o.cpu.emu_start(0x420e9a, STOP, count=40000000)
                if o.cpu.reg_read(UC_X86_REG_EIP) != STOP:
                    raise RuntimeError(f'Predicate timeout scenario={scenario}, tool={tool}')
                used = o.cpu.reg_read(UC_X86_REG_EAX)
                if used not in (0, 1):
                    raise RuntimeError('Invalid predicate result')
                target = o.read(0x48be64) if used and tool not in (1, 5, 6, 12) else 0
                writer.writerow([scenario, tool, seed, used, target, o.read(0x1000400c)])
                counts[tool - 1] += used
            if scenario % 16 == 15:
                file.flush()
                print(f'{scenario + 1}/{args.scenarios} scenarios', flush=True)
    if args.scenarios >= 160 and any(not count for i, count in enumerate(counts) if i != 9):
        raise RuntimeError('Oracle self-check requires acceptance of every enabled tool type')
    manifest = {
        'schema': 1, 'executable_sha256': EXPECTED_EXE, 'rows': args.scenarios * 13,
        'predicate_successes': counts,
        'boundaries': {'0x40a45c': 'fixed ordered rendered-anchor input SCAN',
                       '0x409b18': 'selection-buffer refresh: EDX=0, RET',
                       '0x488f4c': 'isolated CRT thread storage'},
        'original_projection_and_preview': ['0x409ef9', '0x40a0b1', '0x40b221', '0x40b343'],
        'csv_sha256': hashlib.sha256((output / 'predicates.csv').read_bytes()).hexdigest(),
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    latest = output.parent / 'latest'
    if latest.is_symlink():
        latest.unlink()
    latest.symlink_to(output.name)
    print(output, counts)

if __name__ == '__main__':
    main()
