# Active card effects

This migration replaces the active card dispatch with typed C++ requests in
`cards.hpp`/`cards.cpp`. It operates on `World` and has no Win32, pointer overlays,
assembly, host clock, or global random generator. The computer's decision to
play a card is a separate migration; calling a card manually does not consume
computer-decision random numbers.

## Source and boundaries

The source of truth is `.local/upstream/asm/rich4_card_*.asm`, supplemented by the
readable `rich4_card_junfuka.c`, `rich4_card_junpinka.c`, and
`rich4_card_zhuanxiangka.c`. IDs and prices come from `rich4_card_table.c`.
`rich4_ui_use_tool.asm:300` (`0x445e4d`) provides target filters, including its
eight secondary-filter branches at `0x446337`–`0x4465b6`.

`legal_card_targets` separates actor slots, original property codes, stock
indices, and object IDs. A production caller supplies `nearby_instances` from
the original 440×440 selection viewport: these card masks do not set the
picker's map-pan bit. Omitting the callback is useful only for logical-filter
unit tests and does not establish original on-screen reachability. Combined
actor masks preserve every represented actor.

`CardCallbacks` delegates shared payment, prison, defense, hotel release,
auction, god/object, and stock rules to `GameSession`. Selection callbacks only
return choices; the card module performs inventory transfer. Callbacks may
suspend by throwing the session's decision exception. The session must restore
both World and RNG before replaying a multi-stage request. Missing required
callbacks are rejected before consuming the card. Cancelled/invalid requests
preserve the hand.

## Migrated effects

| IDs | Behavior retained |
| --- | --- |
| 1, 2 | Cash-only mean of all active players or a selected opponent; integer remainder disappears; losses update hostility. |
| 3 | Standing opponent property bought for `(base + level×construction)×index`; old owner receives bank credit; a configured lease is renewed. |
| 4, 5 | Same-type properties only. Exchange owner bytes, or construction type/level respectively. Lease dates stay with the original property. |
| 6 | Selected ordinary or special actor turns around through the shared movement rule. |
| 7 | Standing building reconstruction; chain conversion clamps land to level one; facility types 0/3 clamp to one. Cancel retains the card. |
| 8 | Standing property auction, with the card user as seller even when another player owns it. Unsold clears ownership and expiry, retaining construction. |
| 9, 10 | Angel/devil affect the entire matching Big5 street; a facility affects only itself. Angel does not upgrade an existing chain store. Devil retains ownership. |
| 11, 12 | Monster destroys opponent construction; demolition removes one level or a whole chain store, or removes a road obstacle of type 16/17/18. Hotel occupants use the shared release rule. |
| 13 | Explicit victim inventory selection transfers a card or stored tool. Equipped transport is not in the robbery inventory. Cancel retains the robbery card. |
| 14, 30 | Stop/tortoise allow self or special actors; self stop uses `0x80`, self tortoise uses two days, opponents three. |
| 15 | Five-day winter sleep excludes the user, inactive/absent players, and hotel guests; clears existing sleepwalk; affects the four selectable special actors. |
| 16, 17 | Sleepwalk/prison: hostility, immunity, frame, effect, then revenge only if the target was not redirected. Transport inventory, saved traffic/dice, and winter-day accounting preserve separate original branches. |
| 18–21 | Revenge, frame, free, and immunity are passive. The active API rejects them without consuming inventory. Shared rules trigger them when appropriate. |
| 22, 23 | Send-god removes attached bomb and harmful gods. Summon picks the nearest attachable object in scan order; original attachability includes harmful kinds and excludes 11/13/14. |
| 24, 25 | Red/black set the selected stock's movement flag and run the shared price update. Market closure is checked, while suspension and price limits do not exclude manual card targets. |
| 26 | Tax is one fifth of target cash. Free is tried before frame; frame uses mode 2 only above 2000; redirected tax is recomputed and credited to the user's bank. |
| 27, 28 | Street-wide `0x50` price rise or `0x51` seal; a sealed type-4 facility also clears its original byte `+0x1e`. |
| 29 | Seven-day mutual alliance, after dissolving both parties' previous alliances. |

## Observable original quirks

Purchase, auction, and black-card handlers push a binary64 argument to
`0x40df69`, which reads an **int32**. The migration passes the low 32 bits of the
binary64 representation to the hostility rule; it does not replace this ABI
accident with the apparently intended numerical amount. The shared
`numeric::Extended` helper now preserves each original x87 operation and final
binary64 spill, including the division-before-multiplication order. Its arithmetic
is independently tested against real x87 instructions (migration 023); the card
tests do not claim an exhaustive replay of every original handler.

Robbery's inventory helper returns a bare tool ID. Its caller consequently uses
that ID in the **card** price table for hostility, which is retained. Sleepwalk
revenge resets transport/dice but does not increase the attacker's accumulated
winter-sleep days. These are intentional source-derived distinctions.

Card speech and animations do not change the rules represented here. This
module does not create replacement random speech draws or timed animations.

## Validation

`cards_tests.cpp` exercises passive rejection, cancellation, off-screen target
filtering and combined actor masks, cash division, bank-directed purchase,
packed lease renewal, ownership/building-only swaps, unsold auction,
street/chain/facility effects, special actors, immunity and revenge ordering,
redirected prison, alliance replacement, card/tool robbery, nearest-god
selection, stock movement flags, and redirected tax.

The standalone suite passes AddressSanitizer and UndefinedBehaviorSanitizer on
the native host. These are source-derived branch and state-transition tests;
they are not an assertion that every card branch has already been replayed
against the original executable.
