#!/usr/bin/env python3
"""Run original Rich4 stock AI visits in an isolated x86 instruction oracle.

Reuses the disclosed precise-FPREM adapter. Game scores, Watcom qsort, RNG,
trades, company ownership and quote rounding run original executable bytes.
Only GUI/message calls, market-closure input and CRT thread storage are adapted.
"""
import argparse
import csv
from datetime import date, timedelta
import hashlib
import json
from pathlib import Path
import struct
import time

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW
from run_stocks_precision_oracle import Original, ROOT, STACK, STOP, bits
from audit_assets import EXPECTED_EXE

COMPANIES = 0x10001000

def packed(value):
    return value.year << 16 | value.month << 8 | value.day


def configure(o, scenario, mode, seed, closed):
    for address, length in [(0x496b68, 4 * 104), (0x496980, 12 * 36), (0x4971a0, 4 * 96),
                            (0x497328, 12 * 144 * 4), (COMPANIES, 5 * 52)]:
        o.clear(address, length)
    today = date(1998, 4, 1 + scenario % 28)
    for address, value in [(0x497160, packed(today)), (0x4990e8, 1 + scenario % 4),
                           (0x499084, scenario % 4), (0x499100, scenario % 144),
                           (0x499114, 4), (0x49910c, 0), (0x498e7c, COMPANIES),
                           (0x1000400c, seed)]:
        o.write(address, value)
    o.write(0x10004110, closed)
    for p in range(4):
        base = 0x496b68 + p * 104
        o.write(base + 21, 2, 'B')
        o.write(base + 26, 0 if scenario % 13 == 0 else 80, 'B')
        o.write(base + 28, 10000 + scenario * 17)
        o.write(base + 32, 1000000 + scenario * 103)
        if scenario % 7 != 0:
            o.write(base + 44, packed(today + timedelta(days=5 if scenario % 3 == 0 else 30)))
        if mode == 1 and scenario % 4 == 0:
            o.write(base + 32, 20000)
            o.write(base + 36, 300000)
            o.write(base + 44, packed(today))
    for i in range(12):
        base = 0x496980 + i * 36
        o.write(base + 4, i + 1 if i < 4 else 0, 'H')
        o.write(base + 10, 100 + (scenario * 37 + i * 13) % 1000, 'H')
        o.write(base + 6, 2 if scenario % 17 == i else 0, 'B')
        price = 20 + ((scenario * 7 + i * 11) % 201) / 2
        previous = price + scenario % 3 - 1
        if scenario % 11 == 0 and i == scenario % 12:
            previous, price = 100, 110 if mode == 0 else 90
        for j, value in enumerate([100, previous, price, 0, (scenario + i) % 15 - 7]):
            o.write(base + 12 + j * 4, bits(value))
        quantities = []
        for p in range(4):
            amount = (i + p + scenario) % 5 * 100
            quantities.append(amount)
            o.write(0x4971a0 + p * 96 + i * 8, amount)
            o.write(0x4971a0 + p * 96 + i * 8 + 4, bits(20 + (scenario * 7 + i * 11) % 101 / 2))
        o.write(base + 8, 9000 - sum(quantities), 'H')
        for j in range(144):
            o.write(0x497328 + i * 576 + j * 4,
                    0 if j % 19 == 0 else bits(15 + i * 5 + j % 24 * (scenario % 5 + 1)))
        if i < 4:
            company = COMPANIES + (i + 1) * 52
            o.write(company + 25, i, 'B')
            o.write(company + 36, 200000 + (scenario * 19 + i * 7) % 100 * 10000)
            o.write(company + 40, (scenario * 29 + i * 13) % 81 * 1000 - 40000, 'i')
            o.write(company + 44, (scenario * 13 + i * 31) % 61 * 1000 - 20000, 'i')
            o.write(company + 48, 1000)
            order = [p + 1 for p in sorted(range(4), key=lambda p: -quantities[p]) if quantities[p]]
            o.write(company + 24, order[0], 'B')
            o.cpu.mem_write(company + 28, bytes(order + [0] * (4 - len(order))))
    o.cpu.reg_write(UC_X86_REG_FPCW, 0x037f)


def words(o, trades):
    result = [o.read(0x1000400c), len(trades)]
    for i in range(12):
        result.extend(trades[i] if i < len(trades) else (0, 0))
    result.extend([o.read(0x496b68 + 28), o.read(0x496b68 + 32)])
    for i in range(12):
        result.extend([o.read(0x4971a0 + i * 8), o.read(0x4971a0 + i * 8 + 4),
                       o.read(0x496980 + i * 36 + 8, 'H'), o.read(0x496980 + i * 36 + 10, 'H')])
    for i in range(1, 5):
        result.extend([o.read(COMPANIES + i * 52 + 24, 'B'), o.read(COMPANIES + i * 52 + 28)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path.home() / 'Downloads/rich4-original-game')
    parser.add_argument('--scenarios', type=int, default=512)
    args = parser.parse_args()
    executable = next(p for p in args.game.iterdir() if p.name.lower() == 'rich4.exe').read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXPECTED_EXE:
        raise SystemExit('Expected original Rich4 v3.11 executable')
    o = Original(executable)
    o.cpu.mem_write(0x10004100, b'\xb8\x00\x40\x00\x10\xc3')
    o.write(0x488f4c, 0x10004100)
    o.cpu.mem_write(0x428d01, b'\xa1' + struct.pack('<I', 0x10004110) + b'\xc3')
    for address in [0x41d433, 0x452946, 0x457110, 0x440cac]:
        o.cpu.mem_write(address, b'\x31\xc0\xc3')
    trades = []
    def record(cpu, address, size, user_data):
        stack = cpu.reg_read(UC_X86_REG_ESP)
        player, stock, quantity, flag = struct.unpack('<4I', cpu.mem_read(stack + 4, 16))
        if player != 0 or flag != 1 or len(trades) >= 12:
            raise RuntimeError('Unexpected original trade arguments')
        trades.append((stock + 1, quantity))
    for address in [0x428d2a, 0x428e23]:
        o.cpu.hook_add(UC_HOOK_CODE, record, begin=address, end=address)
    output = ROOT / '.local/stocks-ai-oracle' / str(time.time_ns())
    output.mkdir(parents=True)
    counts = [0, 0, 0]
    with (output / 'visits.csv').open('w') as file:
        writer = csv.writer(file)
        writer.writerow(['scenario', 'mode', 'seed', 'closed'] + [f'word{i}' for i in range(84)])
        for scenario in range(args.scenarios):
            for mode in range(2):
                seed = 93 + scenario * 971 + mode * 71
                closed = int(scenario % 23 == 0)
                configure(o, scenario, mode, seed, closed)
                trades.clear()
                o.cpu.reg_write(UC_X86_REG_ESP, STACK)
                o.cpu.mem_write(STACK, struct.pack('<II', STOP, 0))
                o.cpu.emu_start(0x42bf03 if mode == 0 else 0x42c79f, STOP, count=1000000)
                if o.cpu.reg_read(UC_X86_REG_EIP) != STOP:
                    raise RuntimeError(f'Original visit did not return: {scenario=} {mode=}')
                result = words(o, trades)
                if len(result) != 84:
                    raise RuntimeError('Oracle output schema mismatch')
                writer.writerow([scenario, mode, seed, closed, *result])
                counts[mode] += len(trades)
                counts[2] += int(len(trades) > 1)
            if scenario % 64 == 63:
                file.flush()
                print(f'{scenario + 1}/{args.scenarios}', counts, flush=True)
    if args.scenarios >= 128 and not all(counts):
        raise RuntimeError('Oracle self-check requires purchases, sales and repeated-sale visits')
    manifest = {
        'schema': 1, 'exe_sha256': EXPECTED_EXE, 'visits': args.scenarios * 2,
        'purchases_sales_repeated_visits': counts,
        'fprem_fix': 'Same exact binary-integer FPREM instruction adapter as stock precision oracle',
        'fprem_count': o.fprem_count,
        'adapters': ['0x428d01 market-closure fixture Boolean', '0x488f4c private CRT thread storage',
                     '0x40a4e1 minimap redraw', '0x41d433 player panel', '0x452946 strip display text',
                     '0x457110 format display text', '0x440cac display message'],
        'real_game_instructions': ['0x42bf03 buy AI', '0x42c79f sell AI', '0x457e6c Watcom qsort',
                                   '0x428d2a purchase', '0x428e23 sale', '0x4294d5 company ownership'],
        'sha256': hashlib.sha256((output / 'visits.csv').read_bytes()).hexdigest(),
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    latest = output.parent / 'latest'
    if latest.is_symlink():
        latest.unlink()
    latest.symlink_to(output.name)
    print(output, counts)

if __name__ == '__main__':
    main()
