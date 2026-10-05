# Lottery tickets, drawing and settlement

The portable `rich4::lottery` module translates state-changing lottery rules from
mytbk/rich4 revision `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`, principally
`asm/rich4_ui_letou.asm` (`0x42ffd1`, `0x430ab5`, `0x430b0d`, and the computer
branch of `_rich4_ui_letou_bar_entry`). Bankruptcy cleanup follows
`asm/rich4_player_bankrupt.asm`; the fifteenth-day trigger follows
`asm/rich4_player_core_actions.asm`. No commercial assets are included.

## State and rules

`World::lottery_tickets` contains 36 owner bytes. Numbers displayed to players are
1 through 36; a stored zero is unsold, and nonzero is the player slot plus one.
The original array at `0x4990b8` is serialized at SAVE offset `0x26be`. The prize
pool is the existing `World::public_fund` (`0x499080`, SAVE `0x26ba`), also used by
other game payments. It is not a new independent lottery balance.

A ticket costs 1000 cash, without multiplying by the price index or using bank
funds. The original direct subtraction does not increase cumulative spending.
Human players may buy with exactly 1000 cash; the computer branch requires
strictly more than 1000. Computer purchase chooses one of the remaining numbers
in ascending order with one original random draw. No number or insufficient
cash means no purchase and no outcome draw. The original human message path
posts `0x406` with argument 3 after purchase, sets dialog state 5, and then exits;
one visit buys one ticket, rather than reopening selection automatically.

The monthly trigger is day 15. No drawing occurs without sold tickets. When any
single player owns more than ten tickets, one draw selects among **all sold
numbers**, not just that player's tickets. Otherwise one draw selects from all
36 numbers. Exactly ten tickets does not activate this branch. A winning ticket
credits the full public fund to its owner's cash and cumulative income, then
clears all tickets and the pool. A miss retains **both** tickets and pool.
Bankruptcy removes only that player's tickets, with no refund.

All monetary additions preserve the original low 32 bits explicitly, independent
of host integer width. Invalid owner bytes and out-of-range ticket numbers are
rejected before indexing; these errors do not grant a ticket or consume cash.

## API and integration boundary

`available_numbers`, `buy_ticket`, `buy_random_ticket`, `draw`,
`discard_player_tickets`, and `draw_due` operate on the shared `World`. The module
has no windows, timers, image loading, system clock or independent random state.
Human number selection and the next-day/landing/bankruptcy call sites belong to
`GameSession` and its UI. A draw returns whether it ran, selected number, optional
winner, amount and which selection branch was used.

The original Win32 show animation consumes additional timing-dependent random
numbers. This module implements the actual purchase/outcome/settlement state
operations; it does not reproduce those timer ticks or claim identical future
RNG state to every interactive Windows run. The session must document this
animation boundary rather than inserting an arbitrary number of fake draws.

## World extensions in this batch

The lossless world adapter now also interprets 46 object records at SAVE
`0x204`, stride 24: kind/direction at bytes 0/1, node at 2, remaining counter at 4,
attached player at 5, animation counters at 6/7, and four binary32 bit patterns at
8–23. Original object IDs are one-based; C++ indices are ID minus one. Unchanged
float bits, including NaN payloads, round-trip exactly. Fresh pools contain one
each of kinds 1–14, two of 15, and ten each of 16–18. Placement and node flags are
owned by the gameplay object rules; initializing the pool consumes no randomness.

Additional named fields retain exact offsets: camera rotation `0x2743`, round
counter `0x2692`, stock market index `0x26a2`, stock market movement bits `0x26a6`,
player active-card strategy byte 22, loan ratio byte 24, signed god luck modifier
word 68, and facility flag byte `0x1e`. These fields are serialized explicitly;
no legacy structure is overlaid on host memory. Bankruptcy still clears exactly
player bytes 28–103, retaining the profile bytes before that range.

## Validation and limits

Synthetic tests exercise the human/AI 1000-cash boundary, no bank fallback,
unindexed price, duplicate/invalid numbers, invalid saved owner, sold-out visits,
RNG consumption, winning and missing draws, ten/eleven-ticket threshold, per-owner
rather than total count, prize wrap, cumulative income, complete settlement,
selective bankruptcy cleanup and day-15 triggering. They pass under address and
undefined-behavior sanitizers with conversion warnings treated as errors.

World tests additionally assert exact changed bytes for every new field, preserve
all unrelated bytes and snapshots, and verify fresh object type counts. The
user's existing saves are checked read-only for unchanged byte round trips.
These are source-based state tests; this batch does not claim an original-EXE
lottery UI differential capture or proof that an entire native game is complete.
