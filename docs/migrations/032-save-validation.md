# Validate a decoded save before installing a playable session

`validate_playable_world(const World&)` is a platform-neutral admission check.
It throws `std::invalid_argument` naming the table, index and field that failed.
It neither repairs state nor mutates the input. The application calls it before
constructing and installing a replacement `GameSession`, so a rejected import
cannot replace the running game or become its next autosave.

This boundary is deliberately separate from `SaveFile::decode` and
`World::from_save`. Those archival adapters must still decode and reproduce
unknown or invalid gameplay values without silently normalizing the original
file. The validator is a native runtime safety improvement, not a claimed port
of an original save-validation routine.

## Reproduced failure

Changing the character byte of a structurally valid save to 255 passes binary
decoding. Before this check, loading it installed the state, wrote an autosave,
then failed during the next render with `invalid board player sprite selection`.
The character byte is at `0x10 + player * 104 + 19`. A synthetic regression now
checks that binary decode/encode remains lossless while playable admission
rejects this exact mutation. Characters in other normal player slots are also
validated, including inactive players shown in account/name interfaces.

## Checked domains

- Header: one through four normal players, matching record count, current/human
  player bounds, map 0..3 and stage 0..1 before any narrowing cast, a valid date
  in the original calendar, positive price index, lease mode 0..5 and camera
  rotation 0..7. Map tables must retain their index-zero sentinel and nonzero
  records must retain their array IDs.
- Map/player references: every neighbor and player node, recognized land,
  facility and commercial node references, normal character/facing/dice values,
  card IDs, alliances, property owners, object IDs in node flags and attached
  player status. All map records are checked, including those outside the
  current camera viewport. Land/facility levels are at most five; constructed
  facilities must have a known type. Referenced, placed or attached objects
  require supported sprite kinds/facing and valid nodes/owners.
- Active special actors: facing, owner, current and previous nodes. An inactive
  actor's unused position/facing payload is not interpreted. Object slots with
  neither placement, attachment nor a node/player reference are likewise not
  interpreted as live objects.
- Persistent event/investment state: event deck IDs form their original
  permutations, cursor positions are in range, lottery owners exist, stock
  history cursor is 0..143, linked company/shareholder references exist, stock
  values and history are finite. Prices used by price adjustment remain in its
  0..10000 domain; stock quantities and cost bases are nonnegative. The numeric
  module retains its existing overflow behavior; no arbitrary balance ceiling
  is introduced.
- Active sale listings use supported kinds and valid stock/card/tool/property
  IDs. A listing can legitimately become stale before the market next prunes
  it, so ownership/quantity consistency is not an admission requirement.

The semantic sources are the native consumers and their documented original
boundaries: `board_renderer.cpp` sprite selection, `game.cpp` movement/calendar,
`special_actors.cpp`, `events.cpp`, `stocks.cpp` numeric/index domains, and
`trading.cpp` listing kinds. The original constants are independently exercised
by those modules' differential tests. No original commercial data is added here.

## Compatibility exceptions preserved

Flight uses upper bits of `traffic_method`; its valid values are not limited to
0..3. Expired status counters use `0x80`; disappearance also uses `0x40`.
`saved_direction == 15` means a direction must be restored after confinement.
Unspawned players and actors can have zero node/coordinates. Special actor flags
can contain `0x81` and `0x82`. None of these values is normalized or rejected.

The two unused bytes after the thirteen original tool counts remain raw data.
Inactive object motion bytes may contain arbitrary float bit patterns. Empty
sale slots retain old payload bytes; unused player sale tables and map sentinels
are not live records. An unconstructed facility can retain its old type byte.
Property facing follows the renderer's original low-three-bit mask. Chain-store
type means any nonzero value, not strictly one. Company stock index `0xff` is a
valid unquoted-company sentinel. Zero-valued stock samples are valid; negative
finite movement values are not mistaken for negative prices.

Signed account balances, legacy date expiry words and occupancy consistency are
not subjected to invented modern invariants. Original arithmetic can wrap;
calendar expiry uses packed addition; animations/confinement can temporarily
leave occupancy different from a simple coordinate reconstruction. The check
does not require graph reciprocity, reinterpret unknown node events, inspect
commercial sprite resources, or prove that a complete game can finish. Asset
availability, individual action preconditions and subsequent runtime mutations
still have their own checks.

## Tests and local evidence

The asset-free test constructs an original-layout map and fresh world, then
checks malformed references, remote property levels, all major indexed tables,
floating-point NaN/infinity, unknown event/card/listing IDs and the exact
character-255 reproduction. Valid legacy marker values and inactive payloads
are accepted. Successful and failed validation preserve every exported byte.
The test can additionally validate user-owned files read-only:

```sh
./build/native/world_validation_tests /path/to/SAVE1.DAT /path/to/Autosave.DAT
```

The standalone test passes with address/undefined sanitizers and strict
conversion warnings. Read-only validation also passed the supplied Wine
`SAVE0.DAT` and `SAVE1.DAT`, the recovery run's native `Autosave.DAT`, and all five
v4 card/tool/facility/bankruptcy interaction fixtures. Each was exported and
compared byte-for-byte with its input. These local files remain ignored and are
not part of the test fixture committed to Git.

A separate read-only review checked 76 non-deliberately-corrupted saves from
the original-process and native playability artifacts with zero rejections,
and confirmed that event selection advances cursors modulo their deck size.
The integrated ASan/UBSan suite passed all 43 tests; the real SDL recovery flow
also rejects both a truncated file and the full-sized character-255 mutation
while keeping the prior session available.
