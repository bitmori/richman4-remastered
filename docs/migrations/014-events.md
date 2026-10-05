# News, fortune and persistent event state

This module replaces the news/fortune deck selection and 36 news / 49 effective
fortune dispatch branches with portable C++20. Sources are mytbk/rich4 revision
`ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`, `asm/rich4_news.asm`,
`asm/rich4_fortune.asm`, and helper routines in `asm/rich4.asm`,
`asm/rich4_card_utils_2.asm`, `asm/rich4_tool_utils.c`. The incomplete
`csrc/news.c` and `csrc/fortune.c` help identify intent but are not the authority:
several of their recovered assignments and transport remappings are incorrect.

## Dispatch and state

The original deck is persisted, not replaced with a fresh random event on landing.
News has 36 IDs, fortune has 37 base IDs. Selection examines the current card,
skips ineligible cards, and advances the saved cursor for every examination,
wrapping without reshuffling. Transport can remap fortune cards 10–16. Base
fortune IDs 33–36 are available only on stage zero and dispatch as
`base_id + map_id * 4`, producing the 49 effective handlers.

The news eligibility predicates reproduce the original record scans, including
checking only the first four prison/hospital roster bytes, checking stock
quantities rather than stock value for ownership predicates, and treating any
nonzero player controller as alive. News 29 separately uses the original
on-board predicate (alive and all four absence counters at 50–53 zero).

Selectors return source/effective IDs, next cursor and skipped count without
mutating state. `apply_news` / `apply_fortune` apply the effective handler and
then commit the cursor. Direct `apply_*_id` functions support explicit replay
without consuming the deck. A malformed ID, cursor, or deck with no eligible
entry is rejected; the old endless search/division fault is not reproduced.

Fresh decks use the original shuffle: draw `rand() % remaining`, select that
rank among unused IDs in ascending order, and mark it used. The final singleton
also consumes a draw. News is shuffled first, then fortune: exactly 73 Watcom
PRNG draws. The missing original fresh-map object initialization is still a
separate boundary; these shuffles alone do not establish whole-new-game seed
equivalence.

World now interprets and writes these original save regions explicitly:

| State | Original global | Save offset |
| --- | --- | --- |
| 36-byte news deck | `0x499090` | `0x26fa` |
| News cursor | `0x4990e0` | `0x26f2` |
| 37-byte fortune deck | `0x496b38` | `0x271e` |
| Fortune cursor | `0x4990b4` | `0x26f6` |
| Eight prison roster bytes | `0x496b30` | `0x26e2` |
| Eight hospital roster bytes | `0x496b60` | `0x26ea` |
| Five 16-byte special actors | `0x498e28` | `0x1b4` |
| Player stock holdings, 4×12×8 | `0x4971a0` | `0x21f6` |
| Twelve 36-byte stock records | `0x496980` | `0x2376` |
| Twelve × 144 binary32 price history values | `0x497328` | `0x6f6` |
| Stock history cursor | `0x499100` | `0x6f2` |
| Public payment fund | `0x499080` | `0x26ba` |
| News 26 duration | `0x4990dc` | `0x269a` |
| Property lease mode | `0x499110` | `0x267e` |

Stock floating-point values are stored as exact binary32 bits in `World`; reading
and exporting does not canonicalize NaNs. Legacy stock/name pointers remain opaque.
Commercial records add stock index at +25, shareholder order at +28, valuation at
+36, funds at +40, funds-change counter at +44, remaining shares at +48.
Player status adds rescue strategy +23, saved direction +27, loan due date +44,
attached bomb +64, signed financial/physical fortune modifiers +70/+72, and
facility ID +74. Facility's six toll words begin at +36: its first word is also
the building price, exposed through `price_per_level()` so there is only one
mutable value for that overlapping field.

The original special-actor template uses inactive states 1,1,2,2,3; fresh prison
roster entries 4/5 and hospital entries 6/7 are set to one. Fresh stock scalar
settings for eight maps come from `asm/rich4_all_stocks.c`; quoted-company links
and remaining shares are initialized from each commercial record. This does not
replace daily trading, stock animation, complete economic initialization, or the
separate stock rule migration.

## News handlers

| IDs | Portable behavior |
| --- | --- |
| 0–3 | Release normal prisoners/patients using pending-release marker `0x80`, or extend their stay by three with the original seven-bit mask |
| 4 | Choose a built property, delegate exact original square-area destruction and actor injuries |
| 5 | Choose a built land/facility and confiscate it, including owner/level/type/expiration |
| 6 / 14 | Choose any property; raise/lower land base prices for its entire encoded street, or only the selected facility |
| 7 | Choose an unowned property and invoke the original auction boundary |
| 8–10 | Award the active player with most property, least property, or most stock quantity; earliest player wins ties |
| 11–13 | Snapshot cash/property/stock taxes, then charge players in order through session payment and bankruptcy |
| 15 | Choose a built ordinary land and downgrade it |
| 16 / 17 | Stop pedestrians / nonpedestrians for one day |
| 18 | Downgrade all lands on the chosen encoded street, or the chosen facility |
| 19 / 21 | Confiscate / downgrade any randomly chosen property |
| 20 | Choose a property and delegate exact square-area building downgrade |
| 22 | Assign fifteen days of bank loan restrictions to active players |
| 23 | Credit one tenth of bank funds to active players without loans, including cumulative income |
| 24 / 25 | Assign all-stock movement flags 1 / `0x10` and call stock update |
| 26 | Assign the original global effect duration to ten |
| 27 / 28 | Suspend a randomly chosen stock for fifteen days and restore its previous price/history; or resume one suspended stock |
| 29 | Choose an owned commercial, process immunity/redirect, then impose five prison days |
| 30–34 | Apply original company cash-flow changes and stock movement flags |
| 35 | Double a profitable company's funds; add **twice** its old funds to the running counter and derive its stock movement flag |

Destruction keeps the original distinction between confiscation and downgrade.
Chain-store downgrade removes the chain store; a facility losing its final level
clears its type and marks all active hotel guests for release. News 15 has a real
original inconsistency: eligibility includes built facilities, but target choice
uses only ordinary lands. If only facilities qualify, the portable handler
reports an explicit unsupported original fault instead of choosing another event.
News 29 likewise retains the difference between eligibility (an active owner must
exist) and selection (all owned companies enter the candidate list).

## Fortune handlers

| IDs | Portable behavior |
| --- | --- |
| 0 / 1 | Randomly remove owned houses / confiscate owned empty land, pay original unindexed compensation, and consume the original reaction-voice draw |
| 2 / 3 | Add a fraudulent loan and update its due date / add thirty bank-rejection days; preserve addition and byte wrapping |
| 4 | Transfer ten percent of other living players' bank balances, using the original stored binary32 `0.1f` fraction and bank-first debit |
| 5 | Obtain a card from each living opponent: original random choice for AI, explicit selectable/cancelable choice for a human |
| 6 / 7 | Travel / alien abduction, with physical modifier, immunity/redirect and the original absence callback |
| 8 / 9 | Sell away ten percent of stock holdings to the public fund / liquidate all holdings into the bank |
| 10 / 11 | Lose equipped bike / car, reset transport and dice, and return the equipment to shared supply |
| 12 / 13 | Injury and hospitalization through the physical-effect boundary |
| 14–31 | Original indexed fines/rewards: -3000,-3000,-3000,-6000,-600,-1500,+1000,+2000,+3000,-1000,-2000,+10000,-8000,+4000,+6000,+8000,-5000,+5000 |
| 32 | Convert equipped transport back to inventory, sell tools then cards for points, preserve 16-bit point wrapping |
| 33–48 | Four map-specific sets of prison events, with durations 3/5/7/9 days and physical modifiers/immunity/redirect |

Financial modifiers read signed word +70, physical modifiers signed word +72.
Values above 100 always protect against adverse events or double financial gains;
51–100 use one parity draw; negative values double adverse results or prevent
financial gains. Individual original exceptions are preserved: bank rejection,
stock losses and inventory sales do not apply a generic extra doubling simply
because the modifier returned two.

Character speech consumes the original handler's random draws even when no audio
callback is installed. `voice(player, category)` receives the already selected
category, so presentation must not draw another gameplay random number. Human
card theft may be canceled per victim; the final happy-reaction draw still occurs
when any eligible victim was processed, even if all choices were canceled.

## Shared effects and precision boundaries

`EventCallbacks` connects session-owned payment/bankruptcy, prison, hospital,
absence, immunity/redirect cards, loan due dates, stock sale/update, auctions,
human card choices and square-area damage. Callbacks receive explicit original
IDs and parameters. Missing required callbacks return `supported=false` before
mutation, leaving deck and RNG untouched. This is an integration gate, never an
instruction to silently skip that event. The session can checkpoint and replay
an event around an interactive choice without duplicating money or random draws.

Expense handlers call payment first, then check survival/game completion before
speech and insurance. Insurance reimburses the requested expense through the
map's insurance company's funds; it is not an invented free cash credit. Physical
callbacks own their subsidiary injury, insurance and speech effects so they are
not applied twice. Auction animation/timer-dependent random draws remain part
of the auction migration's own equivalence boundary.

Integer additions, multiplication, inventories and counters retain original
fixed-width wrapping. The bank-hack binary32 fraction uses an exact integer
significand product before truncation. Price news uses bounded rational arithmetic
to retain an x87 boundary: binary64 `0.7` is below exact seven tenths, so multiplying
a positive price divisible by ten and truncating yields one less (2000→1399),
where a host binary64 intermediate can incorrectly round back to 1400.
Tax percentages are truncated at the original stage, before price-index scaling.

Stock tax valuation currently reproduces the original sequence of binary32
accumulator stores using binary64 intermediate arithmetic. Its extreme x87
double-rounding cases are **not yet proved equivalent**; a future exact floating
arithmetic oracle must cover these rather than treating successful ordinary tests
as evidence of bit identity across all values. The same care applies to newly
ported shared stock helpers in their own migration.

## Validation and remaining evidence

`events_tests` uses no commercial resources. It covers deck skipping/wrapping,
malformed decks, transport/map remapping, explicit missing-dependency gates,
original event-15 inconsistency, release markers, property ties, taxes and
truncation order, loan/bank-addition corrections, modifier branches, insurance
delegation, stock flags/company cash-flow semantics, physical redirection,
transport sale/point wrap, and human card cancellation. A dispatch coverage test
visits all 85 effective handlers with explicit mock boundaries; those mocks do
not claim to validate the financial/UI/stock/physical implementations themselves.
Address/undefined sanitizers and strict conversion warnings pass.

World tests independently compare expected save bytes for the new decks, cursors,
rosters, modifiers, stock holdings/NaN bit patterns/history, special actors,
company state, lease mode and facility tolls. Existing original saves remain
byte-exact under unchanged typed read/export. Full original-executable event
oracle vectors and integrated long-game coverage remain required evidence;
source translation and dispatch coverage alone do not establish full gameplay
equivalence or that every mandatory callback has been connected in the app.
