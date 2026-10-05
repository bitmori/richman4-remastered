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
all possible corrupt saves. The initial static milestone is independently committed; the daily market and
purchase extension is described below. A forced
`update_stocks` call must not be used as a substitute for daily `0x4291d6`.

## Daily market and purchases

The following operations are now migrated as well:

| Original entry | Portable operation | Behavior |
| --- | --- | --- |
| `0x428d2a` | `buy_stock` | Market quote × quantity charged to bank, or integer company valuation/10000 × quantity charged to cash; exact weighted cost basis and shareholder ranking |
| `0x42915a` | `refresh_trade_limits` | Reset turn trading quota; available ≤1000 is copied, otherwise one random draw scales available by 10…29.99% |
| `0x4291d6` | `daily_update` | Market-wide drift, per-stock impulse/momentum, company or baseline valuation feedback, suspension/news limits, price history and market index |
| `0x4295ea` | `trend` | Rising/limit-up/falling/limit-down/unchanged, using the original rounded ±10% prices |

The caller supplies the calendar holiday predicate. `daily_update` also checks
`World.news_effect_days`; closed markets return without altering stock state,
history, market index or RNG. The original global index and market movement
are represented by `World.stock_market_index` and
`World.stock_market_movement_bits`, persisted at SAVE offsets `0x26a2` and
`0x26a6`. These offsets are distinct from the nearby public fund.

For an open market, suspension takes precedence over news flags and suppresses
that stock's random draw. Forced news also suppresses its draw. Otherwise the
stock uses the original unfloored extended impulse when multiplying by
volatility, despite separately storing the impulse as binary32. Standalone
stocks compare with 0.5×/8× baseline; company stocks compare with 0.85×/3×
valuation per 10000 shares. The 0.85 constant is the original binary64 value.
Every original binary32 spill, the 144-sample history cursor and final market
index conversion are retained.

Day counters are deliberately owned by the session: it first updates the
calendar, news freeze, suspension and news-movement nibbles, then calls the
market update. `refresh_trade_limits` is called at each player's turn, not once
per day. Neither stock routine reseeds the RNG. The original *caller* at
`0x41d066` uses `GetTickCount` as the daily Watcom seed. Native callers can inject
and record that seed, or continue their explicitly deterministic replay stream;
replaying the same market inputs and seed is exact, but the stock module does
not claim to reproduce an unrecorded wall-clock seed. Map holidays are supplied
by the separate holidays module, which documents the original lunar table's
1998–2020 limit and later-date policy.

Market purchases validate available shares, per-turn quota and bank balance.
Primary purchases validate primary shares and cash, and compute the unit price
by integer division **before** multiplying by quantity. Both charge accounts
before recomputing the stored binary32 weighted average basis. Invalid or zero
purchases throw without changing the world; the original UI prevented these
inputs before calling its unchecked routine.

`capture_stock_market.py` extends the precision oracle. Two additional host
boundaries are explicit: its private CRT thread-state block replaces only the
thread lookup import (the original rand/srand instructions still run), and the
calendar holiday predicate returns each fixture's supplied Boolean. It does
not substitute random numbers, stock formulas, conversions or ranking. Every
entry is checked against its original instruction bytes; this caught and
corrected an initial one-byte address error at the purchase entry.

Additional original-instruction comparisons, including raw float bits and
final RNG state, all pass with **0 mismatches**:

- **6,144** trading-limit records in 512 complete market scenarios.
- **12,288** daily records in 1,024 complete market scenarios.
- **1,024** market/primary purchase scenarios.
- **2,048** price-trend scenarios, including equality and one ULP beyond caps.

The complete suite passes AddressSanitizer and UndefinedBehaviorSanitizer.
No native window or audible output is created by these tests. The command
sequence above regenerates schema `original-stocks-precision-v2` evidence with
both static and market CSV files.
