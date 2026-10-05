# Rich4 on-board animations and effects: research spec

Status: research only. Source is the upstream disassembly under `.local/upstream/asm`
(mytbk/rich4 `ec7d70e…`) plus decoded original assets. **UNVERIFIED** marks an item
I inferred but did not confirm against the running EXE. "asm file:line" means a line
in `.local/upstream/asm/<file>`.

Reference images (gitignored) are in `.local/ui-ref/effects/`:
`ov-523-534.png`, `ov-535-546.png`, `ov-547-558.png`, `ov-559-570.png`
(8 evenly spaced frames per FLC, one row per entry, ¼ scale), `ov-panel-4-6.png`
(dice), `panel-3-dice.png`, `data-396-415.png` (object sprites), plus per-entry
`flc-data-NNN.png`. They were made with the throw-away FLC decoder
`.local/ui-ref/effects/flc.py` / `overview.py`, which is not part of the build.

## 0. Key discovery: most effects are FLC movies, not SPR

`Data.mkf` holds 72 Autodesk FLC animations (`magic 0xAF12` at byte 4) that the SPR
contact-sheet tool skips. `Panel.mkf` holds 8 and `jump.mkf` holds 25. The native
port has no FLC decoder yet (`grep -ri af12 src include tools` finds nothing).

FLC layout as stored in MKF: there is a 128-byte header. Byte +6 holds the frame
count (u16), +8 the width, +10 the height, +12 the depth (8), and +16 the default
frame delay in ms (u32). Frame data starts at +0x80. That position may start with
an `0xF100` prefix chunk, which the game skips (`fcn_00450f04`, rich4.asm:~24990).
Frames use type `0xF1FA`. Across all three archives these chunk types occur: 4
(COLOR_256, 8-bit RGB), 7 (DELTA_FLC, word RLE), 15 (BYTE_RUN, first frame),
16 (COPY, once) and 18 (PSTAMP, ignore). Palette index 0 is transparent when an
FLC is played as an overlay. The decoded frames look correct with this rule, but
the exact compositing rule is **UNVERIFIED**.

### The common player `fcn_0045144f(flc, x, y, flags, sound)` (rich4.asm:25526)

Every FLC call site below uses this function. It blocks the game loop while it runs:

- **Position:** `(x, y)` is the screen top-left of the FLC. Board effects nearly
  always pass `(0, 40)`. A 440×440 FLC then covers exactly the board viewport
  `[0,40,440,480)`.
- **Sound:** if `sound != -1`, the function loads `Effect.mkf[sound]` and starts it
  after the first frame is decoded (`fcn_00454304` / `fcn_0045434f`,
  rich4_sound_effect.asm). The value is a direct Effect.mkf index.
- **Frame timing:** each frame waits until `frame_delay` ms have passed since the
  frame started. `frame_delay` is the FLC header value. If `flags` bits 4–7 are
  non-zero, the delay is that value × 10 ms instead. Playback ends after
  `frame_count` frames, with no looping.
- **`flags` bits (`fcn_00450ced`):**

  | bits | meaning |
  |---|---|
  | 0 | overlay on the current board instead of an opaque blit |
  | 1 | skippable by mouse-up or key-up |
  | 2 | unknown (**UNVERIFIED**) |
  | 16–23 `N` | **board-commit frame.** When the frame counter reaches `N`, the board is redrawn from the current world (`fcn_0040829d`). From then on the FLC composites over that new board. Before frame `N` it composites over the screen captured when playback started. `0xFF` means every frame. |
  | 24–30 `M` | play the sound preset by `fcn_00450cda` when frame `M` starts |
  | 31 | "whiten" mode (nuclear missile only): each frame also applies the board-mask fade `fcn_00455711(frame)` |

- **Rule-before-picture contract:** every call site changes the world *before* it
  plays the FLC. Until the commit frame, the player still sees the old board. The
  native app needs the same split (see §9).

Many callers then call `fcn_0045285e(ms)`, a plain sleep that still pumps messages.

### Object fly animation `_rich4_animate_object(obj, x0, y0, x1, y1, hold_ms)` (rich4_animate_object.asm)

1. Projects both world points with `fcn_00409a23`, the same function as
   `board::project`, using the current camera.
2. Computes `steps = trunc(sqrt(dx²+dy²) * 0.125 + 1)` from the screen deltas. That
   is one step per 8 px.
3. Moves linearly in screen space with float accumulation, rounding toward zero.
   Each step lasts at least 24 ms (`0x18`).
4. Draws the sprite's top-left at `screen − (origin_x, origin_y)`.
5. Sprite choice:
   - `obj > 0`: the object's sprite (DATA `0x18c + kind − 1`), with frame
     `(8 − camera_rotation + object.direction) & 7`.
   - `obj == 0`: the **card** sprite DATA 415 (`0x19f`), frame 0.
6. After the last step it sleeps `hold_ms`.
7. Only when `obj == 0` does it erase the sprite afterwards. A tool object stays
   because it is now on the map.

### Plot highlight pulse `fcn_00451985` (rich4.asm:25689)

Callers first mark the affected plots in the 440×440 word mask `ref_00474938` with
`fcn_00456c0a(mask, 0x2f440, plot_id, 0xffff)`. The pulse then runs 16 steps at 30 ms
each. Every step re-tints only the masked board pixels with
`fcn_004554fc(…, level)`. The levels come from `ref_00476380`:
`4,8,12,16,12,8,4,0,−4,−8,−12,−16,−12,−8,−4`, then a 16th byte, 3, read from the
next table. The function then holds for 400 ms. The positive and negative halves
are probably a brighten pass followed by a darken pass (**UNVERIFIED**, from table
`ref_00485d68`).

### Animation setting

`RICH4.CFG` byte 1 ("animation", `_global_rich4_cfg+1`) gates **only** some FLCs:
god appearances, the hospital ambulance, the prison police car, and some
shop/bank/lottery/mini-game visuals. Missiles, explosions, demolition, the monster
and dice always play.

## 1. Dice roll (every turn)

`fcn_00419572` (rich4.asm:12275) is called from the movement state machine
`fcn_0040d7c4` (rich4_player_utils.asm:712). That happens only after the roller's
**toss pose** (movement state 2, 9 frames at one per 80 ms game tick) has played on
the board; see main-screen.md §6.2.

- **Order:** the values are drawn **before** the animation: `rand()%6+1` for each die.
  With the remote dice, the chosen value is used and the count is forced to 1. The
  result does not depend on the animation.
- **Asset:** `Panel.mkf[4 + dice_count − 1]` (entries 4, 5 and 6 for 1, 2 and 3
  dice), loaded in `_rich4_init_panel_resources`, rich4.asm:11960.
  - FLC size 189×285, 36 frames.
  - The header delay of 14 ms is overridden.
- **Position:** this is a screen anchor, not the world.
  - `rel = (player.direction + 8 − camera_rotation) & 7`.
  - Top-left = `(136 + dx[rel], 48 + dy[rel])`.
  - `(dx, dy)` from `ref_00475224`: `(4,12) (12,12) (8,6) (−4,−6) (−12,−12) (−24,−12) (−20,−6) (−12,6)`.
  - The camera is centred on the player, so the dice fall onto the player near
    board centre (220,260).
- **Flags:** `0x1E000001 | (speed << 4)`, which means:
  - overlay;
  - frame delay = `speed × 10 ms`, where `speed = [5,3,2][cfg.game_speed]`. That
    gives **50, 30 or 20 ms per frame, or 1.8, 1.08 or 0.72 s in total**;
  - the dice sound plays at frame 30.
- **Sound:** sound preset `ref_0048235a`, Effect index **10**, is armed with
  `fcn_00450cda` and fires at frame 30. Right after the FLC the same sound is played
  again (rich4.asm:12340). Whether the sound is heard twice is **UNVERIFIED**.
- **Result faces:**
  - Afterwards each die `i` draws `Panel.mkf[3]` frame `i*6 + value − 1` at anchor
    `(X+85, Y+145)`, where `(X, Y)` is the FLC top-left.
  - The SPR origins put the dice side by side. Origins for dice 0, 1 and 2:
    `(84,−98)`, `(−11,−103)`, `(−68,−99)`. Sizes: 35×41, 30×36, 34×40.
  - Then there is a **500 ms** hold (`fcn_0045285e(0x1f4)`). The faces stay until
    the board is next redrawn.
- **Native:** `MainScreen` (tools/screens/main_screen.cpp) plays toss → FLC → faces
  hold on `dice_rolled`, holding the game clock through `dice_busy()`. The toss
  draws via `board::PlayerSprite::tossing`. Sound 10 plays at FLC frame 30.
  Headless replays skip the toss unless `--play-effects` is given.

## 2. Buying land and building or upgrading

`_rich4_handle_player_land_on_node` (rich4_player_core_actions.asm:322).

- **Normal purchase or upgrade has no construction animation.**
  - Purchase (core_actions:1051, also 1702): sets the owner, redraws the board
    (`fcn_0041d476(0,0,1)`), plays sound preset `ref_004823d2` = Effect **49**,
    updates the minimap (`fcn_0040a4e1`), runs the street-chain voice check
    (`fcn_0044f627`), and runs the god post-purchase hook `fcn_0040f8be`.
  - Upgrade (core_actions:439 and 1161): increments the level, redraws, and plays
    preset `ref_004823da` = Effect **50**.
  - The new building simply appears on the next board redraw.
- **Reaching level 5 (top floor):** `fcn_0040b0cd` (rich4.asm:4534). It redraws the
  board, then plays `Data.mkf[523]` (`0x20b`): 440×440 at (0,40), flags `1`
  (overlay), sound **90** (`0x5a`), 66 frames × 42 ms ≈ **2.8 s**. The animation is
  sparkles over the board centre, where the camera sits on the player's plot. The
  same function runs for the top floor via the angel card, angel god or robot
  worker.
- **Robot worker (機器工人) / free-build:**
  - Sources: `_rich4_use_tool_jiqigongren` (rich4_tool_jiqigongren.asm:65–101) and
    core_actions:1839 and 2036 (a land selected with `0x2090086`; this is probably a
    special-square free upgrade, **UNVERIFIED**).
  - Sequence: centre the camera on the plot (`fcn_0041d476(x,y,0)`), then upgrade
    (`fcn_0040b110`). After that it plays `Data.mkf[553]` (`0x229`) at (0,40) with
    flags `0x2C0001`, so the **board commit happens at frame 44**. Sound is **91**
    (`0x5b`), 68 × 57 ms ≈ **3.9 s**. This is the "building rises" movie. If the
    plot reached max level, `fcn_0040b0cd` follows.
  - Magic house uses 553 as well (rich4_magic_house.asm:421).

## 3. Money and rent feedback

- **Rent** (core_actions:492 and 1231):
  1. Play preset `ref_004823ea` = Effect **55**.
  2. Call `_rich4_check_pay_toll_to_owner` (core_actions:5310). It shows a message
     box (`fcn_00440cac(text, 1500)`): the board-area popup, 16 px white text with
     a dark outline, auto-dismissed after **1500 ms**. Then it plays the payer's
     voice (`fcn_0044f567`).
  3. **Street highlight:** when the owner holds more than one plot on the same street
     (strcmp on the street name; count `> 1` at core_actions:661), those plots are
     masked and **pulsed** with `fcn_00451985` before the toll message.
- I found no floating "−$xxx" number or animated cash count-up. The side panel is
  just redrawn (`fcn_0041d476` flag 2 → `fcn_00415d31`). **UNVERIFIED** whether any
  digit animation exists elsewhere. The decimal "money spinner" (doc 026) belongs
  to god kinds 1/2/5/6 and is not shown for rent.

## 4. Tool effects

| Tool | Visual sequence | Assets | Sound | asm |
|---|---|---|---|---|
| Roadblock 路障 (obj kind 16) | `animate_object(slot, player.xy → node.xy, hold 100)` | DATA 411, dir frames | Effect **33** after landing | rich4_tool_luzhang.asm:~60–80 |
| Mine 地雷 (kind 17) placing | same throw | DATA 412 | Effect **34** | rich4_tool_dilei.asm:46–78 |
| Time bomb 定時炸彈 (kind 18) placing | same throw | DATA 413 | Effect **10** | rich4_tool_dingshizhadan.asm:74 |
| Missile 飛彈 | Centre the camera on the target node and redraw. Apply the area damage (`fcn_0040ac7b(100,0x26,0,p)`). Play `Data[528]` at (0,40), flags `0x90001` (**commit at frame 9**): a missile drops in frames 0–8, then the explosion. Hospital (3 days) after. | Data 528, 440×440, 19 × 114 ms ≈ 2.2 s | **81** | rich4_tool_feidan.asm:67–97 |
| Nuclear missile 核子飛彈 | Same, but area −1. Plays `Data[530]` (mushroom cloud), flags `0x80090001`: commit at frame 9 **plus whiten mode** | Data 530, 26 × 114 ms ≈ 3.0 s | **83** | rich4_tool_hezifeidan.asm:74–96 |
| Mine triggered (stop on it) | Remove the object, then `fcn_0040cd07` (injured). Play `Data[525]` at (0,40), flags `0x30001` (commit at frame 3), then hospital 3 days | Data 525 explosion, 8 × 114 ms ≈ 0.9 s | **82** | core_actions:3329–3386 (`loc_0041be5f`) |
| Time bomb carried | Looped tick sound preset `ref_00482362` (Effect **32**) while carried. It stops on pass or explode (`fcn_004542e9`) | — | 32 | core_actions:2700–2730, 2795 |
| Time bomb explodes (counter 0) | Same explosion as the mine (`Data[525]`, `0x30001`, sound 82), then hospital **5** days | Data 525 | 82 | core_actions:2795–2845 |
| Roadblock hit | Remove the object, stop, redraw. No FLC | — | — | core_actions:3247 (`loc_0041bceb`) |
| Robot doll 機器娃娃 | No FLC. Special actor slot 8 walks the road (DATA `0x209` idle and `0x20a` walk, see doc 011/028) and removes objects it meets | — | — | rich4_tool_jiqiwawa.asm |
| Teleporter 傳送機 | No FLC. Two mouse selections, then an instant move: redraw and minimap update (`fcn_0040a4e1`, `fcn_0041d476`) | — | — | rich4_tool_chuansongji.asm:95–424 |
| Engineering truck 工程車 | Sprite swap only (traffic 3), redraw | — | — | rich4_tool_gongchengche.asm:60–65 |
| Remote dice 遙控骰子 | A picker panel from Panel.mkf `0x48` at (92,300). Then the dice roll in §1 with a fixed value | Panel 72 | — | rich4_tool_yaokongtouzi.asm:38–139 |
| Time machine 時光機 | Restore state, full redraw. No FLC | — | — | rich4_tool_shiguangji.asm |

Throw anchoring: both points are world points. `board::project` gives the screen
point. Draw at `screen − sprite.origin`, using the current camera, which is centred
on the active player. The target may be up to the selection range away.

Dog (object kind 11), core_actions:2867–2960. This is not a tool, but stopping on
the dog is common:

- With a vehicle, it plays `Data[552]` at (0,40), flags `0x10001` (commit at frame 1),
  sound **85**, 18 × 71 ms.
- On foot, it calls `fcn_0040cd07`, plays `Data[532]` (dust-cloud fight), flags
  `0x30001`, sound **93**, 38 × 114 ms ≈ 4.3 s, then hospital 3 days.

## 5. Card effects with visuals

Common pattern: for **AI players only** (`player+21 != 1`), the card sprite
DATA 415 flies from the player to the target with `animate_object(0, …, hold 100)`.
For a human the target was already chosen with the mouse, so nothing flies. Then
the camera centres on the target.

| Card | Extra visual | Assets / timing | Sound | asm |
|---|---|---|---|---|
| 拆除卡 demolish | Applies the downgrade first. Then `Data[529]` at (0,40), flags `0x260001` (**commit at frame 38**, so buildings collapse on screen), then a 500 ms hold | 529, 68 × 71 ms ≈ 4.8 s | **97** | rich4_card_chaichuka.asm:98–290 |
| 怪獸卡 monster | Destroys the building first (`fcn_0040ab4a(node,2)`). Then `Data[557]` at (0,40), flags `0x100001` (**commit at frame 16**), 500 ms hold | 557, 22 × 71 ms ≈ 1.6 s | **80** | rich4_card_guaishouka.asm:140–175 |
| 天使卡 angel | Card fly (AI). **Plot pulse** on the street. Upgrades. `fcn_0040b0cd` (Data 523 sparkle) if a plot hits level 5 | pulse ≈ 0.9 s | (upgrade 50 / 90) | rich4_card_tianshika.asm:120–180 |
| 惡魔卡 demon | Card fly (AI), downgrades, **plot pulse** (`fcn_00451985`), redraw | pulse | — | rich4_card_emoka.asm:141–212 |
| 換地卡 / 換屋卡 swap land / house | Card fly ×2, plot pulse ×2 | — | — | rich4_card_huandika.asm:157–376, rich4_card_huanwuka.asm:146–343 |
| Other targeted cards (查封, 查稅, 夢遊, 請神符, 同盟, 搶奪, 漲價, 陷害, 停留, 烏龜) | Card fly (AI only), then a message | DATA 415 | — | each `rich4_card_*.asm` (see list in §8) |

## 6. Gods and special actors appearing

`_rich4_attach_god` (rich4_gods.asm:472). It redraws the board with the camera on
the player, then dispatches on the object kind through the jump table
`ref_0040ea9b`. When cfg animation is on, it plays a 440×440 FLC at (0,40), flags
`1` (overlay, no commit), followed by the god's speech (`fcn_0040e2a2`).

| Kind | God | Data entry | Frames × delay | Sound | asm line |
|---|---|---|---|---|---|
| 1 | 小財神 (ingot boy) | 540 `0x21c` | 21 × 100 | 102 | 563–571 |
| 2 | 大財神 | 541 | 21 × 100 | 103 | 648–656 |
| 3 | 小福神 | 542 | 21 × 128 | 104 | 701–709 |
| 4 | 大福神 | 543 | 35 × 100 | 105 | 764–772 |
| 5 | 小窮神 | 544 | 28 × 100 | 106 | 844–852 |
| 6 | 大窮神 | 545 | 30 × 100 | 107 | 913–921 |
| 7 | 小衰神 | 546 | 14 × 100 | 108 | 966–974 |
| 8 | 大衰神 | 547 | 19 × 100 | 109 | 1036–1044 |
| 9 | 天使 | 548 | 16 × 100 | 110 | 1077–1085 |
| 10 | 惡魔 | 549 | 12 × 100 | 112 | 1107–1115 |
| 12 | 土地公 | 550 | 23 × 71 | 111 | 1131–1139 |
| 15 | 死神 | 551 | 15 × 100 | 113 | 1170–1178 |

The Chinese names follow the art and doc 026 effects (**UNVERIFIED** naming).
Kinds 11, 13 and 14 (dog, tool gift, points gift) have no appearance FLC.

Post-landing property hooks (`fcn_0040f381`, rich4_gods.asm:1196):

- **Angel:** upgrade plus Effect sound, and the 523 sparkle at level 5.
- **Demon** (rich4_gods.asm:1370–1420): downgrade (`fcn_0040ab4a(node,0)`), then
  `Data[526]` (110×110 smoke puff), flags `0x30001`, sound **95**,
  8 × 114 ms ≈ 0.9 s. It is anchored at the **building's screen anchor minus
  (55,55)**. That anchor comes from `fcn_0040b066`, which reads the screen position
  of the node from the last board draw list `ref_0048a850`, so the puff is centred
  on the building.

Other actor and event FLCs (all at (0,40), 440 wide):

| Event | Entry | Flags (commit) | Sound | asm | Notes |
|---|---|---|---|---|---|
| Sent to hospital | 524 ambulance, **440×74 at (0,210)** | `0x1E0001` (30) | 92 | rich4_hospital_utils.asm:92–107 | cfg-gated, 62 × 100 ms |
| Sent to prison | 538 police car | `0x120001` (18) | 94 | rich4_prison_utils.asm:92–107 | cfg-gated |
| UFO abduction | 533 | `0x1C0001` (28) | 84 | rich4_player_utils.asm:290 (`fcn_0040d375`; from fortune and landing) | |
| Airplane trip | 558 | `0x140001` (20) | 96 | rich4_player_utils.asm:377 (`fcn_0040d4e5`) | **UNVERIFIED** trigger |
| Player re-enters the board by parachute | 559 + character (12 colours) | `1` | none | rich4.asm:11494–11521 (`fcn_00418c55`, called rich4_main.asm:441) | Pending-return flag `0x475114`. The trigger events are **UNVERIFIED** |
| Bankrupt | 555 | `1` | 100 | rich4_player_bankrupt.asm:226–234 | |
| News: meteor or light dome | 531 | `0x180001` (24) | 86 | rich4_news.asm:392–400 | **UNVERIFIED** meaning |
| News: UFO | 539 | `0x200001` (32) | 84 | rich4_news.asm:581–589 | |
| News: explosion | 527 | `0x50001` (5) | 87 | rich4_news.asm:1991–1999 | |
| News: tornado ×2 | 534, 535 | `0x80001` (8) | 89 / 88 | rich4_news.asm:2567–2712 | |
| Special square marker | 537 (`0x219`) or 536 (`0x218`), ~30×40 at (204,180)/(208,180) | `1` | 98 / 99 | core_actions:2394–2515 | **UNVERIFIED** which squares |
| Christmas (Santa) | 554, 90 frames | ? | ? | rich4.asm:26116 (`fcn_00452444`) | **UNVERIFIED** |
| Board-overlay character reactions | Data 416–439 (12 characters × 2, ~100×100) | — | — | not traced | **UNVERIFIED** use |

## 7. jump.mkf

This is not an in-board asset. It is the new-game flow:

- Entries 0–7: raw 640×480 RGB555 backgrounds, used by `_rich4_init_new_game`
  (rich4_new_game.asm:3766).
- Entries 8–44: SPR/SMP character-selection art.
- After the map loads, `fcn_00415872` (rich4.asm:7166–7710, called rich4_main.asm:376)
  plays the "players jump in" intro. It is full screen and never touches the board.
  Native: `tools/screens/intro.{hpp,cpp}`, started by `Application::install_session` for
  a new game only. Main-loop order for a new game (rich4_main.asm:367–384):
  `_rich4_init_new_game` → `_rich4_show_loading_scene` (raw Data 601 "Loading" card,
  music stopped) → `_rich4_load_map` → `_rich4_init_panel_resources` → `fcn_004291d6`
  → `fcn_00415872` → `_rich4_start_game_loop(0)`. Loading a save (loc_00401d08) goes
  straight to the game loop with no loading card and no intro.
  1. AVI first (`fcn_00451677`): `AIRPLANE.AVI` when `0x4991b6` is set, else
     `FLY{TW,CHINA,JP,US}.AVI` by `0x4991b8`. A non-zero result (the video was
     interrupted) skips the whole intro. All shipped AVIs are 5 112-byte Indeo stubs, so
     the native port plays nothing here. Music track 1 starts (`fcn_00454d91(1)`).
  2. Jump 45 is an SPR set: 0 = airplane side with the door open (640×480), 1 = sky
     (640×480), 2 = left door-frame overlay (96×240), 3–14 = each character standing.
     Sprite 0 is drawn full screen, Effect 25 plays once, then FLC 46 (door opening,
     220×240, 15 × 71 ms) overlays at (180,60), flags 3.
  3. Loop `i = 0..N` (N = player count, turn order): restore (180,60,220×240) from
     sprite 0; if `i < N`, darken sprite `3 + character[i]` in place with brightness
     level −20 (`fcn_004552b7`) and draw it at (270,220), then sprite 2 at (150,60) (the
     next player waits in the doorway, in shadow, behind the frame); if `i > 0`, play FLC
     `47 + character[i−1]` (640×480, 40 × 28 ms) at (0,0), flags `0x14000003`: overlay,
     skippable, Effect 26 at frame 20 (`fcn_00450cda(ref_00475100, 0)`). Afterwards the
     bottom-right (320,400)–(640,480) is restored, which erases the jumper's last frame.
  4. Sprite 1 (sky) full screen; for each player FLC `59 + character` (parachute,
     640×480, 37 × 42 ms) at (0,0), flags 3; the bottom strip (0,440)–(640,480) is
     restored after each.
  Every FLC is skippable (mouse-up or key-up); a skip jumps to the cleanup, ending the
  whole intro. With 4 players it lasts about 1.1 + 4 × 1.1 + 4 × 1.55 ≈ 11.7 s.
  The native port still places each player on their first turn (unchanged, no RNG impact).

## 8. Card / tool asm quick index

`_rich4_animate_object` callers: chaichuka 98/164/235, chashuika 92, chafengka 142,
emoka 141/202, huandika 157/366, guaishouka 140, mengyouka 93, huanwuka 146/339,
qingshenfu 93, tianshika 137/176, tongmengka 84, qiangduoka 79, zhangjiaka 132,
xianhaika 90, tingliuka 87, wuguika 87, dilei 74, dingshizhadan 74, luzhang 74.

`fcn_00451985` (plot pulse) callers: emoka 206, huandika 167/376, huanwuka 150/343,
tianshika 139/180, fortune 178, news 793/2296/2432, core_actions 661 (rent street).

## 9. Native plumbing (minimum; no rule changes)

The original always **mutates the world first, then plays the picture**, and blocks
input meanwhile. The native session already does the mutation, so the app only
needs enough data to replay the picture, plus a pre-mutation view until the commit
frame.

1. **FLC decoder** (graphics layer). Handle chunk types 4, 7, 15, 16, skip 18 and
   the `0xF100` prefix, and treat index 0 as transparent for overlays. Expose
   `frame_count`, `delay_ms`, `width` and `height`. Cache by (archive, entry).

2. **One new event kind.** It can be a `GameEventKind::effect` with a payload, or a
   separate `drain_effects()`. Suggested shape:

   ```cpp
   enum class EffectKind : std::uint8_t {
       dice_roll, top_floor_sparkle, build_rise /*553*/, plot_pulse,
       object_throw, card_throw, missile, nuclear_missile, explosion /*525*/,
       dog_vehicle /*552*/, dog_fight /*532*/, demolish /*529*/, monster /*557*/,
       god_appear, demon_puff /*526*/, ambulance, police, ufo, airplane,
       parachute, bankrupt, news_flc
   };
   struct EffectEvent {
       EffectKind kind;
       std::size_t player;                 // actor
       std::uint16_t node = 0;             // target road node / land node
       rich4::board::Point target{};       // world point the camera centres on
       std::optional<rich4::board::Point> from;  // throws: actor world point
       std::uint8_t object_kind = 0;       // throws (16/17/18) or god kind (1..15)
       std::vector<std::uint8_t> dice;     // dice_roll: values already rolled
       std::vector<std::uint16_t> plots;   // plot_pulse: land/facility ids masked
       std::uint8_t character = 0;         // parachute variant
   };
   ```

   Each `EffectKind` maps in the app to a static table: archive, entry, screen
   anchor, flags (commit frame, sound frame, speed override), sound index, and
   post-hold ms. These are the values in §1–§6.

3. **Ordering.**
   - Emit `EffectEvent`s in the same order as the original call sequence relative to
     the existing events. Examples:
     - missile: `effect(missile)` after the damage, before the `notice`;
     - purchase: `land_purchased`, then `effect(top_floor_sparkle)` only on level 5.
   - The session must not consume extra RNG for any of this. Today's tests already
     forbid it.

4. **Pre-mutation board for the commit frame.** The world has already changed when
   the event is drained. So the app keeps a copy of the *previous frame's* sprite
   lists (`Scene` players, objects, lands, facilities). `draw_board` already builds
   these every frame.
   - While an effect with commit frame `N` plays, frames `< N` render the cached
     scene. Frames `≥ N` render the live world.
   - This is exact when one effect follows one mutation per tick. When several
     effects come from one tick, everything before the first commit shows the
     oldest snapshot, which is acceptable (**UNVERIFIED** visual parity).

5. **Blocking.** While the effect queue is non-empty, the app calls
   `session_->tick(0)` (or stops ticking) and ignores game input. Effect durations
   never feed back into rules. `auto_rounds` and replay modes should skip effects.
   Mirror cfg byte 1 with an "animation" option that skips the gated set (§0).

6. **Camera.** For targeted effects (missile, nuclear missile, demolish, monster,
   build_rise, throws' destination), temporarily set
   `Camera.center = target` for the duration, as `fcn_0041d476(x,y,0)` does. Then
   restore it to the active player. The 440×440 FLC top-left is `(0,40)`. In
   `board::project` terms that is `project(target) − (220,220)`, which keeps it
   correct if the camera ever differs.

7. **Anchors that need extra data.**
   - The dice need the actor direction and camera rotation (already available).
   - The demon puff needs the building screen anchor. Use the renderer's projection
     of the land's building record, minus (55,55). The renderer would need to expose
     a "building screen anchor for land id" helper (**UNVERIFIED** equivalence with
     `ref_0048a850`).
   - The plot pulse needs a per-pixel mask of the listed plots. The renderer must
     output a land-id layer, or redraw just those buildings and ground with
     brightness applied.

8. **Audio.**
   - FLC sounds start on frame 1 (index from the tables above).
   - The dice sound starts at frame 30.
   - The time-bomb tick (Effect 32) is a looping sound. It needs
     `play_loop` / `stop` support that `native_audio` lacks today.
