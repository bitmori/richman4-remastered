#!/usr/bin/env python3
"""Compare native holiday rules with the user's verified original x86 instructions.

Requires unicorn 2.1.4. No executable instruction, import, table or result is
patched. All 8401 dates in the original lunar table are captured for eight maps.
The post-2020 safe fallback is deliberately outside original-code comparison.
"""
import argparse
import csv
import datetime
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP
from audit_assets import EXPECTED_EXE

ROOT = Path(__file__).resolve().parent.parent
BASE, STACK, STOP = 0x400000, 0x10080000, 0x10010000


def pack(date):
    return date.year << 16 | date.month << 8 | date.day


class Original:
    def __init__(self, executable):
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(BASE, 0x100000)
        pe = struct.unpack_from('<I', executable, 0x3c)[0]
        count, optional = struct.unpack_from('<H', executable, pe + 6)[0], struct.unpack_from('<H', executable, pe + 20)[0]
        if struct.unpack_from('<I', executable, pe + 24 + 28)[0] != BASE:
            raise RuntimeError('Unexpected original image base')
        for index in range(count):
            offset = pe + 24 + optional + 40 * index
            _, address, size, source = struct.unpack_from('<IIII', executable, offset + 8)
            if size:
                self.cpu.mem_write(BASE + address, executable[source:source + size])
        self.cpu.mem_map(0x10000000, 0x100000)

    def write(self, address, value, fmt='I'):
        self.cpu.mem_write(address, struct.pack('<' + fmt, value))

    def read(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def call(self, address, *args):
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.mem_write(STACK, struct.pack('<' + 'I' * (len(args) + 1), STOP, *args))
        self.cpu.emu_start(address, STOP, count=1000000)
        if self.cpu.reg_read(UC_X86_REG_EIP) != STOP:
            raise RuntimeError(f'Original function {address:#x} did not return')
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def select_map(self, index):
        self.write(0x4991b6, index // 4, 'H')
        self.write(0x4991b8, index % 4, 'H')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, nargs='?', default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument('--native-test', type=Path)
    args = parser.parse_args()
    source = next(p for p in args.game.iterdir() if p.name.lower() == 'rich4.exe')
    executable = source.read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXPECTED_EXE:
        raise SystemExit('Holiday oracle requires the verified original Rich4 v3.11 EXE')
    original = Original(executable)
    original.select_map(0)
    if original.call(0x4523d5, pack(datetime.date(1998, 1, 1))) != 1:
        raise SystemExit('Original holiday self-check failed')
    if original.call(0x4523d5, pack(datetime.date(1998, 1, 3))) != 0:
        raise SystemExit('Original Saturday self-check failed')
    root = ROOT / '.local/holidays-oracle'
    output = root / str(time.time_ns())
    output.mkdir(parents=True)
    epoch = datetime.date(1998, 1, 1)
    with (output / 'holidays.csv').open('w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['index', 'date', 'lunar', 'map', 'stage', 'closed'])
        for index in range(8401):
            date = pack(epoch + datetime.timedelta(days=index))
            lunar = original.read(0x47639c + index * 4)
            for region in range(8):
                original.select_map(region)
                closed = original.call(0x4523d5, date)
                writer.writerow([index, date, lunar, region % 4, region // 4, closed])
            if index % 1000 == 0:
                print(f'Captured {index + 1}/8401 original holiday dates on all eight maps', flush=True)
    loan_rows = 0
    with (output / 'loans.csv').open('w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['date', 'map', 'stage', 'due'])
        for year in range(1998, 2021):
            for month in range(1, 13):
                if year == 2020 and month > 9:
                    continue  # Keep +90 and closure skips within the real table.
                current = pack(datetime.date(year, month, 1))
                for region in range(8):
                    original.select_map(region)
                    original.write(0x497160, current)
                    original.write(0x496b68 + 44, 0)
                    original.call(0x433b7e, 0)
                    due = original.read(0x496b68 + 44)
                    if due > pack(datetime.date(2020, 12, 31)):
                        raise RuntimeError('Loan capture crossed the original lunar-table boundary')
                    writer.writerow([current, region % 4, region // 4, due])
                    loan_rows += 1
    expected = {'holidays.csv': 8401 * 8, 'loans.csv': 2184}
    if loan_rows != expected['loans.csv']:
        raise RuntimeError('Incomplete loan capture')
    manifest = {'schema': 'original-holidays-v1', 'oracle': 'unmodified-original-rich4-v3.11-x86',
                'exe_sha256': EXPECTED_EXE, 'adapter_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'functions': ['0x4523d5', '0x433b7e'], 'instruction_patches': [],
                'lunar_range': ['1998-01-01', '2020-12-31'],
                'self_checks': ['New Year closed', 'Saturday open'],
                'outputs': {name: {'rows': rows, 'sha256': hashlib.sha256((output / name).read_bytes()).hexdigest()}
                            for name, rows in expected.items()}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    latest = root / 'latest'
    if latest.is_symlink():
        latest.unlink()
    if latest.exists():
        raise RuntimeError('Refusing to replace a non-symlink latest')
    latest.symlink_to(output, target_is_directory=True)
    print(f'Original holiday capture: {output}', flush=True)
    if args.native_test:
        subprocess.run([str(args.native_test.resolve()), '--oracle', str(output)], check=True)


if __name__ == '__main__':
    main()
