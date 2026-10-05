# Computer card selection

`cards_ai.cpp` migrates the original thirty-entry predicate table at `0x475328`
and dispatch `0x41e69e`, from `.local/upstream/asm/rich4_ai_use_card.asm`.
The hand traversal comes from `rich4_ui_use_card.asm:865` (`0x441d00`).
It returns a `CardUse`; the active-effects module applies that request.

## Traversal and random draws

Only controller flags containing bits 1/2 and profile byte `+22` with bit 0 set
enter this pass. Hands of at most eight cards start at slot zero; larger hands
consume one `rand()%card_count` draw and copy eight cyclic slots. A zero ends the
pass. The aggression byte from the original card table is compared with profile
byte `+23`: a difference of at least two skips the card, a difference of one
requires `rand()%3==0`, and a smaller difference proceeds directly.

The first successful predicate ends the visit, even if its effect subsequently
cancels. Original predicates for exchange-house, turn-around and all four
passive cards always return false. Winter sleep uses `rand()%4==0`; angel picks
an eligible street with one draw; random sleepwalk/prison target selection also
draws when only one candidate exists. Selecting the most hostile candidate
avoids that draw. Reconstruction of an owned basic facility draws its type from
1–4. All these draws use the session-owned `Random` stream.

## Predicate groups

| IDs | Original decision retained |
| --- | --- |
| 1/2 | Cash imbalance thresholds; enemy-first pair equalization, otherwise the last qualifying player slot. |
| 3/4 | Enemy/adjacent-own-street property desirability, strict available-cash comparison, and exchange price/level improvement. |
| 7/8 | Construction profile, chain-store street holdings, owned facility conversion or hostile facility downgrade; auction hostile high-level property. |
| 9/10 | Visible street aggregation: three improvable owned houses for angel; enemy counts/levels or opponent-only totals for devil. |
| 11/12 | Maximum level then base-price per owner; hostile facility priority, then land. Demolition adds chain-store, gas-station and obstacle cases. |
| 13 | Highest-priced aggressive enemy card; fallback considers aggression exactly two and retains first equal-price candidate. |
| 14 | Own upgrade opportunity or a visible opponent standing on the user's developed facility/company. |
| 15/16/17 | Original probability or target filters, hostility preference and revenge avoidance. |
| 22/23 | Harmful attached object or imminent bomb; summon avoids friendly object IDs and accepts a nearest friendly object ID. Object IDs are deliberately not replaced with kind IDs. |
| 24/25 | Market closure, suspension and daily limits; holding **cost basis** valuation; red maximizes own value, black prefers enemy holdings not in the user's portfolio. |
| 26 | Enemy cash above 30000×index, else the last nearby slot above 50000×index. |
| 27/28 | Street ownership ratio and construction totals or developed facility; seal inspects six predicted movement nodes. |
| 29 | Nearby eligible player must also be the global opponent leader in land/facility count. |
| 30 | Three-step acquisition opportunity for self, otherwise predicted three-step toll trap for a visible opponent. Junction paths are rejected. |

Shared stock valuation and price-limit arithmetic uses `numeric::Extended` and
`stocks::trend`, preserving original x87 truncation and stored binary32 values.
The tortoise land-toll calls always pass a street name, including chain-store
land, just as the original predicate does.

## Session contracts and original quirks

`nearby_instances` supplies original viewport scan order and combined actor
masks. `lookahead_nodes` implements `0x40b221`, returns its junction flag and
node list, and consumes routing random values only at actual junctions.
Card 28 requests six nodes; card 30 requests three for self and then opponents.
AI-generated targets bypass the human viewport filter, because the original
computer directly uses its computed parameter instead of the mouse picker.
Logical type/ownership validation remains in `apply_card`.

`CardAiMemory` explicitly retains the original three scratch parameters across
visits. In particular the isolated own level-two-or-higher stop-card branch
returns success without assigning parameter zero. The migration retains that
stale value rather than silently changing the target to self. A zero/invalid
target safely cancels without a modal player-choice request; malformed original
pointer accesses are not reproduced. This scratch is runtime state, not an
invented addition to the original save format.

The price-rise facility branch assigns the old ESI value to its comparison
score, rather than assigning the facility level. The migration explicitly
carries the hand traversal index/most recent land iteration index for this
source-visible behavior. This unusual branch deserves focused original-exe
oracle coverage in addition to source-derived tests.

## Validation

`cards_ai_tests.cpp` covers every predicate family, disabled/profile gates,
randomized hand start, first-card termination, exact random draw counts and
state, cost equality, hostility priority, maximum/tie selection, street
aggregation, reconstruction choice, persistent stop-card parameters, god/bomb
branch ordering, stock restrictions, six/three-step lookahead, and alliance
eligibility. It passes native AddressSanitizer and UndefinedBehaviorSanitizer.
Both card sources compile without `-Wall -Wextra -Wconversion
-Wsign-conversion` warnings. This suite provides source-derived verification;
it does not claim that a whole game or all predicates have already been
differentially replayed against the original executable.
