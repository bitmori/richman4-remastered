# Special actors: walking, pickups and owner benefits

`special_actors.hpp/.cpp` ports original actor slots 4 through 7. Slot 8 is the
robot and belongs to the core's separate nine-step action. All persisted data
uses `World::special_actors` and normal typed map/player inventories; there are
no host pointers, packed structure overlays, Windows APIs or assembly.

## Original boundaries

Sources are upstream `ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`:

- `rich4.asm`, `0x418f93..0x419033`: inactive NPCs are skipped, active slots 4..7
  follow normal players, before the new-day operation.
- `rich4_player_core_actions.asm`, `0x41c84f` / NPC branch `0x41ce39`: clear an
  existing high bit in sleep, sleepwalk, stop and tortoise counters; decrement
  nonzero counters and mark an expiring final turn with `0x80`.
- `rich4_player_utils.asm`, `0x40dd1f`: stopped NPCs do not move, tortoise means
  one step without a roll, otherwise one original RNG call gives `rand()%9+2`.
- `rich4.asm`, `0x40c05c` / NPC branch `0x40c489`: forward neighbors in table
  order exclude the previous node and blocked direction bits 30..27. Choose
  one with a single RNG draw, even when there is only one candidate. With none,
  return to the previous node without consuming RNG.
- `rich4_player_core_actions.asm`, `0x41b42d`: object handling and
  `0x41c164..0x41c844`: special-role benefits and return-home checks.

`turn(world, random, slot, callbacks)` owns these active NPC counters and a
complete turn. It never changes `header.current_player`, so core callbacks must
use their explicit actor/payer arguments. `move_one_step` and `interact` expose
smaller boundaries for deterministic checks. Map occupancy uses bits 12..15.
Movement state reaches the original node endpoint; frame interpolation, camera
pauses and sound playback are presentation work. `after_step` exposes each
completed move; structured effects expose actual victims, amounts and item IDs.

## Role and collision rules preserved

Slot 4 picks up the gift, 500 points, barriers, mines and time bombs for its
zero-based owner while passing. A gift is removed even if the supply is empty.
Inventory limits and supply byte arithmetic use the already migrated inventory
rules. Gift prize speech consumes its original random draw when appropriate.
Slot 4 takes half the points of the lowest-numbered other normal occupant.
The owner receives the points with original 16-bit wrapping.

Slot 5 takes one random card from that same lowest-occupant rule and gives it to
its owner. At an ATM (node flags low byte 14), it requests **20%** of every other
active player's bank balance, using original x87 arithmetic and payment flags
5 (bank first, recipient cash). An inactive lowest occupant does not cause a
search for a later occupant. Sleepwalking suppresses pickups and these benefits.

On its final node, slot 6 requests another owner's same-street base land prices
summed in original Big5 byte equality, multiplied by price index; for a facility,
it uses that facility's base price. Slot 7 requests the last collected land or
facility toll (the legacy `removed_building` field), or the commercial balance
with original encoded company payer `company ID + 100`. These use payment flags
0. Signed argument bits and 32-bit multiplication wrapping are preserved.

Barriers stop slots 5..7 immediately. Mines hospitalize them only on the final
step. The dog hospitalizes any special actor that stops on it. Confinement takes
three days and calls the special-actor confinement path without normal-player
injury. The first matching home marker arms flag `0x80`; the next encounter
returns the actor to prison/hospital with a zero-day callback and stops movement.
Original released actors already have that marker armed.

There is no reciprocal theft when a **normal** player walks into a stationary
NPC: the original routine reads normal occupancy `0x0f00`, and returns directly
at `0x41c164` when current player is less than 4. No invented reverse collision
handler is added.

## Callback and malformed-input boundaries

Payment, bankruptcy, full object removal/respawn and confinement remain core
callbacks. Their exact arguments and order are exposed, not replaced with local
approximations. Missing required callbacks return `supported=false`. As with
other event adapters, a caller that suspends an operation should retain its
World/RNG checkpoint; a later missing callback can follow earlier side effects.
Presentation callbacks are optional, but their absence never suppresses required
RNG calls. Invalid actor slots, owner indices, object/property references or
absent forward-and-return routes fail explicitly rather than indexing invalid
original memory.

## Validation

Synthetic tests cover final-day status semantics, inactive NPC skipping, exact
route RNG, backtracking, occupancy, complete turns, pickup/removal ordering,
sleepwalk suppression, lowest-occupant ties, point overflow, random card theft,
ATM flags/rates, signed toll/company values, Big5 street groups and home returns.
They pass with address/undefined sanitizers and strict conversion warnings.

`scripts/run_special_actors_oracle.py` executes the user's verified original
v3.11 EXE in a 32-bit CPU emulator against independently initialized synthetic
worlds. It captures 1,024 status cases (all 256 counter bytes, all four actors),
256 routes (all blocked-link masks across actors and seeds), and 1,792 collision
cases across 28 scenarios, stopped/passing and awake/sleepwalking states. Native
comparison matches every state, RNG word and payment/removal/confinement argument.
The comparison requires every scheduled case exactly once and rejects empty,
truncated, duplicate, extra or malformed rows.

The capture adapter stubs rendering, resource display and sound, records finance,
removal and confinement calls without applying their effects, and preserves
original instruction execution for the NPC handlers, inventories and RNG. This
isolates the migrated calling rules; it does **not** claim to verify callback
implementations or a complete game. The manifest records the original EXE hash,
adapter hash, stubs, boundaries, self-check and all output hashes. Captures and
commercial bytes remain in ignored `.local/special-actors-oracle/`.

Example local comparison:

```sh
.local/stocks-oracle/venv/bin/python scripts/run_special_actors_oracle.py --native-test /tmp/rich4-special-actors-tests
```
