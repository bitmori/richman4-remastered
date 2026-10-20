# Interactive mini-games: 企鵝挖寶 / 七彩氣球 / 喜從天降

Source: `.local/upstream/asm/rich4_small_games.asm` (5239 lines) and its caller
`rich4_player_core_actions.asm:2366–2383`. "SG:n" means a line in rich4_small_games.asm.
Assets were checked with the contact-sheet tool and the frame dump of `Panel.mkf`.
Native code: rules in `include/rich4/minigames.hpp` / `src/minigames.cpp` (portable, unit-tested),
drawing and touch input in `tools/screens/minigames.{hpp,cpp}`. The core hook is
`DecisionKind::mini_game` + `GameSession::submit_mini_game`.

## 1. Selection, the animation switch and the reward

`_rich4_handle_player_land_on_node` dispatches on the node kind (`flags & 0xff`, jump table
`ref_004197e9`):

| node kind | game | entry |
|---|---|---|
| 6 | 企鵝挖寶 (penguin treasure dig) | `_rich4_ui_game_penguin_treasure` SG:4223 |
| 7 | 七彩氣球 (balloon pop) | `_rich4_ui_game_balloon` SG:4429 |
| 8 | 喜從天降 (catch what the 財神 throws) | `_rich4_ui_game_xicongtianjiang` SG:4515 |

The caller adds the returned value to the player's points as a 16-bit word
(`add word [player+48], ax`, core_actions:2371). Each entry first checks:

- `player.controller (+21) == 1` (a human) **and** `cfg+1 != 0` (the Options "動畫過程" switch)
  → the interactive game; its score is the return value;
- otherwise the **closed mode** (`loc_00415457`, SG:4385–4427): `score = rand() % 20 + 50`, a box
  `sprintf(0x463797 "得點券%d點")` (half-width digits) via `fcn_00440cac(text, 2000)`, then the
  player says `_rich4_event_strings[character][rand() & 1]`. That is two `rand()` draws. The native
  core implements this in `land()` (`src/game.cpp`): `Announcement::mini_game_points` (the box,
  2000 ms) then the event-line speech. Computers always take this path, so it is the announcement
  seen most often.

The interactive branch shows no box and no speech after the game: after `_Wait_0402_Message`
returns it only frees the sprites and jumps to the epilogue `loc_004155ec` (return `[0x48bcec]`),
and the caller (core_actions:2366–2383) only does `add word [player+48], ax`. The game's own end
screen shows the big score (2 s hold, §3–§5), which is the only announcement on that path.

### 1.1 RNG

The original has one C runtime `rand()` stream (Watcom LCG; native `rich4::Random`). Every
`_libc_rand` call inside the mini-games draws from **that same stream** as the rules. So on the
interactive path the original:

1. does **not** make the two closed-mode draws;
2. makes N game-internal draws instead, where N depends on the player's timing (treasure placement,
   balloon spawns, the 財神's walk, the goblin, "?" balloons).

Native port (exact and deterministic):

- `GameSession::set_interactive_mini_games(bool)`. The default `false` is the closed mode.
  Computers and sleep-walkers always use the closed mode, as in the original.
- When it is `true` and a human lands on kind 6–8, the core draws nothing. It sets a pending
  `GameDecision{DecisionKind::mini_game}` whose `mini_game` holds `{game, character,
  seed = random.state()}`. The phase is `awaiting_decision`, so saving is refused and the clock stops.
- The app plays the game on a `Random` seeded from `seed`. It counts every draw and finishes with
  `submit_mini_game(score, draws)`. The core advances its own stream by exactly `draws` calls,
  which reproduces the original's state after the game. It then adds `score` as a 16-bit word,
  emits `points_received` ("小游戏获得N点") with no speech, and finishes the turn.
- `respond()` refuses a mini-game decision, so a stray Enter key cannot skip it.
- The headless replay harness and the acceptance fixtures never enable the switch. Their oracle
  streams are unchanged.

## 2. Shared skeleton

Each game loads its assets and calls `_rich4_init_sound_effect_info(preset table)`. It pushes music
`fcn_004549cf(n)` (MIDI n+1; the native TODO is to switch through `Audio::push_music`/`pop_music`)
and runs a modal window procedure on a Windows timer:

| | penguin | balloon | 喜從天降 |
|---|---|---|---|
| procedure | `fcn_00414858` | `fcn_00414bbc` | `fcn_00414fcd` |
| timer | 100 ms | 100 ms | 50 ms |
| play time (`bd2c`) | 150 ticks = 15 s | 150 ticks = 15 s | 360 ticks = 18 s |
| delay before READY | 10 ticks from start (treasures shown) | 5 ticks after first paint | 10 ticks after first paint |
| music | MIDI13 (`fcn_004549cf(0xc)`) | MIDI12 (0xb) | MIDI11 (0xa) |
| sound presets (Effect.mkf) | 0x475057: 11 walk(loop), 12 dig, 13 happy(loop), 14 sad(loop), 16/17/18 treasure, 15 bomb | 0x47509f: 19 rise, 20 miss, 21 pop | 0x4750bf: 22 goblin throw, 23 (unused), 24 fuse (loop), 15 bomb |

After the delay, message 0x405 plays the **READY/GO** FLC (Panel 78: 640×480, 20 frames × 114 ms,
an index-0-transparent overlay at (0,0)). Play starts after it.

Shared HUD (bottom bar, y = 421; LCD digits are Panel 79 frames 0–9 at 15×28, drawn opaque):
- time: `sprintf("%03d", ticks)` at x = 49, 69, 94 followed by a constant '0' at x = 114. The
  game shows tenths, e.g. "15|00". 喜從天降 shows `ticks/2`.
- The end screen (`fcn_00414789`) shows the score with `sprintf("%d")` in big balloon digits
  (Panel 79 frames 10–19). Each glyph is anchored at y = 150, the first at x = 353 − len·33, with
  a 66 px step.

## 3. 企鵝挖寶 (Panel 80–90)

Assets: Panel 80 has frames 0 (ice field + HUD), 1 (penguin idle), 2 (blackened penguin), 3 (igloo,
redrawn at (320,225) when the penguin stands on a cell flagged 0x10, i.e. behind it), 4–8 (treasure
markers for kinds 1–5 in the preview) and 9 (dug hole). Panel 81 is a raw 640×480 byte hit map
(`v`: col = v%9, row = v/9; 0 = none). 82 is walking (8 directions × 4); 83 is digging (8 × 4);
84 is happy (8 frames); 85 is sad (6). 86–90 are the treasure pop-ups of kinds 1–5 (6 frames each:
explosion, coin, pink gem, blue gem, diamond).

Board: the 9×9 table `ref_00474d7c` (`{x, y, flags}` per cell). There are 64 valid cells; (4,4) is
the igloo. The penguin starts at col 2, row 6.

Placement `fcn_00412014`: kinds 1..5 get {3, 12, 3, 9, 1} cells (bomb, coin, pink, blue, diamond).
For each treasure, `i = rand()*free >> 15` (`free` starts at 64 and drops by one per placement)
picks the i-th free cell in row-major order. That is 28 draws.

Flow:
- Preview: background + markers (Panel 80:kind+3 at each cell) for 10 ticks. The markers stay
  visible during READY/GO; then they are hidden.
- Tap (only while idle, before time-out): look up the hit map and run `fcn_0041211c`. That starts
  a DDA walk toward the cell (major axis one cell per step, 16.16 fixed point). When the next cell
  is invalid it slides along the major axis and re-aims at the following cell (`fcn_00412287`). The
  walk loop plays Effect 11.
- Walk: 4 ticks per cell, interpolated `(next−cur)>>2 · sub`. The direction is set at sub 0:
  dx>0 → 3−dy, dx=0 → (dy>0 ? 1 : 5), dx<0 → (dy+7)&7.
- Arrival: stop 11, play 12, dig (4 ticks). At sub 2 the hole (80:9) is stamped into the
  background. At sub 3 the cell's treasure is taken and its pop-up (Panel 85+kind, 6 ticks) starts.
  Kind 1 (bomb) plays 15, the penguin turns black and the game ends early. Kinds 2–5 play
  16/17/17/18 and count.
- Score (recomputed by every HUD draw, `fcn_00413a4a`):
  **diamond×20 + pink×12 + blue×8 + coin×5** (max 188). HUD counts are drawn at x = 185/205
  (diamond), 276/296 (pink), 367/387 (blue), 458/478 (coin), with the score `%03d` at 549/569/589.
- Time-out: stop 11. score < 40 → sad (85, loop 14, 4 cycles); score > 55 → happy (84, loop 13,
  4 cycles); otherwise idle for 15 ticks. From that point the big score is shown. Then there is a
  20-tick (2 s) hold; a click shortens it to 1 tick.

## 4. 七彩氣球 (Panel 91)

Panel 91 has frame 0 (fun-town background + HUD), 1–9 (balloons "1"–"9", 44×141 for 1–6 and
36×116 for 7–9), 10 "×2", 11 "÷2", 12 "?" and 13 the pop burst. Kind k uses frame k+1.

Every tick (`fcn_00412f6f`), for each of 16 slots:
- empty, game running: `r = rand()%1000`. r<20 → kind r>>2 (1–5); r<28 → kind (27−r)/2+5
  (6–9); r<30 → `{×2,×2,÷2,÷2,÷2,÷2,÷2,?,?,?}[rand()%10]`. Lanes are x = 40 + 80·i. A lane is
  free when no balloon in it is lower than y 300. If any lane is free: Effect 19, lane
  `rand()%free`, y = 420.
- popping: count the 3-tick burst down, then free the slot.
- otherwise: unless frozen, rise by speed `{15,15,15,15,18,18,18,24,24,24,24,18}[kind]`, doubled
  or halved by the "?" speed modifier. The slot is freed once the sprite leaves the 640×387 field.

Tap: every non-popping balloon whose box (±22×±30, or ±18×±26 for kinds ≥ 6) contains the point
pops (Effect 21) and turns into the burst. Others play 20. Effects:
- 1–9: score += value, capped at 999;
- ×2: score ×2 (no cap);
- ÷2: score >>= 1;
- ?: clears the freeze and the speed modifier, then `rand()%6`: end now (time = 1) / freeze balloons
  20 ticks / double speed / half speed / score = 0 / score ×2.

Taps count until the end screen. At time-out spawning stops. Once no balloon is left, the end screen
holds 2 s. HUD score is `%04d` at x = 529/549/569/589.

## 5. 喜從天降 (Panel 92–111, Data 526)

Panel 92 is the raw 640×480 background. 93 is the 財神 (19 frames), drawn at (x, 126). 94 is the
bomb goblin (12 frames), drawn at (gx, 125). 95–99 are falling objects of kinds 0–4 (chest, sack,
gold bar, coin, bomb; 8 frames each). 100+character is the catcher (0–4 poses: 0 normal, 1–3 end
moods, 4 burnt; 5–14 walking left; 15–24 walking right), anchored at (x, 380). Data 526 is the
bomb explosion FLC (8 frames × 114 ms) at (x−55, 295).

Each tick (`fcn_00413248`):
- Objects (16 slots `{x, y, kind|frame<<4|vx<<8, vy}`): the perspective factor is
  `f = max(y−130,0)/250` (float). The object is drawn at `x + trunc(vx·f)`, scaled by
  `trunc(f·32768 + 32768)/65536` (half size at the top, full size at the catcher). The catch test
  uses that drawn x. While
  y < 130 it rises and falls: `vy += 2` (max 16), `y += vy`. After that it falls at
  `{24,18,15,12,15}[kind]`. It vanishes at y > 380.
- Catching happens only while the catcher has a walking direction. Once it has moved, the
  direction stays set when it stops. The test is the object point inside the current catcher
  frame's box. Treasures count. A bomb stops the fuse, plays 15 and the explosion, burns the
  catcher (pose 4) and ends the game.
- Catcher: it moves 10 px per tick toward the pointer x while farther than 8 px away.
- Goblin: while the 財神 walks right on the right half (or left on the left half), there is a 30%
  chance per tick (`rand()%10 >= 7`). The goblin appears at `rand()%140 + (財神 on the right ?
  160 : 360)`. At frame 8 it throws a bomb (Effect 22 + fuse loop 24).
- 財神: walks 110 ↔ 530 in 12 px steps of 5 frames. In each 5-step cycle he throws one treasure at
  step `rand()%5`. The treasure kind comes from `rand()%20`: <9 coin, <15 gold bar, <18 sack,
  else chest. Its horizontal drift is `trunc((x−320)/210·260 − (x−320))`. At the end of a cycle
  he turns: on the far half with `rand()%4 == 0`, or always at the edge.
- Score: **chest×10 + sack×5 + gold×3 + coin×1**. HUD counts are at 185/205 (chest), 276/296
  (sack), 367/387 (gold) and 458/478 (coin); the score `%03d` is at 549/569/589.
- Time-out: the 財神 faces front (frame 9) and stops throwing. When nothing is left falling (or the
  explosion has finished), the catcher takes its end pose: score <40 → 1, <50 → 2, <60 → 0, else 3.
  The original runs one more simulation step while redrawing, then shows the big score for 2 s.

## 6. Touch adaptation (Mac + phone)

The look, timing, rules and scoring are unchanged. The input changes:
- **Penguin**: tap a cell. A tap off the hit map snaps to the nearest valid cell within 40 px.
  The cells are small on a phone.
- **Balloon**: tap a balloon. The original boxes are kept.
- **喜從天降**: the catcher walks toward the finger's x while the finger is down; a tap sets a new
  target. The original warps the mouse cursor to the screen edges, so a mouse flick sends the
  catcher running to that side. That trick does not translate to touch, so the native port
  exposes the cursor x directly. Speed, dead zone and catch rules are unchanged.
- A tap during the final hold shortens it (penguin only, as in the original).
- Native-only option toggle: Options "動畫過程" now switches the interactive mini-games (cfg+1).
  The on mark is Data 3:9 at (98, 50) in the window (ui_options `ref_00474c22/26`). It defaults to
  on. Other cfg+1 effects (god/shop animations) are not tied to it yet.
- Dev: `rich4-portable --minigame penguin|balloon|catch` launches a game directly. With `--headless
  --output DIR` it auto-plays and writes start/mid/result frames.
