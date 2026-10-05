# Gods, normal-player object encounters and the money spinner

The portable module follows mytbk/rich4 revision
`ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`: `asm/rich4_gods.asm`,
`asm/rich4_objects.asm`, object dispatch in `_rich4_player_move_one_step_done`
(`asm/rich4_player_core_actions.asm`), and the decimal spinner functions
`0x43f23e`/`0x43ef3e` in `asm/rich4.asm`.

## Shared state and attachment

God/object IDs are one-based references into `World.objects`, not necessarily the
object kind. The two kind-15 records therefore store different IDs in a player's
`god_info`. Types 1–10, 12 and 15 can attach; dog 11, tool gift 13 and points gift
14 cannot. The public `attach` operation also supports summoning, while `touch`
expresses the different walking-collision rules.

Attachment first temporarily clears the incoming object's node, then detaches a
previous god, sets the player/object association, applies the exact signed-word
modifier tables with 16-bit wrap, and clears the source node's object byte if a
source node was supplied. Ordinary attachment lasts seven days; kind 15 lasts
thirteen. Detachment delegates complete object removal, modifier subtraction and
counterpart respawning to the session's common removal rule, with the original
absence-dependent goodbye speech.

| Kind | Attachment state effect |
| --- | --- |
| 1 | Selected three-digit amount paid by each other active player into this player's cash; stop when the game ends |
| 2 | Selected four-digit amount added directly to cash and cumulative income |
| 3 / 4 | One / two original supply-weighted random cards |
| 5 | Selected three-digit amount paid by this player into each other active player's bank |
| 6 | Selected four-digit amount paid into the public fund |
| 7 / 8 | Lose one random card / the original alternating-slot half of the hand |
| 9 / 10 / 12 | Attachment modifiers, followed by property effects at the original landing boundary |
| 15 | Convert equipped transport to inventory, sell all tools and cards, discard both returned point values |

The selected money amount is **not multiplied by the price index**. The income
speech thresholds do use that index. Payment callbacks preserve the existing
cash/bank/actual-spending/bankruptcy path rather than duplicating it here. Optional
voice callbacks receive the selected original category after the module has
consumed any required voice draw. No UI callback may draw another category.

## Normal-player object encounters

Ordinary gods, dog 11 and gifts 13/14 trigger only when remaining movement is zero.
Walking past them does not collect or attach them. Type 15 has no walking-pickup
handler. A barrier (type 16) removes itself and stops movement on passage. A mine
(type 17) explodes only when stopping, then delegates injury and three-day hospital
confinement. A stopped player without a bomb can pick up type 18, which stores
its object ID, sets the original counter to 38 and clears the node object byte.

The dog is removed even when transport protects the player from injury. A tool
gift remains on the map when shared supply is empty; otherwise its original
weighted receive rule runs before removal. A points gift adds 500 with 16-bit
wrap. The timing countdown, transfer between colliding players, explosions,
special-actor behavior and object lifecycle outside this encounter operation
belong to their corresponding session/actor modules; they are not claimed as
implemented by decoding an object record.

## Property hooks

`after_property` is the original post-landing boundary for IDs 9, 10 and 12,
after the ordinary property action. Hotel occupants and inactive players do not
execute it. Angel upgrades delegate original `0x40b110`, including its building
choice and result bits; demon downgrades preserve the original owner-directed
hostility multiplier. The land god changes ownership and applies a lease expiry
only to previously unowned property.

The original land-god call has an ABI mistake: it calculates
`int32(base*price_index) * ((level+2)/5)` with x87 arithmetic, stores binary64, then
passes that storage to an **integer** hostility parameter. The portable version
uses the shared exact x87 module and passes the low 32 bits of the stored double,
not a guessed intended integer. Existing-owned property expiry stays unchanged.
`after_successful_purchase` is a separate hook for blessing gods 3/4, invoked
only by the normal successful purchase/upgrade path; it preserves the extra
upgrade and top-floor versus random speech behavior.

## Decimal money-spinner state machine

`MoneySpinner` replaces the original Windows drawing/message loop with explicit
frames. The caller supplies a nominal 30ms frame and the Stop input; the core
reads no clock. Its fields retain four half-digit counters, the original phase,
frame count, and digit-specific slowing counters. Every tenth frame while the
spinner is running refreshes all four half-digits with four original random
calls. Computer players stop on the fortieth frame. Subsequent phases stop the
ones, tens, hundreds, and (for four-digit results) thousands positions in the
original order, with the same increasing delay. The final display holds forty
frames. `auto_money` executes that same state machine for computer turns.

This preserves the meaningful original relationship between human stopping time,
selected amount and subsequent random state. It does not replace the spinner
with an arbitrary amount picker or reuse the unrelated property roulette table.
The renderer/audio can animate the state independently but must not manufacture
extra random draws.

## Validation

Synthetic sanitizer tests cover pre-mutation rejection for missing callbacks,
attachment association and exact node-byte masks, signed modifier wrap, payment
argument direction/flags, ignored sell-all proceeds, final-step eligibility,
barriers/mines/bombs, vehicle immunity, empty-supply gift retention, points wrap,
property/lease effects, the land-god ABI behavior, and top-floor speech RNG.

`scripts/run_gods_oracle.py --native-test /path/to/rich4-gods-tests` executes the
user's verified original EXE in an isolated Unicorn 2.1.4 process. The completed
comparison matched:

- 432 spinner cases: four god kinds, twelve seeds and nine stop schedules,
  including AI timing. Final amounts, completion frame counts and full random
  states all agree.
- 256 attachment cases: all original object IDs 1–16, four player slots and four
  seeds. Player modifiers, cash/income, object association/counter, exact node
  flags, card/tool inventory and supply, random state, and payment argument lists
  agree.

The adapter declares its boundaries in a hashed manifest. Rendering/audio/text
routines and DirectDraw methods are no-ops; a private CRT thread-context thunk
backs the **original** Watcom random routine. Clock/message adapters supply known
stop schedules. For the attachment-only capture, selected money is supplied at
the spinner callback boundary and payment arguments are recorded without running
bankruptcy; the spinner itself is tested separately above. These adapters do not
replace attachment, inventory, modifier, income, spinner or random instructions.
Flat x86 segment descriptors are installed so original segment-register stack
operations retain their actual width.

Both native fixture readers require every case exactly once in order and reject
empty/truncated comparisons. Captures and the user's executable remain outside
Git. This evidence establishes the listed boundaries, not a claim that every
original special-actor turn or complete native game has been reproduced.
