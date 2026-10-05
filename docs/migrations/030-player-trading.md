# 030 — Player market listings and computer trading

`trading.hpp` / `trading.cpp` migrate the player-to-player sale market from
`asm/rich4_ui_sale.asm`. This is a separate market from the points-based item
shop and the stock exchange. Players can post, replace or withdraw seven offers
each, then purchase another player's listed stock, property, tool or card.
Posting does not remove or reserve the offered asset.

## Original entries and storage

| Original entry | Native operation | Rules retained |
| --- | --- | --- |
| `0x4246c5`, source line 306 | `post` | First empty or same-kind/same-item slot; seven-slot limit; captures property type and level |
| `0x4247d5`, line 409 | `cancel` | Shift later complete records left and zero the final record |
| `0x42483e`, line 451 | `prune` | Remove offers whose underlying inventory, property owner, property type or level no longer matches |
| `0x4255da`, line 1691 | `buy` | Inventory transfer, buyer cash, seller bank, spending/income counters |
| `0x425ea2`, line 2354 | `listing_limits`, stock | Quantity times current binary32 quote, extended-precision truncation |
| `0x426569`, line 2880 | `listing_limits`, property | Land/facility acquisition value including all current building levels |
| `0x426af2` / `0x426f0f`, lines 3320 / 3640 | `listing_limits`, inventory | Item table price times 100 times price index |
| `0x4284be`, line 5334 | Market entry | Prune before use; financing reconciliation on exit |
| `0x42886e`, line 5638 | `ai_turn` | Three randomized stages: listing, repricing, bargain purchase |

The offers table at original VA `0x4967e0` is **not the lottery ticket table**.
Its SAVE offset is `0x2526`: four players × seven records × twelve bytes. World
now exposes `offers[4][7]` as portable values; decoding and encoding use explicit
little-endian offsets, without packed host structures or pointers.

| Record offset | Type | Meaning |
| --- | --- | --- |
| 0 | byte | 0 empty, 1 stock, 2 property, 3 tool, 4 card |
| 1 | byte | Preserved flags, reset to zero when posted |
| 2 | little-endian uint16 | Stock 0..11; property 2001+/4001+; tool/card 1-based |
| 4 | little-endian int32 | Asking price |
| 8 | little-endian uint16 | Share quantity for stock offers |
| 10, 11 | two bytes | Property type and level at posting |

Original posting leaves unused quantity/type/level bytes untouched. The native
code does likewise, including replacement of existing records. Fresh games
start with zero offers. Existing SAVE files retain all records, unused fields,
unknown kind/flag bytes, other reserved bytes, history and trailing data.

## Transactions and price limits

Human asking prices range from 1 through ten times the reference value. Property
reference is `(land_price + level * construction_price) * price_index`, using
the separate land and facility fields. Tools/cards use their original table
prices. Stock reference is `trunc(quantity * current_quote)` with the shared
software x87 extended-precision implementation. Original 32-bit integer
arithmetic wraps explicitly. This market does not impose stock-exchange holiday,
suspension, daily limit or bank-funding rules.

A buyer must have the entire asking price in cash. The seller receives that
amount in the bank; cumulative spending and income increase by the same amount.
Purchases do not add hostility. Tools retain the nine-per-type limit and cards
retain the fifteen-card hand limit. Card removal shifts the seller's hand, and
receipt fills the buyer's first empty slot. Shared inventory supply runs through
the original consume/receive routines.

Stock transfers leave exchange quotes and available share quantities unchanged.
The buyer's prior total cost is truncated before adding the asking price;
division by the new holding is stored as binary32. The seller's cost basis is
zeroed when their holding reaches zero. The original commercial-owner update
runs for the buyer only; it can transfer control of a company.

Private property transfer changes only its owner byte: building state, lease
expiry, temporary state and other property metadata remain intact. A successful
public `buy` includes the caller's offer cancellation, making one complete action
for GameSession/UI. Every purchase rechecks the selected offer before mutation.

## Computer behavior and RNG

`ai_turn` consumes the original Watcom stream and preserves gate order:

1. Draw `% 15`. On zero, if the hand has more than twelve cards, form the ordered
   pairs of matching IDs at different slots. An ID occurring `k` times has
   `k * (k - 1)` entries. One additional draw picks a duplicate to list. If there
   are no duplicate candidates, instead collect tools with amount at least three,
   or nonzero amount with `tool_aggression - rescue_strategy == 2`; one additional
   draw selects a tool. An occupied seventh offer causes the oldest offer to be
   withdrawn first, even when the subsequent post replaces an existing item.
2. Draw `% 3`. On zero, reprice all own card/tool offers to current reference value.
3. Draw `% 4`. On zero, scan other active players and their seven slots in order.
   A stock bargain has `trunc(asking_price / quantity) < current_quote`. A property
   bargain requires both `3 * reference_value > asking_price` and
   `2 * asking_price < buyer_cash`. Stop after the first successful purchase.

The computer does not buy listed tools or cards. All three stage draws occur even
with no listings or inventory. GameSession retains the surrounding original
sequence: stock buying/selling, financing mode 0, this market, financing mode 0,
then card/tool selection and dice selection. Financing is outside this module.

## Original quirks and safety boundaries

Original pruning retains its `deleted` flag after the first deletion. Its scan
therefore stays at the shifted entry for the remaining seven-attempt budget; a
valid shifted entry can delay cleanup of a later stale entry until the next
market visit. This behavior is preserved and verified against executable bytes.
`quotes` hides currently invalid entries while returning their real slot index;
`buy` rejects stale selections so a UI cannot transfer another person's asset.

The original duplicate-card candidate array has only sixteen bytes and can
overwrite its own stack when many duplicates exist. Native code uses a vector
with the same pair order and weighting for defined cases. It intentionally does
not emulate memory corruption beyond that original capacity. Public APIs also
reject out-of-range IDs, unknown offer kinds, nonpositive prices, nonfinite stock
costs/quotes, impossible quantity/holding ranges and invalid price limits, before
indexing or signed overflow. Unused and unknown bytes still round-trip unchanged.
These safety checks do not change the verified normal gameplay domain.

## Verification

- `trading_tests` passed AddressSanitizer and UndefinedBehaviorSanitizer. It tests
  slot limits/replacement, original cleanup cursor behavior, stale-property
  rejection, bank-vs-cash rules, inventory limits/order/supply, preserved leases,
  stock cost truncation, commercial control, strict AI bargain thresholds and RNG.
- `world_tests` checks lossless source round-trip and exact byte changes for every
  field of the final player's final offer. No snapshots or padding may drift.
- `scripts/run_trading_oracle.py --native /path/to/trading_tests` executed 814
  native/original comparisons with **zero mismatches**: fourteen mutation cases,
  plus 200 seeds each for empty inventory, tool listing, duplicate-card listing,
  and stock/property bargains. Compared output includes all 336 listing bytes,
  every player's cash, bank, spending/income, cards, tools, share amounts and cost
  bits, shared inventory counts, property owners, and final PRNG state.

The oracle executes the user's original v3.11 EXE bytes under Unicorn, without
Wine or a host window. Only minimap/info redraw, CRT thread storage, and the
separately integrated financing exit have isolation adapters. Posting, pruning,
stock arithmetic, inventory transfer, payment, AI branching and `rand` execute
original instructions. Capture:
`.local/trading-oracle/1791184135297179000/manifest.json`.

```sh
.local/stocks-oracle/venv/bin/python scripts/run_trading_oracle.py \
  --native build/native/trading_tests
ctest --test-dir build/native -R '^(world|trading)$' --output-on-failure
```

The fixtures and comparisons are synthetic rule checks. They do not claim that
the original game or native UI has completed a whole match through these actions;
interactive acceptance is recorded separately in migration 018.
