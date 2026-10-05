# 016 — Native property auctions

`include/rich4/auction.hpp` and `src/auction.cpp` replace the economic state
machine behind the Win32 auction dialog with a resumable C++ session. The
module reads and writes the same `World` used by ordinary turns. It does not
open a window, call Wine, play sound, or consult a wall clock.

## Original sources

The local upstream checkout is the source reference; it is not a runtime
dependency.

| Original routine | Local source | Responsibility |
| --- | --- | --- |
| `0x43bde5` | `.local/upstream/asm/rich4_ui_auction.asm:2458` | Starting price, participants, ownership and payment |
| `0x439f0d` | `rich4_ui_auction.asm:85` | Computer bidders' maximum prices |
| `0x43a2dd` | `rich4_ui_auction.asm:412` | Bid/pass/withdraw state machine |
| `0x43af13` | `rich4_ui_auction.asm:1315` | Starting participant selection |
| `0x43b124` | `rich4_ui_auction.asm:1474` | Computer bid increment selection |
| `0x43b295` | `rich4_ui_auction.asm:1606` | Completion and next participant |
| `0x41d2c6` | `.local/upstream/asm/rich4_player_core_actions.asm:5058` | Money transfer and income/spending counters |
| `0x4751f0` | `.local/upstream/asm/rich4.asm:40670` | Six saved ownership modes' packed lease intervals |

## Rules retained

Starting price is `trunc(basePrice * (1 + level * 0.5)) * priceIndex`, using
the land or facility's base price. It is not the normal buy/upgrade value.
A participant must have a nonzero controller byte, cash **strictly greater**
than the starting price, no hotel/disappearance/prison/hospital/sleep/sleepwalk
status, and must not be the seller argument. Release-marker status values such
as `0x80` still exclude a player. Savings do not fund bidding.

Participant records retain the original active-player order and the holes
left by excluded players. The original start-selection loop does not update
its cached sentinel and therefore selects the **last** eligible record. The
port retains that behavior. Later turns wrap through four records, skipping
withdrawn records.

Action numbers retain the original dialog values:

| Action | Meaning |
| --- | --- |
| 0 | Pass for the current price |
| 1–5 | Raise by 100, 500, 1,000, 5,000 or 10,000 |
| 6 | Withdraw permanently |

An increase is legal when the resulting price is at most the bidder's cash.
An accepted increase clears every temporary pass and records the bidder.
The auction completes when no participants remain, everyone has passed, or
only one unpassed participant remains after an actual bid. The original last
bidder result is retained by these completion paths. An auction with no bid
does not sell the property. A sole participant must still place a bid.

For each eligible computer participant, `0x439f0d` consumes two economic
random values. Its formulas are:

```
enthusiasm = float((rand() / 32767.0) * 0.3 + 0.5)
scarcity = float(6 - 4 * unownedCount / totalCount)
desired = trunc((level / 2 + 1 + sameStreetOwned) * startingPrice
                * priceIndex * scarcity * enthusiasm)
ceiling = trunc(float(basePrice * priceIndex) * (3 + rand() / 65536.0))
maximum = min(desired, ceiling, cash)
```

The same-street count is only used for land; names compare original bytes up
to NUL. `level / 2` is integer division. The second price-index multiplication
in `desired` is present in the original and is deliberately retained. Integer
multiplications use the original low 32 bits. Binary32 storage points are
explicit. The final two-coefficient product is evaluated with integer
significands and rounded to the x87 64-bit significand before truncation;
using the host's `long double` would lose precision on Apple Silicon. This
small arithmetic helper uses Clang/GCC's 128-bit integer type, available on
the native Mac and Android toolchains.

Computers pick the largest permitted increment under their maximum. They
reduce an unnecessarily large increase relative to the preceding bidder's
cash plus 500 using the original increment bands. If there was only one
eligible participant, any non-pass computer action becomes a 100 increase.

Settlement preserves building type/level, temporary effects, and an existing
owner's lease. A previously unowned property obtains an expiry only in a
nonzero saved `World::lease_mode`. The intervals are 0, two years, one year,
six months, three months and one month. They use the original packed-date
addition: January 31 plus one month remains the encoded February 31.

The buyer pays cash and records cumulative spending. Seller `-1` credits
`World::public_fund`; a player seller receives the payment in their **bank**
account and cumulative income, matching original payment flags `0`. Since
bids have already checked available cash, settlement does not need the
ordinary bankruptcy path. Rejected bids and calls after completion cannot
repeat a payment.

## Integration

Construct `AuctionSession(world, random, propertyCode, seller)`. `tick()`
executes one computer action. `decision()` exposes the current human player,
price, and legal action codes; pass the chosen action to `bid()`. Inspect
`finished()` and `result()` for the outcome. The world and random stream must
outlive the session and the world must not be mutated during its pending
decision. A caller using transactional choice replay can reconstruct the
session and replay its chosen actions against the restored world and stream.

## Deliberate boundary

The original Win32 timer calls `rand()` for the announcer's face and mouth
animations at `0x43b5f7`, `0x43b945`, and `0x43baa7`. The number of these calls
depends on elapsed UI/audio time. This module preserves the valuation draws
and economic transitions, but does **not** inject a guessed number of cosmetic
draws into the game stream. Consequently it does not claim to reproduce the
original global PRNG state after an arbitrarily timed dialog. An exact trace
comparison must supply the original timer/input schedule or exclude these
cosmetic calls at the oracle boundary.

Out-of-range property codes, invalid saved lease modes, and a starting-price
overflow to a negative value are rejected explicitly. Reproducing invalid
original pointer access or allowing a malformed negative auction price is
not part of the supported save domain.

## Validation

`tests/auction_tests.cpp` checks initial truncation, cash/status eligibility,
the original starting order, pass reset after a raise, permanent withdrawal,
rejected actions, all-pass no-sale, exact-cash purchases, seller/public-fund
accounting, facility preservation, and packed lease dates. It checks the two
valuation draws and sole-computer-bidder behavior. It also completes 2,000
four-computer auction scenarios across prices, levels and cash balances,
asserting termination, nonnegative cash and money conservation.

The test binary passed with AddressSanitizer and UndefinedBehaviorSanitizer
on native arm64 macOS. These are source-derived and state-machine tests;
an original executable differential capture for this dialog has not yet been
recorded. They are not presented as proof of timed-dialog PRNG equivalence.
