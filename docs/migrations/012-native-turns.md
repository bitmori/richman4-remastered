# Native turn state machine

`GameSession` in `include/rich4/game.hpp` and `src/game.cpp` owns a portable `World` and Watcom random stream. It contains no Windows handles, x86 pointers, Wine calls, process hooks, or asset paths. The SDL application supplies elapsed milliseconds and player decisions. Saving uses `World::export_save()` and the lossless original save codec.

This is an incremental gameplay migration, not a claim that every original rule has been ported. The end-to-end original Wine evidence in migration 009 remains a separate baseline.

## Implemented rules and source evidence

| Behavior | Original source / entry point | Native behavior |
| --- | --- | --- |
| Dice | `rich4.asm`, `0x419572` | One Watcom `rand()` per die, `rand % 6 + 1`; sum determines remaining steps. Tortoise status moves one step without a die call. |
| Path choice | `rich4.asm`, `0x40c05c` | Examine the four neighbors in slot order; omit zero, the previous node, and edges blocked by `0x40000000 >> slot`. Consume one random value even with one candidate. With no candidates, return to the previous node without consuming randomness. Both human and computer players use this rule. |
| First appearance | `rich4.asm`, `0x40829d`; `node_utils.asm` | A player whose x coordinate is zero appears at its first turn. Eligible nodes have no `0x80ffff00` bits and have a neighbor. One random draw chooses the node, another chooses a nonzero previous neighbor. Occupancy bits are updated. |
| Occupancy | `0x40c05c` | Clear `0x100 << player` on the old node and set it on the new node. Pixel interpolation is presentation; it does not add random calls. |
| Ordinary land | `player_core_actions.asm`, landing dispatcher | The low byte of node flags selects the event; `type_and_index - 2000` selects ordinary land when the code is between 2000 and 4000. |
| Purchase | `player_core_actions.asm`, `0x41d7d4` | `price_index * (base_price + level * price_per_level)`. Human choice is explicit. A computer buys only when its remaining cash plus bank is strictly greater than `min(trunc(initial_fund * .05), 7000) * price_index`. |
| Upgrade | Ordinary land branch | The owner pays `price_index * price_per_level`; level increases by one, up to five. Chain stores do not take this branch. |
| Toll | Existing economy migration; `0x41d2c6`, `0x41d709` | Street and chain-store tolls use the migrated economy routine. Cash pays first, then bank. The recipient receives bank credit. Only the amount actually paid enters spending/income totals. Requested toll is retained in land offset `+0x2c`. |
| Toll exemptions | `0x41d2c6` callers / `0x41d709` | Owner alliance, owner god 15, owner unavailable statuses, and closed property are checked. Payer gods 1/2/5/6 halve, waive, multiply by 1.5, or double the amount. |
| ATM | `rich4_ui_bank.asm`, `0x436ef8` path | Arbitrary integer deposits and withdrawals; no invented denomination restriction. Bank refusal prevents both; frozen withdrawals still permit deposits. Passing a bank can open the ATM before the final step. |
| Computer ATM | `rich4_ui_bank.asm`, lines around 5134 | Use the character cash-ratio profile; multiply target by 1.5 on days 1–7 and .5 on days 26–31, clamp extreme targets to .9/.1. Rebalance only when the ratio differs by at least .25 or cash is zero. |
| Point tiles | Landing kinds 10/11/12 | Add 50/30/10 with 16-bit wrapping; kind 10 consumes one speech draw (category 0/1), kind 11 speaks category 2 without a draw, and kind 12 is silent. |
| Player rotation | `rich4.asm`, `0x418ebd` | Skip inactive ordinary players; advance the date after the last player's turn. |
| Status timers | `0x41c84f` | Timers decrement on that player's turn, not all together at midnight. The terminal `0x80` marker is retained. Sleep and absence skip action; stop still resolves the current landing; sleepwalking skips special tiles and cannot buy/upgrade. |
| Month boundary | Monthly callback `0x437e61` and `0x439bfa` | A player with no loan receives `trunc(bank * double(1.1))`; clear monthly spending/income and winter-sleep counters. This is not `bank += trunc(bank * .1)`. |
| Price index | `rich4_update_price_index.asm` | Average surviving players' wealth, divide by initial funds, and only raise the existing index. Wealth delegates to `stocks::player_wealth`, including the original float rounding at each stock addition. |
| Bankruptcy | `rich4_player_bankrupt.asm` | No forced sale to cover a toll: insufficient liquid funds immediately bankrupt the player and transfer only available cash/bank. Object detach, summoned-role confinement, stock/inventory liquidation and three distinct randomly selected property auctions are implemented when play continues. Buildings remain when ownership clears; the original checks game completion before liquidating. The sole human losing ends the game; the last surviving human wins. |

Additional integrated locations and decisions:

- Facilities use original base purchase/construction cost, upgrade table, maximum levels, and AI reserve checks. Hotel and department-store multipliers use the original twelve-cell roulette rows. The initial cursor consumes one draw; AI advances 51 cells then skips invalid cells. Human elapsed time advances the cursor and the Stop action performs twelve further advances. This preserves the stopping rule while replacing the original Win32 presentation.
- Prison/hospital visits offer thirty-point ordinary-player rescue and three-hundred-point special-role rescue. AI uses the original personality/coin-flip selection and its stricter affordability thresholds. A released ordinary player retains `0x80` until its next turn. Incarceration, hospitalization, disappearance, and removal/restore of node occupancy have explicit state mutations.
- Minigames use the original configuration with interactive minigames disabled: `rand % 20 + 50` points, followed by one happy-speech draw. The three interactive minigames themselves are not implemented.
- A card tile samples proportionally to remaining supply and uses the migrated full-hand replacement routine.
- News and fortune call migration 014's saved-deck handlers. Physical, payment, inventory-choice, auction, stock and speech callbacks are wired to the same world. A nested human choice rolls back all earlier mutations and random draws; replay with the recorded answers commits the event once. Pending choice positions use occurrence order, so repeated choices in three consecutive fortune events remain distinct.
- Shop and magic-house decisions call migration 017. A shop retains its finite shelf across multiple purchases/sales and credits its revenue on leaving. Magic requires category and effect choices and can suspend for nested fortune, construction, card or auction decisions.
- Auction decisions use migration 016. The current bidder can be another human; the UI must use the decision's player rather than assume the active turn's player. Free magic upgrades/downgrades follow `0x40b110`/`0x40ab4a`, including chain-store exceptions and release of all hotel guests when a facility is demolished.
- Event speech carries an already-selected `voice_category`. Audio plays that category without consuming gameplay randomness. Purchase, upgrade, toll, absent-days and event helpers preserve the known source selection thresholds.

The old C map header reverses the meaning of ordinary-land offsets `+0x1c` and `+0x1e`. The executable's purchase expression is authoritative: `+0x1c` is the base price, `+0x1e` the price per building level. The old name `removed_building` for offset `+0x2c` is also incomplete: the toll path stores its requested amount there.

## Application contract

The state machine exposes `human_ready`, `rolling`, `moving`, `awaiting_decision`, `advancing_turn`, and `finished`. `roll_dice()` applies only at human readiness. A decision includes its player, prompt, amount, and options; `respond()` takes an option index and, for bank transfers, the amount. Invalid or unaffordable input returns false without mutation.

`tick(unsigned milliseconds)` drives dice and movement animation. Dice and path logic are independent of tick size. Human roulette input deliberately depends on elapsed stopping time, as it did in the original. `MoveAnimation` exposes endpoints and elapsed time; the renderer must ignore unspawned players at zero coordinates. Events expose actual mutations for the UI and automation, including an explicit `unsupported_event` when a rule has not been migrated.

Saving is allowed only at settled human input or game completion. In-flight movement and unresolved decisions are rejected, rather than losing queued steps on reload. Original saves do not contain the Watcom random state; a loaded save therefore needs a session seed. A fresh world provides the initialization stream's state, including a valid zero seed.

## Validation

`tests/game_tests.cpp` uses synthetic, redistributable worlds. It verifies the original random-call count, blocked-edge/previous-node selection, purchase and upgrade amounts, payer/recipient bank semantics, actual-amount bankruptcy accounting, integer bank transfers, and save guards. Additional cases exercise facility purchasing/construction/roulette, fortune deck commits, nested card-selection rollback, and prison rescue. A four-player road-only simulation advances thirty days, including month interest. The simulation is repeated with 17 ms and 100000 ms ticks and must finish with identical nodes and random state. Tests run with strict warnings, AddressSanitizer, and UndefinedBehaviorSanitizer.

These tests validate native control flow and selected known rules. They do not substitute for an original-executable differential capture of an entire turn or for playable UI verification on the real map.

## Remaining rule boundaries

The original global random stream is also consumed by speech, cosmetic animation, stocks, and computer card/tool decisions. Daily processing reseeds it from `GetTickCount()`. This native session currently uses an explicit deterministic stream and does not reproduce every presentation draw or the original daily clock reseed. It must not be described as a complete seeded replay of the Windows program.

Active special actors' turn/collision rules, timed-bomb transfer/fuse progression, proactive computer card/tool decisions, computer stock strategy and special financing, overdue-loan collection, and alliance toll splitting remain to migrate. Preserved fields alone are not evidence that their rules execute. Physical event animation and cosmetic speech timing still differ from the Windows presentation.

Region-damage and loan-holiday callbacks are connected. The area picker preserves the original 440×440 projected-anchor OR buffer and row-major scan, using typed-world visibility; it does not yet consume original graph-null metadata, so that filtering boundary is not covered by a full-render-record differential. Lottery uses migration 019. Mandatory unported paths emit `unsupported_event`; continuing the notice does not prove the original rule ran. The UI acceptance runner must reject that count, and its completed-day count cannot prove coverage of all events or the remaining daily rules. Whole-game equivalence still requires broader original executable differential evidence. This module's current tests establish a working, sourced subset and protect its progression and save behavior.


## Integrated native actions and daily systems

The next milestone connects ordinary and primary stock trading, original daily stock updates/quotas, lottery purchases and fifteenth-day drawing, human and AI bank loans, holiday-adjusted due dates, and lease expiry. Stock market actions enforce closure, suspension, limit-up/limit-down and original quota rules. Borrowing credits the bank; repayment uses bank funds first and clears a human's due date only after complete repayment. The native session still keeps a deterministic random stream instead of the original midnight clock seed.

Commercial properties now resolve their original travel, insurance, periodic charge, transport, construction and primary-stock branches. Construction invokes the same building helper as magic and events. Ordinary and facility tolls support free-toll cards, frame cards and the original death-object redirection predicate (literal object IDs 14/15); nested decisions replay the whole payment transaction. Alliance street sharing remains a separate missing rule.

Human-ready `use_card`, `use_tool`, `dismount`, `buy_stock` and `sell_stock` APIs share the same world. Logical and viewport target selection live in the core. Decision options can carry a world coordinate so the application can show their actual locations. Card and tool cancellation leaves inventory unchanged. Active card effects delegate to migration 020; tools to migration 021. Computer action selection is not yet wired in this milestone.

Fresh setup continues the factory's 73 deck-shuffle draws, places object kinds 1/3/5/7/9/11/13/14 using the original free-node selector, then refreshes stock quotas before the first player turn. Movement synchronizes attached-object node IDs. Object encounters and property god effects call migration 022's original-derived god module. Human monetary gods use its decimal spinner: one tick per 30 ms, Stop input, then the original deceleration. Choice replay records both the final amount and final RNG state; rollback cannot duplicate gifts or random consumption.

The robot tool follows **nine**, not twenty, steps. `rich4_player_utils.asm` at `0x40deb9` writes 9 to the remaining-step global. Each route uses the original neighbor order and consumes one random draw even for a sole candidate. Every encountered object is removed through the original respawn/supply policy. The owner stays in place and regains control. The native robot currently resolves its nine steps synchronously and emits each road event; its animation remains an application presentation task.

Time-machine history is stored per human immediately before departure (`0x44808a`, called by `0x40dd1f`). Restore reinstates the world, date, price index, initial funds, inventory, market, decks and map state. It preserves current player, map/stage, player counts, lease setting, camera and RNG, which the original snapshot did not copy. The tool is consumed **after** restoration, matching `rich4_tool_shiguangji.asm`. History is session-local; a loaded save starts without a reconstructed native history.

Additional synthetic tests cover robot route count/object cleanup, remote dice without a die-face random draw, time-machine exclusion of RNG/camera, human god-spinner rollback, positive integer borrowing/repayment and holiday due dates, and free-toll card consumption. All game rule tests pass under AddressSanitizer/UndefinedBehaviorSanitizer and `-Wall -Wextra -Wconversion -Werror`. Original whole-turn equivalence remains unproven; module differential results and actual SDL playability evidence are recorded in their own migrations and runner reports.
