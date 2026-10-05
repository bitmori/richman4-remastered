#!/usr/bin/env python3
"""Execute original stock instructions with precise FPREM, isolated from Windows/UI.

Requires unicorn==2.1.4. Its QEMU FPREM quotient has a decimal-boundary rounding
bug also present in Boxedwine. Only that instruction is emulated here using exact
binary integers; all game arithmetic, branches, stores and conversions run the
original executable bytes. See docs/migrations/015-stocks.md.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import time

import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP
from unicorn.x86_const import UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FP0
from unicorn.x86_const import UC_X86_REG_GDTR, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS
from audit_assets import EXPECTED_EXE

ROOT = Path(__file__).resolve().parent.parent
BASE, STACK, STOP = 0x400000, 0x10080000, 0x10010000


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def real(bits_value):
    return struct.unpack('<f', struct.pack('<I', bits_value))[0]


class Original:
    def __init__(self, executable):
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(BASE, 0x100000)
        pe = struct.unpack_from('<I', executable, 0x3c)[0]
        sections = struct.unpack_from('<H', executable, pe + 6)[0]
        optional_size = struct.unpack_from('<H', executable, pe + 20)[0]
        for index in range(sections):
            offset = pe + 24 + optional_size + 40 * index
            _, address, size, source = struct.unpack_from('<IIII', executable, offset + 8)
            if size:
                self.cpu.mem_write(BASE + address, executable[source:source + size])
        self.cpu.mem_map(0x10000000, 0x100000)
        self.cpu.mem_write(0x10090000, bytes(8) + bytes.fromhex("ffff0000009acf00ffff00000092cf00"))
        self.cpu.reg_write(UC_X86_REG_GDTR, (0, 0x10090000, 23, 0))
        self.cpu.reg_write(UC_X86_REG_CS, 8)
        for segment in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.cpu.reg_write(segment, 16)
        self.cpu.reg_write(UC_X86_REG_FPCW, 0x037f)
        # Minimap refresh is the only external/UI action in these routines.
        self.cpu.mem_write(0x40a4e1, b'\xc3')
        self.cpu.hook_add(UC_HOOK_CODE, self.fprem, begin=0x458425, end=0x458425)
        self.fprem_count = 0
        self.cpu.hook_add(UC_HOOK_MEM_INVALID, self.invalid)

    def invalid(self, cpu, access, address, size, value, userdata):
        raise RuntimeError(f"Unmapped access at {address:#x}, PC={cpu.reg_read(UC_X86_REG_EIP):#x}")

    def fprem(self, cpu, address, size, userdata):
        if bytes(cpu.mem_read(address, size)) != b'\xd9\xf8':
            raise RuntimeError('Unexpected FPREM opcode')
        status = cpu.reg_read(UC_X86_REG_FPSW)
        top = (status >> 11) & 7
        dividend, dexp = cpu.reg_read(UC_X86_REG_FP0 + top)
        divisor, vexp = cpu.reg_read(UC_X86_REG_FP0 + ((top + 1) & 7))
        # This helper receives only finite, nonnegative deltas and positive ticks.
        if dexp & 0x8000 or vexp & 0x8000 or not divisor:
            raise RuntimeError('FPREM outside tested market domain')
        dexp &= 0x7fff
        vexp &= 0x7fff
        if dexp - vexp >= 64:
            raise RuntimeError('Partial FPREM is outside the market domain')
        common_exp = min(dexp, vexp)
        numerator = dividend << (dexp - common_exp)
        denominator = divisor << (vexp - common_exp)
        quotient, remainder = divmod(numerator, denominator)
        if remainder:
            length = remainder.bit_length()
            if length > 64:
                raise RuntimeError('Unexpected nonexact extended remainder')
            significand = remainder << (64 - length)
            exponent = common_exp + length - 64
        else:
            significand, exponent = 0, 0
        cpu.reg_write(UC_X86_REG_FP0 + top, (significand, exponent))
        status &= ~0x4700  # C0,C1,C2,C3; C2=0 for completed remainder.
        status |= ((quotient & 4) << 6) | ((quotient & 2) << 13) | ((quotient & 1) << 9)
        cpu.reg_write(UC_X86_REG_FPSW, status)
        cpu.reg_write(UC_X86_REG_EIP, address + size)
        self.fprem_count += 1

    def write(self, address, value, fmt='I'):
        self.cpu.mem_write(address, struct.pack('<' + fmt, value))

    def read(self, address, fmt='I'):
        return struct.unpack('<' + fmt, bytes(self.cpu.mem_read(address, struct.calcsize(fmt))))[0]

    def clear(self, address, size):
        self.cpu.mem_write(address, bytes(size))

    def call(self, address, *args):
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.mem_write(STACK, struct.pack('<' + 'I' * (len(args) + 1), STOP, *args))
        self.cpu.emu_start(address, STOP, count=100000)
        if self.cpu.reg_read(UC_X86_REG_EIP) != STOP:
            raise RuntimeError(f'Original function {address:#x} did not return')
        return self.cpu.reg_read(UC_X86_REG_EAX)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', nargs='?', type=Path, default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument('--boxedwine-capture', type=Path, default=ROOT / '.local/stocks-oracle/latest')
    args = parser.parse_args()
    path = next(p for p in args.game.iterdir() if p.name.lower() == 'rich4.exe')
    executable = path.read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXPECTED_EXE:
        raise SystemExit('Expected original Rich4 v3.11 EXE')
    oracle = Original(executable)
    if oracle.call(0x428ec5, bits(15), bits(-10)) != bits(13.55):
        raise SystemExit('Precise FPREM boundary self-check failed')
    output = ROOT / '.local/stocks-oracle' / ('precision-' + str(time.time_ns()))
    output.mkdir()
    outputs, differences = {}, {}
    for name, count in [('prices.csv', 100618), ('sales.csv', 104), ('updates.csv', 2184)]:
        source = args.boxedwine_capture / name
        with source.open() as file:
            reader = csv.reader(file)
            header = next(reader)
            rows = [[int(v) for v in row] for row in reader]
        if len(rows) != count:
            raise SystemExit(f'Incomplete input fixture {name}')
        changed = 0
        with (output / name).open('w') as file:
            writer = csv.writer(file)
            writer.writerow(header)
            for row in rows:
                result = row.copy()
                if name == 'prices.csv':
                    result[2] = oracle.call(0x428ec5, row[0], row[1])
                elif name == 'sales.csv':
                    oracle.clear(0x496980, 12 * 36)
                    oracle.clear(0x4971a0, 4 * 96)
                    oracle.clear(0x496b68, 4 * 104)
                    oracle.clear(0x10001000, 104)
                    oracle.write(0x498e7c, 0x10001000)
                    for player in range(4):
                        oracle.write(0x4971a0 + player * 96, row[4 + player])
                        oracle.write(0x4971a4 + player * 96, bits(21.25))
                        oracle.write(0x496b88 + player * 104, 10000)
                    oracle.write(0x496984, 1, 'H')
                    oracle.write(0x496988, 6600, 'H')
                    oracle.write(0x49698a, 1500, 'H')
                    oracle.write(0x496994, row[3])
                    oracle.write(0x499080, 2000)
                    oracle.write(0x10001000 + 52 + 24, row[9], 'B')
                    oracle.write(0x10001000 + 52 + 28, row[8])
                    oracle.call(0x428e23, row[0], 0, row[1], row[2])
                    bank = oracle.read(0x496b88 + row[0] * 104)
                    fund = oracle.read(0x499080)
                    result[10:] = [(bank - 10000 if row[2] else fund - 2000) & 0xffffffff,
                                   bank, fund, oracle.read(0x4971a0 + row[0] * 96),
                                   oracle.read(0x4971a4 + row[0] * 96), oracle.read(0x496988, 'H'),
                                   oracle.read(0x49698a, 'H'), oracle.read(0x10001000 + 80),
                                   oracle.read(0x10001000 + 76, 'B')]
                else:
                    if row[2] == 0:
                        oracle.clear(0x496980, 12 * 36)
                        oracle.clear(0x497328, 12 * 144 * 4)
                        oracle.write(0x499100, row[0])
                        group = rows[rows.index(row):rows.index(row) + 12]
                        for member in group:
                            index = member[2]
                            oracle.write(0x496987 + index * 36, member[3], 'B')
                            oracle.write(0x496990 + index * 36, member[4])
                            oracle.write(0x496994 + index * 36, bits(123))
                        oracle.call(0x429040, row[1])
                    index = row[2]
                    sample = (row[0] - 1) % 144
                    result[5:] = [oracle.read(0x496994 + index * 36), oracle.read(0x49699c + index * 36),
                                  oracle.read(0x497328 + (index * 144 + sample) * 4)]
                changed += result != row
                writer.writerow(result)
        differences[name] = changed
        outputs[name] = {'rows': len(rows), 'sha256': hashlib.sha256((output / name).read_bytes()).hexdigest()}
        print(name, len(rows), 'differences from Boxedwine', changed, flush=True)
    wealth_rows = []
    state = 1234567
    def random():
        nonlocal state
        state = (state * 1664525 + 1013904223) & 0xffffffff
        return state
    for scenario in range(1024):
        player = scenario % 4
        cash = ((random() % 50000001) - 25000000) & 0xffffffff
        bank = random() % 50000001
        loan = random() % 50000001
        quantities = [random() % 100001 for _ in range(12)]
        prices = [bits(1 + (random() % 999800) / 100) for _ in range(12)]
        if scenario < 32:
            cash, bank, loan = (0x01000000 + scenario - 16) & 0xffffffff, 0, 0
            quantities = [0] * 12
        if 32 <= scenario < 64:
            quantities = [10000000] * 12  # Exercises FISTP integer-indefinite and recovery.
        oracle.clear(0x496b68, 4 * 104)
        oracle.clear(0x4971a0, 4 * 96)
        oracle.write(0x496b84 + player * 104, cash)
        oracle.write(0x496b88 + player * 104, bank)
        oracle.write(0x496b8c + player * 104, loan)
        for index in range(12):
            oracle.write(0x4971a0 + player * 96 + index * 8, quantities[index])
            oracle.write(0x496994 + index * 36, prices[index])
        oracle.clear(0x10002000, 5 * 52)
        oracle.clear(0x10003000, 4 * 56)
        oracle.write(0x498e84, 0x10002000)
        oracle.write(0x498e98, 4)
        oracle.write(0x498e88, 0x10003000)
        oracle.write(0x498e8c, 3)
        for index in range(1, 5):
            address = 0x10002000 + index * 52
            oracle.write(address + 24, index % 2, 'B')
            oracle.write(address + 25, 1 + ((player + index - 1) % 4), 'B')
            oracle.write(address + 26, index + 1, 'B')
            oracle.write(address + 28, 1234 * index, 'H')
            oracle.write(address + 30, 234 * index, 'H')
        for index in range(1, 4):
            address = 0x10003000 + index * 56
            oracle.write(address + 25, 1 + ((player + index - 1) % 4), 'B')
            oracle.write(address + 26, index + 2, 'B')
            oracle.write(address + 34, 4321 * index, 'H')
            oracle.write(address + 36, 321 * index, 'H')
        result = oracle.call(0x4239b9, player)
        wealth_rows.append([scenario, player, cash, bank, loan, result] + quantities + prices)
    wealth = output / 'wealth.csv'
    with wealth.open('w') as file:
        writer = csv.writer(file)
        writer.writerow(['case', 'player', 'cash', 'bank', 'loan', 'wealth'] +
                        [f'quantity{i}' for i in range(12)] + [f'price_bits{i}' for i in range(12)])
        writer.writerows(wealth_rows)
    outputs['wealth.csv'] = {'rows': len(wealth_rows), 'sha256': hashlib.sha256(wealth.read_bytes()).hexdigest()}
    from capture_stock_market import capture
    outputs.update(capture(oracle, output, bits))
    manifest = {'schema': 'original-stocks-precision-v2', 'exe_sha256': EXPECTED_EXE,
                'engine': 'Unicorn ' + unicorn.__version__, 'x87_control_word': '0x037f',
                'fprem_exact_instruction_emulation_count': oracle.fprem_count,
                'fprem_fix': 'Exact binary integer quotient/remainder replaces emulator FPREM at 0x458425 only',
                'ui_stub': '0x40a4e1 minimap redraw returns without drawing',
                'market_adapters': ['CRT thread state returns private guest storage',
                                    'calendar holiday predicate returns fixture Boolean'],
                'differences_from_boxedwine': differences, 'outputs': outputs}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    latest = ROOT / '.local/stocks-oracle/precision-latest'
    if latest.is_symlink():
        latest.unlink()
    latest.symlink_to(output.name)
    print('Wealth cases:', len(wealth_rows))
    print('Evidence:', latest)


if __name__ == '__main__':
    main()
