#!/usr/bin/env python3
"""Execute original NPC movement/status/interaction instructions on synthetic worlds.

Commercial files remain local. The adapter records finance/removal/confinement
calls without implementing them; the compared scope is the calling NPC rules.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ESP
from audit_assets import EXPECTED_EXE
from run_gods_oracle import Spinner, UI_STUBS
from run_holidays_oracle import ROOT

SEEDS = [0, 1, 42, 0xdeadbeef]
STUBS = [0x40b8d8, 0x41d546, 0x452946, 0x450441, 0x45144f, 0x456e11]
NODE, LAND, FACILITY, COMPANY = 0x10020000, 0x10021000, 0x10022000, 0x10023000


class Actors(Spinner):
    def __init__(self, executable):
        super().__init__(executable)
        for address in STUBS:
            self.cpu.mem_write(address, b'\xc3')
        self.boundaries = []
        for address, code, count in [(0x40e14d, 1, 1), (0x43d593, 2, 2), (0x43ec3f, 3, 2)]:
            def hook(cpu, pc, size, data, code=code, count=count):
                sp = cpu.reg_read(UC_X86_REG_ESP)
                args = [self.read(sp+4+i*4) for i in range(count)]
                self.boundaries.append([code, *args, *([0]*(2-count))])
                self.return_value(0)
            self.cpu.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def setup(self, slot, seed):
        self.payments = []; self.boundaries = []
        for address, size in [(0x496b68, 4*104), (0x498e28, 5*16), (0x498ea0, 9*52),
                              (0x496d08, 46*24), (0x499120, 120), (NODE, 6*40),
                              (LAND, 4*52), (FACILITY, 2*56), (COMPANY, 2*52)]:
            self.cpu.mem_write(address, bytes(size))
        self.cpu.mem_write(0x499198, bytes([3])*30)
        self.cpu.mem_write(0x497320, bytes([10])*8)
        self.write(0x499114, 4); self.write(0x49910c, slot); self.write(0x4990e8, 3)
        self.write(0x498e80, NODE); self.write(0x498e84, LAND); self.write(0x498e88, FACILITY); self.write(0x498e7c, COMPANY)
        self.write(0x498e98, 3); self.write(0x498e9c, 1); self.write(0x498e8c, 1)
        self.write(0x4749dc, 0); self.write(0x4749d4, 11)
        for actor in range(4):
            p=0x496b68+actor*104
            self.write(p+21, 2, 'B'); self.write(p+48, 65500+actor, 'H'); self.write(p+32, 10003+actor*101)
            self.cpu.mem_write(0x499120+actor*15, bytes([1,3,5,7,9])+bytes(10))
        for i in range(5):
            self.cpu.mem_write(0x498e28+i*16, struct.pack('<HHHHBBBBBBBB', 32,32,1,5,0,0,0,0,0,0,0,0))
        for i in range(1,6):
            self.write(NODE+i*40, i*32, 'h'); self.write(NODE+i*40+2, 32, 'h')
            self.write(NODE+i*40+24, 1 if i==5 else i+1, 'H')
            self.write(NODE+i*40+26, 5 if i==1 else i-1, 'H')
        for i in range(1,4):
            self.cpu.mem_write(LAND+i*52+4, b'other\0' if i==3 else bytes.fromhex('b0eaa5f300'))
            self.write(LAND+i*52+25, 2, 'B'); self.write(LAND+i*52+28, i*1000, 'H')
            self.write(LAND+i*52+44, 0xfffffff9)
        self.write(FACILITY+56+25, 2, 'B'); self.write(FACILITY+56+34, 5000, 'H'); self.write(FACILITY+56+48, 12345)
        self.write(COMPANY+52+24, 2, 'B'); self.write(COMPANY+52+40, 0xffffffef)
        for i in range(46): self.write(0x496d08+i*24, i+1 if i<14 else 15 if i<16 else 16+(i-16)//10, 'B')
        for i in range(9): self.write(0x498ea0+i*52+28, 0x10024000)
        self.write(0x10024004, 80)
        self.call(0x456f50, seed)

    def interaction(self, slot, seed, scenario, stopped, sleeping):
        self.setup(slot, seed)
        self.write(0x498e28+(slot-4)*16+13, sleeping, 'B')
        flags=0
        if scenario in (1,2): flags=0x600
        if scenario==2: self.write(0x496b68+104+21, 0, 'B')
        if scenario==3: flags=0x300
        if scenario==4: flags=14
        if scenario in (5,6,7): self.write(NODE+40+32, {5:2001,6:4001,7:6001}[scenario], 'H')
        if 8<=scenario<=25:
            kind=scenario-7
            object_id=kind if kind<=15 else {16:17,17:27,18:37}[kind]
            flags=object_id<<16|0x200
        if scenario in (26,27):
            flags=4 if scenario==26 else 5
            self.write(0x498e28+(slot-4)*16+11, 0x81 if scenario==26 else 0x82, 'B')
        self.write(NODE+40+36, flags); self.write(0x48baf8, 0 if stopped else 2)
        self.call(0x41b42d)
        row=[slot,seed,scenario,stopped,sleeping,self.read(0x1000410c),self.read(0x48baf8)]
        for i in range(4):
            row.append(struct.unpack('<H',self.cpu.mem_read(0x496b68+i*104+48,2))[0])
            row+=list(self.cpu.mem_read(0x499120+i*15,15))+list(self.cpu.mem_read(0x49915c+i*15,15))
        row+=list(self.cpu.mem_read(0x499198,30))+list(self.cpu.mem_read(0x497320,8))
        row+=[len(self.payments)]+sum(self.payments,[])+[0]*(12-4*len(self.payments))
        row+=[len(self.boundaries)]+sum(self.boundaries,[])+[0]*(9-3*len(self.boundaries))
        return row


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game',type=Path,nargs='?',default=(Path(__file__).resolve().parents[1] / "game-assets"))
    parser.add_argument('--native-test',type=Path)
    args=parser.parse_args()
    source=next(p for p in args.game.iterdir() if p.name.lower()=='rich4.exe'); executable=source.read_bytes()
    if hashlib.sha256(executable).hexdigest()!=EXPECTED_EXE: raise SystemExit('Expected verified original EXE')
    original=Actors(executable)
    original.call(0x456f50,0)
    if original.call(0x456f2d)!=0 or original.read(0x1000410c)!=12345: raise RuntimeError('Original RNG self-check failed')
    output=ROOT/'.local/special-actors-oracle'/str(time.time_ns());output.mkdir(parents=True)
    counts={}
    with (output/'status.csv').open('w',newline='') as file:
        writer=csv.writer(file);writer.writerow(['actor','before','sleep','walk','stop','tortoise']);rows=0
        for slot in range(4,8):
            original.setup(slot,0)
            for value in range(256):
                original.cpu.mem_write(0x498e28+(slot-4)*16+12,bytes([value])*4)
                original.call(0x41c84f,slot)
                writer.writerow([slot,value,*original.cpu.mem_read(0x498e28+(slot-4)*16+12,4)]);rows+=1
        counts['status.csv']=rows
    with (output/'routes.csv').open('w',newline='') as file:
        writer=csv.writer(file);writer.writerow(['actor','seed','mask','node','previous','x','y','direction','rng','oldflags','newflags']);rows=0
        for slot in range(4,8):
            for seed in SEEDS:
                for mask in range(16):
                    original.setup(slot,seed)
                    original.write(NODE+40+28,3,'H');original.write(NODE+40+30,4,'H')
                    flags=0x1000<<(slot-4)
                    for i in range(4):
                        if mask&(1<<i):flags|=0x40000000>>i
                    original.write(NODE+40+36,flags)
                    for frame in range(256):
                        if original.call(0x40c05c):break
                    else:raise RuntimeError('Original NPC step did not finish')
                    raw=original.cpu.mem_read(0x498e28+(slot-4)*16,16)
                    x,y,node,last=struct.unpack_from('<HHHH',raw)
                    writer.writerow([slot,seed,mask,node,last,x,y,raw[9],original.read(0x1000410c),original.read(NODE+40+36),original.read(NODE+node*40+36)]);rows+=1
        counts['routes.csv']=rows
    with (output/'interactions.csv').open('w',newline='') as file:
        writer=csv.writer(file);writer.writerow(['actor','seed','scenario','stopped','sleepwalking','rng','steps', 'points/cards/tools/supplies/payment/removal/confinement']);rows=0
        for slot in range(4,8):
            for seed in SEEDS:
                for scenario in range(28):
                    for stopped in (0,1):
                        for sleeping in (0,128):
                            writer.writerow(original.interaction(slot,seed,scenario,stopped,sleeping));rows+=1
        counts['interactions.csv']=rows
    if counts!={'status.csv':1024,'routes.csv':256,'interactions.csv':1792}:raise RuntimeError(f'Incomplete capture{counts}')
    manifest={'schema':'original-special-actors-v1','exe_sha256':EXPECTED_EXE,
              'oracle':'original-rich4-v3.11-x86-with-explicit-platform-adapters',
              'adapter_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'functions':['0x41c84f','0x40c05c','0x41b42d'],
              'ui_only_ret_stubs':[hex(address) for address in UI_STUBS+STUBS],
              'boundaries':['private CRT thread context','payment/removal/confinement arguments recorded without applying callback effects'],
              'self_checks':['original srand/rand seed0 gives0 and state12345'],
              'outputs':{name:{'rows':count,'sha256':hashlib.sha256((output/name).read_bytes()).hexdigest()} for name,count in counts.items()}}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    latest=output.parent/'latest'
    if latest.is_symlink():latest.unlink()
    if latest.exists():raise RuntimeError('Refusing to replace non-symlink latest')
    latest.symlink_to(output,target_is_directory=True)
    print(f'Captured original NPC status, routes, interactions: {counts} at {output}',flush=True)
    if args.native_test:subprocess.run([str(args.native_test.resolve()),'--oracle',str(output)],check=True)


if __name__=='__main__':main()
