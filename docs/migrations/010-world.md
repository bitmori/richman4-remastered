# Portable runtime world and original-format state export

`World` is the mutable state shared by the portable turn engine and the renderer's
snapshot adapter. It translates the original save/map records into ordinary C++20
values. It has no Win32 dependency, serialized host pointers, structure packing,
32-bit host assumption, or process-global state. This layer alone does not make a
game playable and does not establish equivalence of the complete original game.

## Sources and representation

The sources are mytbk/rich4 revision
`ec7d70e693059d1e781d9d1cc2ad7f36d360d39e`, principally
`asm/rich4_load_map.asm`, `asm/rich4_map.h`, `asm/rich4_player_info.h`,
`asm/rich4_save_files.asm`, `asm/rich4_new_game.asm`,
`asm/rich4_characters.c`, `asm/rich4_node_utils.asm` and the player setup
routine `0x40829d` in `asm/rich4.asm`. The original machine code remains the
authority when a recovered structure's field label conflicts with its use.

`World::from_save` accepts the existing lossless `SaveFile` codec. The runtime
owns a private byte-for-byte copy of that save as the backing for unidentified
fields. `header`, `players`, the five map tables, `card_supply`, and `tool_supply`
are the mutable, typed state; no game rule should maintain a second copy of
these values. Player indices start at zero; owner codes are player index plus
one. Map vectors retain the original index-zero sentinel, so an original record
ID is also its vector index. Node positions are signed map coordinates, not grid
indices. Player and property coordinates retain their serialized unsigned bits.

The decoded odd-numbered `MAP.MKF` entries contain a 40-byte header followed by
five tables. Each header pair gives a count excluding its sentinel and a byte
offset relative to the map block. The row strides are 40, 52, 56, 52 and 28 bytes.
`map_summary()` exposes those exact counts, offsets and strides. All table bounds
and overlaps are checked by `SaveFile`; no absolute original address is used.

| Table | Migrated fields and offsets within a record |
| --- | --- |
| Node, 40 bytes | signed x/y at 0/2; four neighbor IDs at 24; event/property code at 32; ground sprite marker at 34; raw flags at 36 |
| Land, 52 bytes | x/y at 0/2; encoded street name at 4; temporary state at 23; chain store at 24; owner at 25; level at 26; direction at 27; base land price at 28; price per building level at 30; six level tolls at 32; removed-building state at 44; expiration date at 48 |
| Facility, 56 bytes | x/y at 0/2; encoded name at 4; type/owner/level/direction/temporary state at 24–28; base price at 34; level price at 36; removed-building state at 48; expiration date at 52 |
| Commercial, 52 bytes | x/y at 0/2; owner at 24; commerce type at 26; direction at 27; sprite index at 32; toll at 34 |
| Landscape, 28 bytes | x/y at 0/2; direction at 24; sprite index at 26; remaining fields retain raw backing |

The recovered land structure labels the two price words in the opposite order.
The original purchase expression is `price_index * (word[0x1c] + level *
word[0x1e])`; the portable names follow this actual behavior. Event/property
codes and node flags remain explicit original integers; this layer does not
invent enum meanings or discard unfamiliar event codes. Street names keep their
original encoded bytes, normally Big5. Reading is bounded to the 16-byte field;
an unchanged name preserves even unusual nonzero padding or a missing terminator.
A changed name must fit 15 non-NUL bytes and receives NUL padding.

Players reuse the previously decoded `PlayerState` fields. `PlayerStatus` adds
the cash ratio at byte 25, named day counters at 50–57 and 59–62, god state at 63,
ally at 65, winter-sleep total at 66, four hostility words at 76–91, accumulated
spending/income at 92/96, and four explicitly numbered flag bytes at 100–103.
The old header's six-element hostility array overlaps the last two accounting
words; the new type does not expose aliases for the same storage. Unidentified
player fields and legacy name pointers stay opaque in the backing bytes.

## Editing and exporting

`export_save()` patches known fields into a copy of the original byte stream.
No original file is opened or overwritten by this API. An unchanged loaded world
exports exactly the original bytes, including raw regions, pointer-shaped values,
name padding, historical player/map snapshots, and trailing data. Runtime changes
do not rewrite those historical snapshots. Table resizing, inconsistent record
IDs, player-slot changes, inconsistent header counts, and invalid changed names
are rejected instead of silently corrupting the layout.

Export returns bytes, not a native C++ object dump. Each integer is written
explicitly in little-endian order. Signed monetary values use their fixed-width
two's-complement bit patterns. The result can be decoded again with `SaveFile`.
This is a state serialization boundary; it does not execute original save-button
UI, stock bookkeeping, snapshot/time-machine updates or related game rules.

`clear_bankrupt_player_fields(slot)` provides one narrowly defined raw-state
mutation required by the original bankruptcy routine: clear player record bytes
28 through 103, including unidentified fields, and synchronize their typed values.
It retains the profile bytes (including cash ratio at 25), controller, separate
inventories and historical snapshots. The turn engine performs those additional
bankruptcy actions explicitly. Tests assert the exact zeroed range, rather than
assuming clearing only the interpreted cash/status fields would be sufficient.

## Fresh worlds

`World::from_map_resource` takes one already decompressed map metadata entry.
`NewGameOptions` selects the date, map/stage IDs, initial fund, seed and player
characters/controllers/transport. The application supplies the current local
date; the core does not query a clock. Defaults are map/stage zero, initial fund
200000, character 10 controlled by the human and characters 3, 0 and 6 controlled
by the computer. Original map templates have unowned level-zero properties; their
record bytes are retained, including maps with zero ordinary lands.

Character colors, sex, AI cash ratios and the still-uninterpreted profile bytes
22–26 come from the recovered character profile table. Human cash starts at half
the initial fund; computer cash uses its profile ratio, with the remainder in
the bank. The original initial tool IDs 1, 2, 3, 4, 8 and 9 are granted with the
already migrated inventory rules; equipped transport adjusts its shared supply.
Card and tool supply constants follow the recovered new-game tables.

An option's `start_node == 0` leaves that player's node, previous node and x/y at
zero for the turn engine to spawn on its first turn. This preserves the original
placement timing: routine `0x40829d` chooses a free connected node using one
random draw, chooses a nonzero neighbor using another, sets direction and marks
occupancy. The factory consumes no spawn draws for these deferred players. An
explicit nonzero start is a caller-selected setup option: it sets coordinates,
chooses a linked previous node, computes direction and sets the player's occupancy
bit immediately. `has_initial_random_state()` distinguishes a fresh RNG state
from an original save, which does not store Watcom's PRNG state; seed zero is valid.

Fresh worlds serialize the complete legacy-shaped layout, including all required
snapshot regions. Unknown state and historical snapshots begin at zero; original
name/stock pointers are not fabricated. This makes them loadable by the portable
codec and session, but is **not** a promise that a fresh portable save is already
safe to restore in the original Windows executable. Original stock initialization,
46 object records, placement of gods/objects (including eight RNG draws before
the first player), special players and their initialization side effects have not
been migrated here. Consequently this factory does not claim a seed-for-seed match
with the entire original new-game menu sequence. These omissions must be addressed
as those gameplay systems are migrated, rather than faking RNG consumption alone.

## Validation

The default `world_tests` target uses generated synthetic bytes only. It verifies
all five table layouts, original base/level price offsets, signed cash, status
and inventory changes, exact expected byte patches, unchanged unknown regions and
snapshots, name padding, invalid resizing/IDs, bounds and malformed links, fresh
player finance/tool setup, deferred spawn RNG behavior, explicit placement and
fresh save reload. It passes with address/undefined sanitizers and strict warnings.

Optional `--save PATH` and `--map PATH` arguments are repeatable. Local validation
read the user's `SAVE0.DAT` and `SAVE1.DAT` without changes and confirmed exact
world export equality. All eight decoded original metadata entries (1, 3, 5, 7,
9, 11, 13 and 15) passed fresh creation, complete map-byte preservation, export
and reload. Those decoded entries were produced by the original-executable asset
oracle; no commercial saves or assets are committed with the tests. This verifies
the state/data boundary, not the original game's complete rules or playability.
