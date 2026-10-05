# 025 — Computer tool decisions

The portable `tools_ai` module replaces the thirteen tool predicates at
`0x420e9a..0x4221ae` and the computer branch in `rich4_ui_use_tool.asm`
(`0x447f82..0x448077`). Source: the GPL-3.0-or-later reconstruction at
[mytbk/rich4](https://github.com/mytbk/rich4), checked against the user's Rich4
v3.11 executable. Original archives and executable remain outside Git.

`choose(World, Random, actor, callbacks)` returns a typed tool, target, and
optional teleport destination. It never mutates the world or spends inventory;
the existing `tools::use` transaction does that. A missing required rendered
selection returns `supported=false`; it is not silently treated as “no tool.”
`evaluate` exposes one predicate including its original profile gate for
independent tests. There are no Windows APIs, pointer overlays, process-global
random generators, or SDL calls.

## Preserved decisions

| Tool | Original selection |
| --- | --- |
| Robot | Four-node forward preview; adverse gods, mines on own property, or roadblocks before expensive opponents' tolls. A preview fork aborts after retaining its RNG draws. |
| Roadblock | Buy a useful unowned lot/facility or stop at a tool shop; otherwise select the highest qualifying own street toll among visible roads on the reverse six-node path. |
| Mine | Reverse-path visible opponents' property; an occupied prison/hospital exit has priority. |
| Time bomb | Reverse-path visible roads, with the same confinement priority. |
| Motorcycle / car | Original transport restriction and one `rand()%4` test. |
| Missile | Highest positive signed hostility, else random available opponent; visible actor required, and reject own actor/property within the original projected central 200×200 square. |
| Remote die | Six-node forward preview, adverse-object/status filters, original cash thresholds, useful street completion or construction. Preserves strict comparisons, the 2.5× cost threshold, and highest-level tie behavior. |
| Construction robot | Highest current toll among eligible visible own land/facilities; facility maximum levels are `[1,5,5,1,5]`. |
| Time machine | Never considered by computer inventory visits. |
| Teleporter | Highest-level useful unowned visible property. The returned source is the current actor code; the destination is the associated road ID. |
| Construction vehicle | `rand()%15 <= profile`, except when already equipped. |
| Nuclear missile | Up to ten random enemy-property candidates, rejecting self in the projected view and applying original own/other property-count and level ratios. The ratios round through binary32 via the portable 64-significand numeric helper. |

The visit requires controller bits `&6` and profile byte 22 bit `2`. It enumerates
owned tool *types* in original order, excludes tool 10, randomizes the starting
index only above four types, and makes at most four attempts. With fewer than
four types it wraps and may try the same type repeatedly. Tool aggression bytes
minus profile byte 23 retain the original rejection and `rand()%3` gate. A
successful choice ends the visit; presentation never draws gameplay randomness.

Forward/reverse previews use the exact neighbor order and directed road masks.
The reverse version starts at `last_node_id` with `node_id` as previous. A single
candidate consumes no random value; multiple candidates do, even when the
calling tool subsequently rejects all branched paths.

## Projection and selection boundaries

The native implementation reuses the original lookup-table `board::project`.
Two selections are separate because their collision behavior differs:

* `0x409ef9`: all unoccupied road anchors in the current actor's 440×440 view,
  written in node-ID order, with later anchors overwriting earlier ones.
* `0x40a0b1`: current actor (unless absent), then owned land and facilities,
  projected about an explicit center, also with overwrite semantics. Radius 100
  scans `[120,320)` on each axis; radius −1 scans the entire view.

Both return nonzero pixels in row-major order. `nearby_instances(actor)` supplies
the existing renderer's `0x40a45c(-1)` anchor list, whose overlapping codes use
bitwise OR. It is required by missiles and construction robots. These are anchor
queries, not circular world-distance tests or sprite bounding boxes.

### Isolated original register defect

The actual executable's `0x4216ab` ownership helper does not initialize EDX. It
returns one for own property, but otherwise returns the caller's previous EDX.
Missile/nuclear code then tests equality to one. Therefore, an unrelated final
selection-buffer redraw register equal to one can misclassify the first enemy
property. The native API accepts an optional `ownership_carry` capture value;
ordinary native callers use zero. Nuclear loops subsequently overwrite EDX with
an address or a multiple of eight, which cannot equal one.

This register-dependent redraw corner is **not claimed equivalent** for arbitrary
original frames. It is deliberately isolated and documented instead of silently
presenting a host-pointer-dependent result as a portable ownership rule. Normal
property ownership and an explicitly supplied carry of one remain testable.

## Validation

`tools_ai_tests` checks the visit gates, repeated attempts with one held tool,
random start above four types, and explicit missing-renderer results. Its optional
CSV argument verifies each original predicate's acceptance, target, and final
Watcom RNG state, not merely a plausible action.

The independent `scripts/run_tools_ai_oracle.py` loads the original EXE with the
existing isolated Unicorn PE loader. Original preference dispatch, all thirteen
predicates, forward/reverse previews, both projection/scan routines, string
comparison, toll arithmetic, and libc RNG execute their actual original bytes.
Only rendered-anchor input, CRT thread storage, and selection-buffer redraw are
adapted. Redraw explicitly returns EDX=0, so this oracle does not claim to resolve
the register defect above. No desktop window, Wine process, audio device, or
original resource is modified. Private captures and SHA-256 manifests stay under
`.local/tools-ai-oracle/`.

```sh
.local/stocks-oracle/venv/bin/python scripts/run_tools_ai_oracle.py
build/native/tools_ai_tests .local/tools-ai-oracle/latest/predicates.csv
```

All 2,080 predicates (160 fixtures × 13) matched decisions, targets, and
final RNG state exactly, with ASan/UBSan enabled. Fixtures cover all eight camera
rotations, directed-path restrictions/forks, ownership/street combinations,
vehicle/status/profile gates, obstacles, confinement, and 32 accepted nuclear
missile decisions. Every tool that can be selected has both accepted and rejected
cases; the deliberately disabled time machine has no accepted cases.
