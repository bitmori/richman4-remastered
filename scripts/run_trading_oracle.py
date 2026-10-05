#!/usr/bin/env python3
"""Compare native market transactions against original Rich4 v3.11 instructions.

Synthetic fixtures only; no Windows, Wine, host windows, sound or original game
assets are distributed. Requires the same Unicorn environment as stock oracles.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

from audit_assets import EXPECTED_EXE
from run_stocks_precision_oracle import Original, ROOT

LANDS, FACILITIES = 0x10001000, 0x10002000
OFFERS, PLAYERS, CARDS, TOOLS, HOLDINGS = 0x4967e0, 0x496b68, 0x499120, 0x49915c, 0x4971a0
RNG = 0x1000400c


class Market(Original):
    def __init__(self, executable):
        super().__init__(executable)
        # Only rendered information refresh and the separately tested financing
        # reconciliation are outside this isolated market module.
        for address in (0x41d433, 0x436b0a):
            self.cpu.mem_write(address, b'\xc3')
        self.cpu.mem_write(0x10004100, b'\xb8\x00\x40\x00\x10\xc3')
        self.write(0x488f4c, 0x10004100)

    def setup(self, seed):
        for address, size in ((OFFERS, 336), (PLAYERS, 416), (CARDS, 60), (TOOLS, 60),
                              (HOLDINGS, 384), (0x496980, 432), (LANDS, 8*52), (FACILITIES, 4*56)):
            self.clear(address, size)
        for address, value in ((0x49910c, 0), (0x499114, 4), (0x4990e8, 2),
                               (0x498e84, LANDS), (0x498e88, FACILITIES), (RNG, seed)):
            self.write(address, value)
        for slot in range(4):
            p = PLAYERS + slot*104
            self.write(p+21, 2, 'B')
            self.write(p+28, 100000)
            self.write(p+32, 10000)
            self.cpu.mem_write(CARDS+slot*15, bytes([1,3,5,7,9])+bytes(10))
            self.cpu.mem_write(TOOLS+slot*15, bytes([1,2,3,1,2,3,1,2,3,1,2,3,1,0,0]))
            for stock in range(12):
                self.write(HOLDINGS+slot*96+stock*8, 100+slot)
                self.write(HOLDINGS+slot*96+stock*8+4, 12.375, 'f')
        for i in range(1,8):
            p=LANDS+i*52
            self.write(p+25,1,'B'); self.write(p+26,2,'B')
            self.write(p+28,1000,'H'); self.write(p+30,500,'H')
            self.write(p+48,0x07eb0101)
        for i in range(1,4):
            p=FACILITIES+i*56
            self.write(p+24,2,'B'); self.write(p+25,1,'B'); self.write(p+26,3,'B')
            self.write(p+34,2000,'H'); self.write(p+36,700,'H')
            self.write(p+52,0x07eb0101)
        for i in range(12):
            self.write(0x496980+i*36+20,20.25,'f')
            self.write(0x496980+i*36+8,777,'H')
        self.cpu.mem_write(0x499198,bytes([3])*30)
        self.cpu.mem_write(0x497320,bytes([10])*8)

    def post(self, kind, item, price, quantity=0):
        self.call(0x4246c5,0,kind,item,price,quantity)

    def purchase(self):
        self.write(0x49910c,1)
        if self.call(0x4255da,0,0)==1:
            self.call(0x4247d5,0,0)

    def signature(self):
        result=bytearray(self.cpu.mem_read(OFFERS,336))
        for slot in range(4):
            p=PLAYERS+slot*104
            for address,size in ((p+28,8),(p+92,8),(CARDS+slot*15,15),(TOOLS+slot*15,15),(HOLDINGS+slot*96,96)):
                result.extend(self.cpu.mem_read(address,size))
        result.extend(self.cpu.mem_read(0x499198,30)); result.extend(self.cpu.mem_read(0x497320,8))
        for i in range(1,8): result.extend(self.cpu.mem_read(LANDS+i*52+25,1))
        for i in range(1,4): result.extend(self.cpu.mem_read(FACILITIES+i*56+25,1))
        result.extend(self.cpu.mem_read(RNG,4))
        return result.hex()

    def scenario(self, kind, seed):
        self.setup(seed)
        if kind==0:
            self.write(OFFERS+8,4321,'H'); self.write(OFFERS+10,77,'B'); self.write(OFFERS+11,88,'B')
            self.post(4,3,111)
        elif kind==1: self.post(2,2001,1000)
        elif kind==2: self.post(2,4001,1000)
        elif kind==3: self.post(1,2,1000,99)
        elif kind in (4,5):
            for i in range(1,8): self.post(2,2000+i,100)
            if kind==4: self.post(2,2004,200)
            else: self.call(0x4247d5,0,3)
        elif kind==6:
            for i in range(1,4): self.post(3,i,100)
            self.write(TOOLS,0,'B'); self.write(TOOLS+2,0,'B')
            self.call(0x42483e)
        elif kind==7:
            self.post(2,2001,100); self.post(2,4001,100)
            self.write(LANDS+52+26,3,'B'); self.write(FACILITIES+56+24,1,'B')
            self.call(0x42483e)
        elif kind in (8,9,10,11,12,13):
            listing={8:(4,3,1234),9:(3,1,1234),10:(1,2,1001,100),11:(2,2001,1234),12:(2,4001,1234),13:(3,1,1234)}[kind]
            self.post(*listing)
            if kind==13: self.write(TOOLS+15,9,'B')
            self.purchase()
        elif kind>=14:
            if kind==14: self.clear(CARDS,60); self.clear(TOOLS,60)
            elif kind==16: self.cpu.mem_write(CARDS,bytes([1,1,2,3,4,5,6,7,8,9,10,11,12,0,0]))
            elif kind==17:
                self.post(1,2,1001,100); self.post(2,2001,11000)
                self.write(0x49910c,1)
            self.call(0x4284be)
        else: raise ValueError(kind)
        return self.signature()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path.home()/'Downloads/rich4-original-game')
    parser.add_argument('--native',type=Path,default=ROOT/'build/native/trading_tests')
    args=parser.parse_args()
    executable=next(p for p in args.game.iterdir() if p.name.lower()=='rich4.exe').read_bytes()
    if hashlib.sha256(executable).hexdigest()!=EXPECTED_EXE: raise SystemExit('Original Rich4 v3.11 EXE required')
    native=subprocess.run([str(args.native),'--oracle'],check=True,text=True,capture_output=True).stdout
    original=Market(executable)
    original.setup(0)
    if original.call(0x456f2d)!=0 or original.read(RNG)!=12345: raise RuntimeError('RNG self-check failed')
    rows=[]
    for line in native.splitlines():
        kind,seed,actual=line.split(); kind=int(kind);seed=int(seed)
        expected=original.scenario(kind,seed)
        if actual!=expected:
            a,e=bytes.fromhex(actual),bytes.fromhex(expected)
            offsets=[i for i,(x,y) in enumerate(zip(a,e)) if x!=y]
            raise RuntimeError(f'Mismatch case={kind} seed={seed}: offsets={offsets[:20]}, native={[a[i] for i in offsets[:20]]}, original={[e[i] for i in offsets[:20]]}')
        rows.append(line)
    output=ROOT/'.local/trading-oracle'/str(time.time_ns()); output.mkdir(parents=True)
    capture='\n'.join(rows)+'\n';(output/'comparisons.txt').write_text(capture)
    manifest={'schema':1,'executable_sha256':EXPECTED_EXE,'native':str(args.native.resolve()),'cases':len(rows),
              'mismatches':0,'comparisons_sha256':hashlib.sha256(capture.encode()).hexdigest(),
              'boundaries':{'0x40a4e1':'minimap redraw returns','0x41d433':'player info redraw returns',
                            '0x436b0a':'financing reconciliation returns; tested independently in GameSession',
                            '0x488f4c':'isolated CRT thread storage'},
              'coverage':['create/replace/compact seven offers','property type/level snapshots','cleanup cursor behavior',
                          'cash-to-bank payments and counters','card/tool pool transfers and refusal',
                          'stock weighted cost and seller reset','land/facility owner change',
                          '200 seeds each: empty, surplus tool, duplicate card, stock/property bargain AI']}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'cases':len(rows),'mismatches':0,'output':str(output)}))


if __name__=='__main__': main()
