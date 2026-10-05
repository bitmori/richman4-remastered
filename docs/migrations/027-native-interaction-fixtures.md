# 027 — Synthetic native interaction fixtures

`rich4-native-fixture` prepares reproducible saves for exercising the native
inventory and map-target UI. These saves are **synthetic**, not captures of an
original game and not evidence of a complete or behavior-equivalent playthrough.
The generator uses the migrated `World` serializer; it does not execute Wine or
the original EXE.

## Generate and load

```sh
cmake --build build/native --target rich4-native-fixture rich4-native
build/native/rich4-native-fixture \
  --assets "game-assets" \
  --output .local/native-interaction-fixtures
build/native/rich4-native.app/Contents/MacOS/rich4-native \
  --assets "game-assets" \
  --load .local/native-interaction-fixtures/cards-01-15.DAT
```

The output directory must not already exist. Use a different private output
directory when regenerating. `--map 0..7` selects the original archive's eight
map resources; `--from-save FILE` instead uses an existing four-player save as
the map/state template. Both inputs remain read-only. Output beneath the asset
directory is rejected, and the input bytes are checked again after generation.

Each scenario produces a `.DAT`, `.DAT.rng`, and `.json`. The JSON records its
synthetic purpose, source path, seed, map, date, player inventories/positions and
finances, visible properties, constructed objects, and limitations. The RNG
sidecar uses the same `RICH4-RNG-1` format as the native app. Do not commit the
generated saves or manifests containing private source paths. The public recipe
in `tests/fixtures/native_interactions.json` contains only generated metadata.

## Scenarios

| Save | Human inventory | Starting property |
| --- | --- | --- |
| `cards-01-15.DAT` | Cards 1–15, three of each tool 1–13 | Opponent's level-2 land |
| `cards-16-30.DAT` | Cards 16–30, three of each tool 1–13 | Same neighborhood |
| `tools-01-13.DAT` | All 13 tools, cards 1–15 available for combinations | Same neighborhood |
| `cards-facility.DAT` | Cards 1–15 and all tools | Human's level-2 hotel |
| `bankruptcy-victory.DAT` | No cards, tools or stock holdings | Two-player endgame route |

The second-stage fourth map has no ordinary land, so its first three scenarios
also begin on a facility. An imported map with no usable facility omits the
facility scenario. A source must contain at least eight connected, dry nodes
in a suitable viewport; otherwise generation fails before creating output.

Player zero is human; the three AI opponents stand on separate nearby nodes.
The inventory-fixture human has 900,000 cash, 1,500,000 in the bank and 1,000 points. Other players
have 300,000 cash and the same bank balance/points. Date is Monday, 1998-06-15,
price index is one, and participants start walking with one die. Properties
have varied owners and levels; one visible property of each available kind is
vacated to allow property teleportation. All statuses are reset before adding
the explicit attached god/bomb state.

Constructed objects occupy their original pools: god 1 on the road, god 5
attached to the human, roadblock object 17, mine object 27 and attached bomb
object 37. The latter has eight remaining steps. Opponents have several active
and reactive cards. Two players hold stocks. Shared supplies remain available;
empty object slots retain their original fixed pool types for later respawns.
the synthetic inventories deliberately do not conserve the original finite
decks. This is recorded in every manifest.

The separate bankruptcy scenario uses an actual two-player save. The human has
1,000,000 cash and 2,000,000 in the bank; the AI has one cash, zero bank, zero
loans, no stocks, cards or tools. Both have five days of the original tortoise
status, so their next movement is one step. An existing pair of single-exit
route segments lets the human leave its own property and then forces the AI to
land there. Properties are human-owned and have a synthetic 50,000 toll. The
route links and event-node types are unchanged. `expected_endgame` records the
intended winner and loser in the manifest. This setup validates the native
bankruptcy/victory/save/reload flow once executed; generation alone does not.

## Interaction checks and evidence boundary

Use a fresh load for each independent action so earlier cards cannot remove or
invalidate a later action's target. Verify the inventory, select an active
card/tool, choose a map target or list item, confirm, and inspect the resulting
state. Exercise both selection paths: a successful right-side list click does
not establish that the map marker's hit box works. Overlapping map markers can
be disambiguated through the numbered target list. Cancelling must return to
the turn without consuming inventory.

Cards 18–21 are reactive; their presence should not make them manually usable.
Time machine needs a prior human turn snapshot in the running session, because
that runtime history is not carried by the native save. Facility cards need the
facility scenario; stock cards use the weekday fixture. Tool placement needs an
unoccupied node, while property teleportation needs the prepared vacant plot.
Random event outcomes, complete-game progression and original-rule equivalence
remain separate acceptance checks.

`tests/native_fixture_tests.py` constructs its own small ring map/save from
publicly authored bytes. It requires no original assets. Tests compare actual
serialized inventories, controllers, positions and finances against the JSON,
verify object pools and vacant targets, check deterministic output, and reject
existing/asset-contained output paths or malformed sources.

```sh
python3 tests/native_fixture_tests.py build/native/rich4-native-fixture
```

Validation in this migration: all five generator tests passed with the native
ASan/UBSan build; saves generated from all eight installed map resources and
every emitted save passed decode/re-encode byte equality. Those checks validate
fixture construction and persistence, not the card/tool UI actions themselves.

The bankruptcy fixture also passed native SDL execution in local artifact
`artifacts/native-playability/20261005-144528-88d79e10`: two dice actions, two
movement steps, one rent payment, then bankruptcy and match completion. Saving
and reloading retained the finished state with identical DAT bytes and RNG.
Both SDL drivers were `dummy`, so the test took no desktop input or audible
audio devices; there were no unsupported events. This is specifically a
synthetic endgame-flow result, not a naturally completed full game. That run's
return-to-menu transition was an automation assignment; a later run must use
the actual menu button before claiming coverage of that button.
