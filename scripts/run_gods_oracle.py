#!/usr/bin/env python3
"""Capture original decimal money-spinner outcomes with scheduled stop input.

Unicorn executes original0x43f23e/0x43ef3e and Watcom rand. Drawing/sound/message/
clock adapters replace platform effects only; digit/RNG/stopping rules are not
reimplemented by this adapter. Requires unicorn2.1.4 and the user's verified EXE.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_GDTR, UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS
from audit_assets import EXPECTED_EXE
from run_holidays_oracle import Original, ROOT

UI_STUBS = [0x41d476, 0x41d433, 0x440cac, 0x44ef41, 0x40b93b, 0x457110, 0x44fabc, 0x4542ce, 0x4542e9, 0x454d2c, 0x4563f5, 0x456418, 0x45643d]
SEEDS = [0, 1, 2, 3, 42, 12345, 0x7fffffff, 0x80000000, 0xffffffff, 0x12345678, 0xdeadbeef, 987654321]
STOPS = [0, 1, 9, 10, 11, 39, 40, 41, 100]


class Spinner(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.cpu.mem_write(0x10090000, bytes(8)+bytes.fromhex('ffff0000009acf00ffff00000092cf00'))
        self.cpu.reg_write(UC_X86_REG_GDTR,(0,0x10090000,23,0))
        self.cpu.reg_write(UC_X86_REG_CS,8)
        for segment in (UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_SS): self.cpu.reg_write(segment,16)
        def invalid(cpu,access,address,size,value,data):
            raise RuntimeError(f'Unmapped memory at{address:#x};PC{cpu.reg_read(UC_X86_REG_EIP):#x}')
        self.cpu.hook_add(UC_HOOK_MEM_INVALID,invalid)
        self.cpu.mem_write(0x10002000, struct.pack('<I', 0x10002100))
        for offset, address, arguments in [(0x64, 0x10003000, 20), (0x80, 0x10003010, 8), (0x1c, 0x10003020, 24)]:
            self.write(0x10002100 + offset, address)
            self.cpu.mem_write(address, b'\xc2' + struct.pack('<H', arguments))
        self.write(0x48a0dc, 0x10002000)
        self.write(0x48a0e0, 0x10002000)
        for address in UI_STUBS:
            self.cpu.mem_write(address, b'\xc3')
        # The original CRT rand fetches its state through a thread-context thunk.
        self.cpu.mem_write(0x10004000, b'\xb8' + struct.pack('<I', 0x10004100) + b'\xc3')
        self.write(0x488f4c, 0x10004000)
        self.write(0x46246c, 0x10005000)  # timeGetTime
        self.write(0x46230c, 0x10005010)  # PeekMessageA
        self.cpu.hook_add(UC_HOOK_CODE, self.clock, begin=0x10005000, end=0x10005000)
        self.cpu.hook_add(UC_HOOK_CODE, self.message, begin=0x10005010, end=0x10005010)
        self.time = self.frames = self.stop_frame = 0
        self.payments = []
        self.cpu.hook_add(UC_HOOK_CODE, self.money, begin=0x440706, end=0x440706)
        self.cpu.hook_add(UC_HOOK_CODE, self.payment, begin=0x41d2c6, end=0x41d2c6)

    def return_value(self, value, arguments=0):
        sp = self.cpu.reg_read(UC_X86_REG_ESP)
        target = self.read(sp)
        self.cpu.reg_write(UC_X86_REG_EAX, value)
        self.cpu.reg_write(UC_X86_REG_ESP, sp + 4 + arguments)
        self.cpu.reg_write(UC_X86_REG_EIP, target)

    def clock(self, cpu, address, size, data):
        self.time += 31
        self.return_value(self.time)

    def message(self, cpu, address, size, data):
        self.frames += 1
        if self.stop_frame and self.frames == self.stop_frame:
            output = self.read(cpu.reg_read(UC_X86_REG_ESP) + 4)
            self.write(output + 4, 0x202)  # Original left-button-release message.
            self.return_value(1, 20)
        else:
            self.return_value(0, 20)

    def money(self, cpu, address, size, data):
        kind = self.read(cpu.reg_read(UC_X86_REG_ESP) + 4)
        self.return_value({0: 701, 1: 6789, 4: 500, 5: 1234}[kind])

    def payment(self, cpu, address, size, data):
        sp = cpu.reg_read(UC_X86_REG_ESP)
        self.payments.append([self.read(sp + 4 + i*4) for i in range(4)])
        self.return_value(0)

    def attachment(self, object_id, actor, seed):
        self.payments = []
        self.cpu.mem_write(0x496b68, bytes(4*104))
        self.cpu.mem_write(0x496d08, bytes(46*24))
        self.cpu.mem_write(0x499120, bytes(4*15))
        self.cpu.mem_write(0x49915c, bytes(4*15))
        self.cpu.mem_write(0x499198, bytes([3])*30)
        self.cpu.mem_write(0x497320, bytes([10])*8)
        self.cpu.mem_write(0x10007000, bytes(3*40))
        self.write(0x498e80, 0x10007000)
        self.write(0x10007000+40+36, 0xff010abc)
        self.write(0x499114, 4)
        self.write(0x49910c, 0)
        self.write(0x4990e8, 1)
        self.write(0x46caf8, 0, 'B')
        self.write(0x497159, 0, 'B')
        for slot in range(4):
            p = 0x496b68 + slot*104
            self.write(p+12, 2, 'H')
            self.write(p+21, 1 if slot == 0 else 2, 'B')
            self.write(p+28, 10000)
            self.write(p+17, 1, 'B')
            self.write(p+18, 2, 'B')
            self.write(p+48, 65500, 'H')
            self.write(p+68, 32700, 'H')
            self.write(p+70, (-32700) & 65535, 'H')
            self.write(p+72, 100, 'H')
            self.cpu.mem_write(0x499120+slot*15, bytes([1,3,5,7,9])+bytes(10))
            self.cpu.mem_write(0x49915c+slot*15, bytes(i%3 for i in range(15)))
        for i in range(46):
            self.write(0x496d08+i*24, i+1 if i<14 else 15 if i<16 else 16+(i-16)//10, 'B')
        self.write(0x496d08+(object_id-1)*24+2, 1, 'H')
        self.call(0x456f50, seed)
        self.call(0x40ead7, actor, 1, object_id)
        p = 0x496b68+actor*104
        raw = bytes(self.cpu.mem_read(p,104))
        obj = bytes(self.cpu.mem_read(0x496d08+(object_id-1)*24,24))
        word = lambda b, offset: struct.unpack_from('<H', b, offset)[0]
        dword = lambda b, offset: struct.unpack_from('<I', b, offset)[0]
        result = [object_id, actor, seed, self.read(0x10007000+40+36), self.read(0x10004100+12),
                  raw[63], word(raw,68), word(raw,70), word(raw,72), dword(raw,28), dword(raw,96),
                  word(raw,48), raw[17], raw[18], word(obj,2), obj[4], obj[5]]
        for address, count in [(0x499120+actor*15,15),(0x49915c+actor*15,15),(0x499198,30),(0x497320,8)]:
            result += list(self.cpu.mem_read(address,count))
        result += [len(self.payments)]
        result += sum(self.payments,[]) + [0]*(12-len(self.payments)*4)
        return result

    def run(self, kind, seed, stop_frame):
        self.time = self.frames = 0
        self.stop_frame = stop_frame
        self.cpu.mem_write(0x48c504, bytes(4))
        self.cpu.mem_write(0x475d5c, bytes(3))
        self.write(0x49910c, 0)
        self.write(0x496b68 + 21, 1 if stop_frame else 2, 'B')
        self.write(0x496b68 + 55, 0, 'B')
        self.call(0x456f50, seed)
        result = self.call(0x43f23e, kind & 1)
        return result, self.read(0x10004100 + 12), self.frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, nargs='?', default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument('--native-test', type=Path)
    args = parser.parse_args()
    source = next(p for p in args.game.iterdir() if p.name.lower() == 'rich4.exe')
    executable = source.read_bytes()
    if hashlib.sha256(executable).hexdigest() != EXPECTED_EXE:
        raise SystemExit('God oracle requires the verified original Rich4 v3.11 EXE')
    original = Spinner(executable)
    original.call(0x456f50, 0)
    if original.call(0x456f2d) != 0 or original.read(0x10004100 + 12) != 12345:
        raise SystemExit('Original RNG/thread-state self-check failed')
    root = ROOT / '.local/gods-oracle'
    output = root / str(time.time_ns())
    output.mkdir(parents=True)
    count = 0
    with (output / 'spinner.csv').open('w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['kind', 'seed', 'stop_frame', 'amount', 'random_after', 'frames'])
        for kind in [1, 2, 5, 6]:
            for seed in SEEDS:
                for stop in STOPS:
                    amount, random_after, frames = original.run(kind, seed, stop)
                    writer.writerow([kind, seed, stop, amount, random_after, frames])
                    count += 1
    attach_count = 0
    with (output / 'attach.csv').open('w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(['object','actor','seed','node_flags','rng','god','luck','money','physical','cash','income',
                         'points','traffic','dice','object_node','object_days','object_attached'] +
                        [f'card{i}' for i in range(15)] + [f'tool{i}' for i in range(15)] +
                        [f'card_supply{i}' for i in range(30)] + [f'tool_supply{i}' for i in range(8)] +
                        ['payment_count'] + [f'pay{i}' for i in range(12)])
        for object_id in range(1,17):
            for actor in range(4):
                for seed in [0,1,42,0xdeadbeef]:
                    writer.writerow(original.attachment(object_id,actor,seed))
                    attach_count += 1
    path = output / 'spinner.csv'
    manifest = {'schema': 'original-gods-v1', 'exe_sha256': EXPECTED_EXE,
                'oracle': 'original-rich4-v3.11-x86-with-explicit-platform-adapters',
                'adapter_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'functions': ['0x43f23e', '0x43ef3e', '0x456f2d', '0x456f50'],
                'ui_only_ret_stubs': [hex(address) for address in UI_STUBS],
                'platform_adapters': ['private CRT thread context', 'no-op DirectDraw methods',
                                      '31ms clock increment per call', 'scheduled left-button-release input',
                                      'attachment only: selected money amounts supplied and payment arguments recorded without finance'],
                'self_checks': ['original srand/rand seed0 gives0 and state12345'],
                'outputs': {'spinner.csv': {'rows': count, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()},
                            'attach.csv': {'rows': attach_count, 'sha256': hashlib.sha256((output/'attach.csv').read_bytes()).hexdigest()}}}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    latest = root / 'latest'
    if latest.is_symlink(): latest.unlink()
    if latest.exists(): raise RuntimeError('Refusing to replace a non-symlink latest')
    latest.symlink_to(output, target_is_directory=True)
    print(f'Captured {count} original money-spinner cases: {output}', flush=True)
    if args.native_test:
        subprocess.run([str(args.native_test.resolve()), '--oracle', str(output)], check=True)


if __name__ == '__main__':
    main()
