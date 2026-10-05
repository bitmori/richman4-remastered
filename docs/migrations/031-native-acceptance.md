# 031 — Native acceptance matrix

`scripts/run_native_acceptance.py` runs 20 independent native gameplay cases,
collects their evidence, and rejects incomplete action coverage. It composes
`run_native_playability.run`; it does not substitute a state-only simulation
for the SDL application's event queue. Both SDL drivers are `dummy`, with no
desktop input or audible output.

This is a native flow acceptance test. It is **not** a comparison against the
original EXE for a complete game. The summary always records
`original_full_game_equivalence_verified: false`. Pure-function original-EXE
comparisons remain documented in their individual migrations.

**Paused on 2026-10-05 at the user's request.** The user found substantial missing
UI details and incomplete reproduction of the original interface. This runner
does not check visual fidelity or complete user experience. Do not resume tests
or development automatically; see [the handoff](../STATUS.md).

## Run

Build the current native application and fixture generator before starting.
Do not rebuild either executable or edit the runner scripts during a matrix;
the final integrity check rejects such a run.

```sh
cmake --build build/native --target rich4-native rich4-native-fixture
python3 scripts/run_native_acceptance.py \
  --binary build/native/rich4-native.app/Contents/MacOS/rich4-native \
  --fixture-binary build/native/rich4-native-fixture \
  --game "game-assets" \
  --output artifacts/native-acceptance/my-new-run \
  --workers 2 --turns 30 --timeout 180
```

The output directory must be new and outside the original asset directory.
Without `--output`, a timestamp and random suffix select a new directory below
`artifacts/native-acceptance`. `--workers` accepts one or two; `--timeout` is the
timeout in seconds for each child process, including fixture generation.
`--turns` retains the existing runner's name but measures game **days**. It sets
the natural-start and inventory-run duration. The directed cases always request
three days. A finished match may end before the requested duration. A shorter
override is recorded as `default_duration_or_longer: false` and is not evidence
of a 30-day test.

## Matrix and input provenance

| Cases | Count | Requested days | Input |
| --- | ---: | ---: | --- |
| All eight map resources | 8 | 30 by default | Natural new game; distinct seeds `42 + 97 * map_id` |
| Inventory coverage | 4 | 30 by default | Synthetic `cards-01-15`, `cards-16-30`, `tools-01-13`, `cards-facility` |
| Directed actions | 5 | 3 | Fresh synthetic loads: card 13 + tool 11, card 17, card 22, card 26, card 28 |
| Bankruptcy and human victory | 1 | 3 | Synthetic two-player `bankruptcy-victory` |
| Recovery and card cancellation | 1 | 3 | Fresh `cards-01-15`; card 6 and `--exercise-recovery` |
| Player trading | 1 | 3 | Synthetic `player-trading`; `--exercise-trading` |

The generator is invoked once with `--output NEW_DIR --assets GAME`. It emits
six master fixture sets, each containing DAT bytes, a native RNG sidecar and a
JSON manifest. All manifests must explicitly identify synthetic provenance.
Each of the twelve synthetic runs receives its own copy of all three files;
it never loads another run's output. Each run also has separate gameplay,
save-directory, report and log paths. At most two child games execute together.

The first five fixture recipes are described in
[027-native-interaction-fixtures.md](027-native-interaction-fixtures.md).
This migration adds `player-trading.DAT`: player zero is human with no active
offers, three copies of tool 5 and sufficient cash. Seller slot one owns two
copies and lists one tool 5 for 1,000 in offer slot zero. The buyer's count is
below the original cap of nine. Posting does not remove an item from the
seller's inventory. `expected_trading` records this setup in the manifest.
The UI replay must post a human offer, cancel it and buy the rival offer.
These deliberately configured inventories and finances are not natural starts.

## Pass conditions and evidence

Every case must pass the existing native runner gates: actual movement, dice,
rendering, SDL input, audio rendering and effects, save/load activity, no
unsupported events, and identical saved bytes and RNG after reload. Wine and
desktop input must be absent. The orchestrator repeats those checks and verifies
the requested map and automation seed. A failed child does not prevent the
remaining cases from collecting their evidence.

The combined result additionally requires:

- All 20 cases pass; all eight natural map IDs appear.
- Executed card IDs cover all 26 active cards, `1..17` and `22..30`; executed
  tools cover `1..13`. Reactive cards `18..21`, if observed, are reported
  separately and cannot fill an active-card gap. Presence in an inventory is
  insufficient: only executed IDs count.
- The synthetic endgame event log contains bankruptcy for slot one, a winner
  event for slot zero, and a finished match. A generic game-over notification
  does not count as victory. Its save/reload byte and RNG gates must also pass.
- Recovery reports real boolean success for invalid-load handling and batched
  cancellation. The native application's current invalid-load probe includes
  both truncated data and a full-size save with an invalid character ID; the
  runner succeeds only when both are rejected without losing the current game.
- The trading case reports integer counts of at least one for each of
  `offers_posted`, `offers_cancelled` and `offers_bought`.
- Each case retains nonempty before/after DAT files, PPM frames and an event
  log, with hashes matching its runner report. Before/after saves must differ.
- Original game-file contents, generated master fixtures, private fixture
  inputs, executable hashes and runner source hashes remain unchanged.

The original directory is checked recursively for every ordinary file,
including extensions outside the individual runner's protection list.
Symlink targets are recorded; linked files are hashed, but linked directories
are not traversed. This is content integrity checking, not a filesystem
sandbox. Save/output locations are kept outside the original directory.

`summary.json` is written atomically as cases finish and again at completion.
It records the plan, natural/synthetic labels, seeds, durations, process
commands, binary/source hashes, input manifests, evidence report hashes,
individual errors, aggregate missing-action lists and integrity checks.
The process exits zero only when `native_acceptance_verified` is true; failed
generation, a timeout, missing evidence or an incomplete coverage union yields
a failed machine-readable summary. Raw artifacts contain original game data
and local paths and belong in the ignored artifact directory, not Git.

## Validation of the test tooling

```sh
python3 tests/native_acceptance_tests.py
python3 tests/native_fixture_tests.py build/native/rich4-native-fixture
```

At introduction, all **11 orchestration tests** passed using mocked child
processes. They cover the 20-case plan, two-worker overlap, private inputs,
coverage failures despite individual success, timeout continuation, winner
verification, strict recovery/trading types, changed source files, mutated
private input, tampered evidence, failed or mislabeled generation, and output
boundaries. These tests never launch the game.

All **six fixture tests** also passed with an ASan/UBSan build of the actual
generator and a publicly authored ring-map save. The added test examines the
serialized 12-byte offer, empty human listing slots, inventory capacity, funds
and source immutability. These tests need no original game assets.

The initial 19-case execution is recorded in local artifact
`artifacts/native-acceptance/debug-asan-final-20261005/summary.json`.
All 19 individual runs passed, including eight natural starts of 30 days each,
all 13 tools, victory, recovery, trading and integrity checks. The combined
result correctly **failed**, because only 25 of 26 active cards were executed:
card 17 was missing. The twentieth case explicitly requests card 17 on a fresh
fixture. The coverage requirement is unchanged; an inventory containing a card
still cannot count as execution.

The expanded matrix's result must come from its own subsequent `summary.json`
produced by the integrated application. Passing the orchestration tests or
combining separate partial reports does not establish a passing full matrix.

## Result at pause

The standalone card 17 probe passed at
`artifacts/native-acceptance/card17-probe-20261005/report.json`.
The 20-case run at
`artifacts/native-acceptance/debug-asan-complete-20261005/summary.json`
completed only five cases (natural maps 0–4), all passed, before the user requested
a pause. Its dedicated runner and children were terminated and the retained
summary was marked `status: interrupted`, `interruption.partial: true`, and
`native_acceptance_verified: false`. No complete 20-case pass exists; the separate
probe is not merged into the previous failed summary. No background run remains.
