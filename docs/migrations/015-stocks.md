# 015 — Stocks and player wealth

The portable C++20 module `stocks.hpp` / `stocks.cpp` translates Iru Cai's
GPL-3.0-or-later `asm/rich4_stocks.asm` and
`asm/rich4_calculate_player_wealth.asm`. The original executable is Rich4 v3.11,
SHA-256 `5a90aee28ee5f7a5c3ba5cb935c9e55751a529c25fcd91208748a66293569550`.
The production module has no Windows API, assembly, process globals, guest
pointers, or original executable dependency.

## Completed static operations

| Original entry | Portable operation | Preserved details |
| --- | --- | --- |
| `0x428ec5` | `adjusted_price` | Percentage change, candidate-based tick bands, rounding the *change*, bounds 1…9999 |
| `0x428e23` | `sell_stock` | Share counters wrap as words; partial basis retained, zero quantity clears basis; proceeds to bank or public fund |
| `0x4294d5` | Internal shareholder ranking | Remove/reinsert seller, incumbent wins equal holdings, one-based owner list; return redraw decision |
| `0x429040` | `update_stocks` | Forced news/card rise or fall; high nibble wins; overwrite preceding history slot without advancing cursor or drawing RNG |
| `0x4239b9` | `player_wealth` | Cash + bank − loan; per-stock integer accumulator converted to binary32 before adding stock value; original unindexed property values |

`credit_bank=true` means the bank account, never cash. `false` credits the
original `0x499080` public/liquidation fund. A share sale retains the original
16-bit count and 32-bit account wrapping, while invalid indices, negative
quantities and overselling are rejected before mutation. Wealth treats a chain
store as one building level and uses normal land/facility levels otherwise.
It does not multiply by the game's current price index.

The original x87 uses a 64-bit significand. ARM's `long double` has 53 bits and
is insufficient. A small private finite arithmetic type implements explicit
64-bit significand rounding with `uint64_t` and the Clang/GCC `__uint128_t`
integer extension, including explicit binary32 stores. These are numeric
widths, not 32-bit host-address assumptions. This currently targets the
project's macOS/Android Clang and Linux GCC/Clang toolchains. MSVC without its
128-bit integer equivalent is not yet supported. Invalid nonfinite prices are
rejected; tested market price inputs are around 1…9999 and movements −10…10.

## Differential evidence and an emulator trap

`run_stocks_oracle.py` loads the private original EXE in an isolated windowless
Boxedwine process and invokes original instructions at `-O0` through opaque
function pointers. It checks the EXE hash, original entry bytes, a known price
result, completion marker, output counts and hashes. The only game function
stub is minimap refresh `0x40a4e1`, which returns without drawing after a change
of commercial owner. No original file is modified.

The initial capture exposed a real oracle limitation: Boxedwine's cached FPREM
implementation divides in binary64 and truncates the rounded quotient. For
`1.5 % binary64(0.05)`, it produces zero; Unicorn 2.1.4's default instruction
implementation produces a tiny negative remainder. Both differ from x87.
`tools/oracle/fprem_probe.c` executes the FPREM instruction through macOS's
x86_64/Rosetta runtime and observes `0.0499999999999999195`, agreeing with exact
binary integer arithmetic and `fmod`. Consequently a price of 15 falling 10%
rounds to the binary32 representation of 13.55 with the original algorithm.

`run_stocks_precision_oracle.py` therefore executes the original executable
bytes in Unicorn 2.1.4 with **one explicitly disclosed instruction correction**:
FPREM at `0x458425` computes its finite binary quotient/remainder using Python
integers, including quotient status flags. It does not substitute the game
price function or native implementation. The original x87 arithmetic,
conversions, decisions and memory stores still execute. A flat 32-bit guest
segment setup also permits the original overlapping short memcpy in ranking.

This is an instruction-level differential oracle with a corrected emulated
instruction, not an unqualified hardware run of the original EXE. The separate
Boxedwine capture is retained to make the 150 affected price cases visible;
sales and forced-update fixtures agree between the engines. Against this
precision oracle the portable module passes:

- **100,618** price bit patterns, including ±8 ULP around all price bands.
- **104** sale/ranking/account scenarios.
- **2,184** forced-update stock records across selection and history wrap cases.
- **1,024** wealth cases, including binary32 integer rounding, negative net
  balances, overflow/integer-indefinite, ownership and chain-store values.
- **0 mismatches**, including raw binary32 outputs.

Private evidence lives under `.local/stocks-oracle/latest` (Boxedwine) and
`.local/stocks-oracle/precision-latest` (corrected instruction oracle). These
CSV files, runtime copies and original game assets are not committed.

Reproduce on the configured development Mac:

```sh
python3 scripts/run_stocks_oracle.py
python3 -m venv .local/stocks-oracle/venv
.local/stocks-oracle/venv/bin/pip install unicorn==2.1.4
.local/stocks-oracle/venv/bin/python scripts/run_stocks_precision_oracle.py
clang -arch x86_64 tools/oracle/fprem_probe.c -o .local/stocks-oracle/fprem-probe
.local/stocks-oracle/fprem-probe
build/native/stocks_tests .local/stocks-oracle/precision-latest
```

Synthetic unit tests also cover liquidation, tie ownership, counter/account
wrap, invalid sales, history wrap and suspension/forced-news interaction.
Neither captured tables nor these fixtures establish exhaustive behavior for
all possible corrupt saves. Pure static operations are complete; stochastic
daily market updates, primary/market purchases, holiday policy and replayable
clock reseeding are tracked separately in the next integration step. A forced
`update_stocks` call must not be used as a substitute for daily `0x4291d6`.
