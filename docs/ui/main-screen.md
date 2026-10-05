# Original main game screen: implementation spec

Source: the pinned mytbk/rich4 disassembly in `.local/upstream/asm`. Unless a
file is named, line numbers refer to `rich4.asm`. Archives are named `Panel`,
`Data` and `map` (the `.mkf` files in `game-assets/`). The notation `P1:3` means
Panel entry 1, frame 3. All coordinates are on the 640x480 screen.

**Blit semantics.** Every `draw(frame, X, Y)` places the frame's top-left at
`(X - origin_x, Y - origin_y)`. This is the graph_st convention that the native
`picture()` already follows. Most UI frames have origin 0,0. Exceptions are
listed with their origin.

Reference images are in `.local/ui-ref/main-screen/`:

| File | Content |
| --- | --- |
| `orig-mode1-full.png` | Original screenshot in view mode 1 |
| `orig-topbar.png`, `orig-infopanel-tab0.png`, `orig-minimap.png` | Crops of the original screenshot |
| `panel-0.png` … `panel-4-8.png` | Panel contact sheets |
| `data-517.png` | Data 517 contact sheet |
| `map-16-minimap.png` | Map 16 contact sheet |
| `map-37-char0a.png` | Character sprite set (character 0x0a) |
| `data-4-festival-raw.png` | Data 4 decoded as raw 200x200 RGB555 |

## 1. Screen regions and view modes

`_rich4_init_panel_resources` (11888) loads these resources once:

| Panel entry | Contents |
| --- | --- |
| 0 | Info panel |
| 1 | Toolbar |
| 2 | Seasons and calendar |
| 3 | Dice faces |
| 4–6 | Dice animations |
| 7 | GO button |
| 8 | GO hit mask |

The same function also allocates a 200x200 festival buffer.

The window procedure is `fcn_00417e26` (10373). The redraw dirty bits are in
`fcn_004192f7` (12068):

| Bit | Rectangle | Content |
| --- | --- | --- |
| 1 | (0,0)-(440,40) | Toolbar |
| 2 | (0,40)-(440,480) | Board view (clip rectangle set by `_rich4_set_draw_area(0,40,440,480)`, 10424 block) |
| 4 | (440,0)-(640,80) | Right panel, top |
| 8 | (440,80)-(640,280) | Right panel, middle |
| 16 | (440,280)-(640,480) | Right panel, bottom |

The view mode is stored in `RICH4.CFG` byte +5. The "cycle view" hotkey (cfg
word +30) cycles it as 0→1→2 (`rich4_keyboard_hook.asm` around line 220).
Paint handling is at `loc_00418bb9` (11421).

| Mode | (440,0)-(640,280) | (440,280)-(640,480) | Minimap top `T` (table 0x4752aa, line 40753) |
| --- | --- | --- | --- |
| 0 | Info panel with 4 tabs (§3) | Season/calendar panel (§5) | none |
| 1 | Info panel with 4 tabs | Minimap (§4) | 280 |
| 2 | Compact header (440,0)-(640,80) (§3.4), then minimap at (440,80) | Season/calendar panel | 80 |

**Input gate.** Mouse input is ignored unless byte `0x46cafd` is set (10618 and
the other handlers). The byte means "GO button visible", that is, a human player
is waiting to roll. Set the byte with `fcn_004196f1` (12417) and clear it with
`fcn_00419703`. Every toolbar action clears it first and then restores it.

## 2. Top toolbar (440x40)

The toolbar is drawn by `fcn_00415d31` (7590).

The background is `P1:0` (439x40, origin 0,0), drawn opaque at (0,0).

Each button `i = 0..10` is anchored at `(40*i + 20, 20)`. The normal frame is
`P1:(1+i)`. When `i == hover`, the frame is `P1:(12+i)` instead (the larger,
brighter icon).

Hover is handled by mouse move at `loc_00418b0a` (11373). If x < 440 and
y < 40, then `hover = x/40`. When `hover` changes, the game plays sound effect
id 0 (`ref_0048231a`) and redraws the toolbar. Leaving the toolbar sets
`hover = -1`.

Press:

- **Mouse down** (`loc_00418670`, 11008): x < 440 and y < 40 sets
  `pending = 100 + x/40` and plays click sound id 1 (`ref_00482322`).
- **Mouse up** (`loc_004186cb` → `loc_00418863`, 11037–11180): calls
  `_rich4_ui_clicking_top_panel(pending - 100)`. The release position is not
  checked.

There is no pressed frame, no disabled frame and no tooltip text; the hover
path draws no text.

| i | Icon (normal / hover frame) | Action (`rich4_ui_clicking_top_panel.asm`) | Hotkey cfg word | Native target |
| --- | --- | --- | --- | --- |
| 0 | ? (P1:1 / P1:12) | `_rich4_ui_help_entry(20,60)`, help | +64 輔助說明 | help (missing) |
| 1 | PC (P1:2 / P1:13) | `_rich4_ui_options_entry(1)`, system/options | +58 系統 | `Screen::settings` |
| 2 | PC + bulb (P1:3 / P1:14) | `_rich4_ui_ai_settings_entry`, hand the player to the AI (託管) | +56 託管 | missing |
| 3 | Red floppy (P1:4 / P1:15) | `_rich4_ui_load_game(1)` | +62 LOAD | `load()` |
| 4 | Green floppy (P1:5 / P1:16) | `_rich4_ui_save_game` | +60 SAVE | `save()` |
| 5 | Map (P1:6 / P1:17) | `_rich4_ui_small_map_entry`, full map (§4.3) | +50 地圖 | missing |
| 6 | Magnifier (P1:7 / P1:18) | `_rich4_ui_player_info_view_entry`, 查詢 dialog (Panel 9 + Panel 0x4a; separate spec) | +48 查詢 | `Screen::wealth` (approx.) |
| 7 | Hammer (P1:8 / P1:19) | `_rich4_ui_use_tool_entry`, tools | +46 道具 | `Screen::tools` |
| 8 | CARD (P1:9 / P1:20) | `_rich4_ui_use_card_entry`, cards | +44 卡片 | `Screen::cards` |
| 9 | SALE (P1:10 / P1:21) | `_rich4_ui_sale_entry`, player trading | +42 交易 | `open_trading()` |
| 10 | Chart (P1:11 / P1:22) | `_rich4_ui_stock_entry(0)`, stock market | +40 股市 | `Screen::stocks` |

Notes:

- There are no toolbar rotate buttons. Rotation belongs to the minimap (§4.2)
  and to hotkeys +52 (left) and +54 (right).
- The current native toolbar order and labels (`tools/native_game.cpp:708`) do
  not match this table.
- The hotkey list is in `docs/rich4_cfg.txt`.

## 3. Right info panel (modes 0 and 1: 200x280 at (440,0))

The panel is drawn by `fcn_00415f69` (7811). The selected tab is stored per
player in `tab[player]` (byte array `0x48be24`; 0 on init). In mode 2 this
panel is replaced by §3.4.

### 3.1 Static artwork

- Background: `P0:tab` (200x280), opaque at (440,0).
  - Frames 0–3 are the 資金/地產/股票/其他 states. The tab strip (x 616–640) is
    baked into each frame with the active tab highlighted.
  - Each frame includes the three row icons and the light-blue value bars.
  - `P0:5` is the tab-less variant for a special actor (§3.3).
- Labels are rendered **into the frames once** at window init (`loc_00417eba`,
  10424). The font is 12 px, colour 0x101010, plain (style 2), spacing 1, align 0.
  Each label is at frame-local x=10 (screen x=450), y = 80, 145 and 208:

| Frame | y=80 | y=145 | y=208 |
| --- | --- | --- | --- |
| P0:0 資金 | 現　金 | 存　款 | 總資產 |
| P0:1 地產 | 土　地 | 連鎖店 | 設　施 |
| P0:2 股票 | 總市值 | 成　本 | 經營權 |
| P0:3 其他 | 點　卷 | 貸　款 | 保險期 |
| P0:4 (mode 2 header) | 現金 at (90,40), 存款 at (90,62) (spaces stripped) | | |

The strings are at `ref_00463920`–`ref_00463966` (lines 28269–28329). The
labels contain two full-width spaces, as in "現  金".

### 3.2 Dynamic content (regular player `p`)

If the current player is 8, then `p = byte 0x498e70` (special-turn proxy,
UNVERIFIED meaning).

| Element | Draw | Font (size, fg, bg, style, spacing) | Source line |
| --- | --- | --- | --- |
| Price index | "物價指數  %d" at (450,260), align 0 | 12, 0x101010, -, 2, 1 | 7924 block |
| Tab labels i=0..3 資金 地產 股票 其他 | Vertical text (align 3, centred) at (627, {35,108,178,250}). The y values are the table at 0x415d0d (7571) +20 | 18, active 0x101010 / inactive 0x404040, -, 2, 0 | `loc_004161a5` |
| Colour bar | Fill (523,57) 86x12 with 0x000000, then fill (522,56) 86x12 with player colour (player +4) | — | `loc_004161f8` |
| Portrait | `map:(27+character):0` (e.g. 74x64, origin 35,32) at (482,40), transparent | — | same |
| Ally head | If `allied_player` (player +65) ≠ 0: `map:(27+char of ally):1` (34x29, origin 18,14) at (524,64) | — | same |
| Name | Player name at (564,40), align 2 (centred both ways) | 22, 0x101010, -, 2, 0 | `loc_00416288` |
| 3 values | Right-aligned (align 1, x = right edge) at x=600, y = 102 / 166 / 230 (ink top) | Inherits 22 px plain | below |

Tab contents, in row order y=102 / 166 / 230:

| Tab | Row 1 | Row 2 | Row 3 | Lines |
| --- | --- | --- | --- | --- |
| 0 資金 | `"$"+fmt(cash)` | `"$"+fmt(bank_balance)` | `"$"+fmt(_rich4_calculate_player_wealth(p))` | 8079 |
| 1 地產 | Count of lands **and** facilities with owner == p+1 | Owned lands with chain-store byte (+0x18) ≠ 0 | Owned facilities with byte +0x1a ≠ 0 (built level, UNVERIFIED) | 8121 |
| 2 股票 | `"$"+fmt(Σ shares × price)`; the float sum is truncated toward zero after each stock | `"$"+fmt(Σ shares × per-share cost)` (player stock +4 float), same truncation | Count of on-map commercials with owner == p+1. The loop runs records 1..count+1, a likely off-by-one (UNVERIFIED) | 8226 |
| 3 其他 | `itoa(points)` | `"$"+fmt(loan)`. If loan ≠ 0 and `days_between(today, loan_due)` ≠ 0, also draw "%d天" at (500,146), align 0, 12 px plain spacing 1 | `"%d天"` of `days_assurance` (+0x3e) | 8332 |

`fmt` is `_rich4_num_to_currency_string` (0x452793), which adds thousands
commas: "$97,800". Counts use plain `itoa`.

**Tab click** (`loc_004182fa`, 10760): mode ≠ 2, x ≥ 616 and y < 280 set
`tab[current] = y/70`. If the tab changed, the game plays click sound 1 and
redraws the panel. The hit bands are y 0–69, 70–139, 140–209 and 210–279.

### 3.3 Special actor turn

This applies when `current_player >= player_count` and ≠ 8 (lines 7860–7900).

- `P0:5` is drawn at (440,0).
- The actor name (`_rich4_special_player_names[cur-4]`) is drawn at (582,40),
  centred, 22 px plain.
- The actor sprite set frame 1 is drawn at (524,64).
- There are no tabs and no values.

### 3.4 Mode 2 compact header (200x80)

The header is drawn by `fcn_004166f8` (8422).

- `P0:4` is drawn at (440,0).
- Colour bar: fill (529,33) 106x4 with black, then fill (528,32) 106x4 with the
  player colour.
- The portrait is at (482,40) and the ally head at (524,64), as in §3.2.
- Name: centred at (582,16), 20 px plain.
- Cash and deposit are right-aligned at x=634, y=41 and y=63. The font is 12 px,
  0x101010, style 2, spacing 1.
- For a special actor: the name is drawn at (582,16) in 20 px, and the sprite
  frame 1 at (524,64).

## 4. Minimap

### 4.1 Panel minimap (modes 1 and 2: 200x200 at (440,T))

The panel minimap is drawn by `fcn_00416e6d` (9036). The scale is `s(v) = (v*5696) >> 16` (≈ v/11.5).

1. **Base:** `map:(16 + stage*4 + map):0` (200x200), opaque at (440,T).
   - This is a working copy rebuilt by `fcn_0040a4e1(0)` (3663). It copies the
     pristine frame, then stamps owner markers recoloured to the owner's colour
     by `fcn_00456384`.
   - Lands (`owner` byte +0x19) use `D517:(22 + (rec[+0x1b]&1))` (3x3 / 5x5).
   - Facilities and on-map commercials use `D517:(24 + flag&1)` (7x7 / 11x11).
   - Each marker is placed at minimap-local `(s(x), s(y))`.
   - The meaning of +0x1b bit 0 is UNVERIFIED (size class).
   - The rebuild call sites (after ownership changes) are not enumerated here.
2. **Rotate buttons:** `D517:20` at (443,T+3) and `D517:21` at (468,T+3), each
   25x26 with origin 0.
3. **Player dots:** for each slot < player_count with x ≠ 0, draw
   `map:(27+char):6` (10x9, origin 5,4) at `(440 + s(x), T + s(y))`. Bankrupt
   players (x = 0) and special actors get no dot.
4. **Current-player box:** a 1-px white (0xffffff) outline rectangle 30x30
   centred on the current player's point. For a special actor, its own x,y
   are used. `fcn_0045620f` is an outline, confirmed in
   `rich4_drawing_utils2.asm`.
5. **Free-scroll box:** when the view is detached (`0x48be18`), draw a red
   (0xff0000) 30x30 outline centred on `(440 + s(cx), T + s(cy))`, unless it
   coincides with the player box.

Input (`loc_00418415`, 10830). It requires mode ≠ 0, x > 440 and T < y < T+200.
Local coordinates are `lx = x-440` and `ly = y-T`.

- **Rotate buttons**, hit when 3 ≤ lx ≤ 53 and 3 ≤ ly ≤ 28: `b = (lx-3)/25 + 1`.
  - On mouse down, draw the pressed frame `D517:(17+b)` (18 or 19) at the same
    place and play click sound 1.
  - On mouse up (11037–11120), draw `D517:(19+b)` (20 or 21).
  - Then left (b=1) sets `rotation = (rotation-1) & 7` and right sets
    `rotation = (rotation+1) & 7`. The view cache (`0x474930/34 = -1`) is
    invalidated and the board is redrawn.
- **Click elsewhere in the minimap** (`loc_00418591`, 10942): the view centre
  becomes `c = (l*761856) >> 16` (≈ l × 11.625) on both axes.
  - The centre is clamped to [220, 2084].
  - The view enters free-scroll, and the board and minimap are redrawn.
  - While the button is held, mouse moves keep dragging (`loc_00418910`, 11226).
- **Right button up inside the minimap** (`loc_00418893`, 11188) clears
  free-scroll. The view then follows the current player again.

### 4.2 Rotation

There are 8 views, stored at `0x499088` (native `World::camera_rotation`).

### 4.3 Full map dialog (toolbar 5)

The dialog is in `rich4_ui_small_map.asm`.

- On init (WM 0x401), `fcn_0040a4e1(1)` rebuilds `map:(16+id):1` (400x400).
  Lands use `D517:26/27` and facilities `D517:28/29`, with scale
  `(v*11392)>>16` (≈ v/5.75).
- On paint, the frame is drawn at (20,60). Each player marker is
  `map:(27+char):5` (28x25, origin 13,12) at `(20 + v*11392>>16, 60 + …)`.
- The dialog closes on WM 0x205 (right button up). It is UNVERIFIED whether a
  left click also closes it through `_Wait_0402_Message`.

## 5. Date, season and calendar panel (modes 0 and 2: 200x200 at (440,280))

The panel is drawn by `fcn_004169bc` (8648).

The date is `cfg+8` (day, month, year16). The sub-mode is `cfg+12`: 0 means the
season picture, 1 means the month calendar. The month→season table at 0x475218
(40684) is:

| Month | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Season | 3 | 0 | 0 | 0 | 1 | 1 | 1 | 2 | 2 | 2 | 3 | 3 |

The seasons are 0 spring (meadow), 1 summer (beach), 2 autumn (lake) and
3 winter (snow).

**Season picture (cfg+12 = 0, `loc_00416b4c`, 8780)**

1. **Picture:**
   - Let `row = holiday_row(date)` (`fcn_004521f0`). If row = -1, draw
     `P2:season` (200x200) at (440,280).
   - Otherwise, use the festival picture: Data entry
     `base[stage*4+map] + row`. It is raw 200x200 RGB555 with no SPR header
     (80000 bytes, e.g. Data 4 = 元旦).
   - The bases are {4, 28, 47, 67, 87, 108, 95, 118} (0x475208, 40678).
   - The game caches the entry already loaded.
   - On a festival day, red colouring applies.
2. **Mode buttons:** `P2:8` (lit sun, 24x23, origin 12,11) at (462,300), and
   `P2:11` (moon, 20x20, origin 10,10) at (492,301).
   - `P2:9` and `P2:10` (the other sun/moon states) are never drawn: UNVERIFIED.
3. **Text:**
   - Weekday: `ref_0047511c[wd]` (星期日…六), vertical (align 3) at (454,352),
     16 px.
   - Day number: centred at (500,376), 60 px.
   - Colours: if the day is Sunday or a festival, fg 0xff0000 with outline
     0x101010; otherwise fg 0x101010 with outline 0xffffff. The style is 6
     (bold + 1-px outline) and spacing 1.
4. **Year and month:** drawn on both sub-modes, at `loc_00416d3b` (8939).
   - Year: at (580,288), align 0, 24 px.
   - Month: "%d月", centred at (500,328), 28 px.
   - Both use fg 0x101010, outline 0xffffff, style 6, spacing 1.

**Month calendar (cfg+12 = 1)**

- The background is `P2:(season+4)`, which has the moon lit and the S M T W T F S
  header baked in.
- Let `wd` be the weekday of the 1st (`_rich4_parse_date`, Sunday = 0). Day `d`
  is centred at:
  - `x = 470 + 23*((wd+d-1) % 7)`
  - `y = 378 + 14*((wd+d-1) / 7)`

  The code wraps after x = 608.
- Today gets a red 0xff0000 1-px outline rectangle 20x14 at (x-10, y-6).
- Day numbers use 12 px plain (style 2, spacing 1), align 2. They are red if
  `fcn_004523d5(date) == 1` (closed day; probably native
  `holidays::is_closed`, UNVERIFIED equivalence), otherwise 0x101010.
- The year and month are drawn as above. There is no big day number or weekday.

**Toggle** (`loc_0041835f`, 10787). It requires mode ≠ 1, x > 440 and
288 ≤ y ≤ 314:

- x 448–474 (sun) sets cfg+12 = 0, if it was 1.
- x 478–504 (moon) sets cfg+12 = 1, if it was 0.

Either change plays click sound 1 and redraws the panel. The value is part of
`RICH4.CFG`; whether it is written on exit is UNVERIFIED.

## 6. GO button, dice selector and dice roll

### 6.1 Floating GO button (Panel 7, 72x67, origin 0,0)

The button is drawn by `fcn_00417191` (9310); the drawing is also decompiled in
`.local/upstream/csrc/drawing.c` `fcn_417191`.

- **Position:** global `(gx,gy)`, default (180,120) (0x475284, 40723). It is the
  top-left corner and is kept across turns.
- **Frame:** `P7:(blink + k)`.
  - `blink` toggles 0/1 every 500 ms (SetTimer 0x1f4, WM_TIMER `loc_00418b7a`,
    11409). The timer only runs while the button is visible and no tile popup is
    shown.
  - `k = 0` normally, `2` if `days_stopping`, `4` if `days_tortoise_walking`
    (tortoise overrides stopping).
  - The frames are 0/1 GO (white/yellow), 2/3 no-entry and 4/5 tortoise.
- **Dice-count icons** (frames 6–11, 15x15). They are drawn at x = gx+7 when
  active, or gx+8 when inactive:

  | Traffic (player +17 & 3) | Icons |
  | --- | --- |
  | 0 or 3 (foot) | One icon at y = gy+26: `P7:7`, or `P7:6` when stopped |
  | 1 (motorbike) | i = 0..1 at y = gy+16+19i |
  | 2 (car) | i = 0..2 at y = gy+9+16i |

  For the motorbike and car, die `i` is active when `i < dice_count` and the
  player is not stopped. Active uses `P7:(2i+7)`, inactive uses `P7:(2i+6)`.
- **Hit mask:** `Panel 8` is raw 72x67 bytes, row-major. Hit handling is at
  `loc_00418151` (10618):

  | Value | Region | Action |
  | --- | --- | --- |
  | 1 | Dice column x 7–22, y 9–56 | Not when stopped. Motorbike: y in [16+19i, 32+19i] gives dice_count = i+1. Car: y in [9+16i, 25+16i] gives dice_count = i+1. Plays click sound and redraws |
  | 2 | Frame | Starts a drag. The offset is stored and the button follows the mouse, clamped to [0, 568] × [0, 413] (11330) |
  | 3 | GO, x 30–64, y 14–54 | Click sound, hide GO, refresh, roll (`fcn_0040dd1f`) |
  | 4 | Transparent | Swallowed (nothing happens) |

- The dice-count hotkey (cfg +38) cycles 1→max (`rich4_keyboard_hook.asm`
  ~285). The roll hotkey is cfg +36.

### 6.2 Roll animation

**Toss pose first.** GO (or the AI) calls `fcn_0040dd1f`
(rich4_player_utils.asm:994). For a regular player it sets the per-player
movement state byte `[0x498ea2 + slot*0x34]` to **2 (toss)** and its frame counter
`[0x498ea3 + slot*0x34]` to 0, except:

- `controller_flags & 0x30` or `days_tortoise_walking`: state 1, one step, no
  toss and no dice;
- `days_stopping`: state 0, no roll.
- Special actors (slots 4–7, robot 8) never toss.

The movement state machine `fcn_0040d7c4` (state 2 = `loc_0040d975`, line 695)
runs once per game tick: the 20 ms multimedia timer (`fcn_00401f98`,
rich4_mouse_utils.asm:141) raises the tick every `{6,4,2}[game_speed]` periods
(0x46cb20), so **120/80/40 ms**, 80 ms at the default speed 1. Each tick increments
the counter and redraws the board. When the counter reaches the pose graph's frames
per direction (`[graph+4] >> 3`), it calls the roll `fcn_00419572` below. Then it
starts the looped walk sound preset `0xb + (traffic & 3)` (Effects 44/45/46/53),
or preset `0xf` (Effect 47) on water, and switches to state 1 (walk).

- **Pose graph:** `[0x498eb4 + slot*0x34 + state*8 + anim*4]`, loaded by
  `_rich4_update_player_sprite` (rich4.asm:5297) from `Data.mkf`, base
  `0x80 + character*21`:
  - on land: `+traffic*3 + {0 stand, 1 walk, 2 toss}`;
  - on water (node flag `0x80` at +0x27, anim 1): `+13/+14/+15`;
  - sleepwalking: `+16/+17`, and the foot toss `+2`.
  - Each toss graph has 8 directions × 9 frames.
- **Board frame:** `facing * frames_per_direction + counter`, where
  `facing = (direction + 8 - rotation) & 7` (rich4.asm:1418–1430). Frames 0–8 show the
  wind-up with a die in hand; frame 8 is the follow-through with empty hands.
- The roll FLC is an overlay played synchronously. So the last toss frame stays
  under the dice and the 500 ms faces hold, until the walk redraws.

The roll is `fcn_00419572` (12275).

1. Draw `n = dice_count` values with `rand()%6+1`. When forced, `n = 1` with the
   given value.
2. Compute the facing `f = (8 - rotation + player.direction) & 7`. The anchor is
   `(ax, ay) = (136 + DX[f], 48 + DY[f])`, where `DX/DY` are (4,12), (12,12),
   (8,6), (-4,-6), (-12,-12), (-24,-12), (-20,-6), (-12,6) (0x475224, 40689).
3. Play sound effect id 10 (`ref_0048235a`).
4. Play the animation `Panel (3+n)` at (ax,ay): entry 4 for 1 die, 5 for 2, 6
   for 3.
   - This is **not SPR**. It is a custom animation format, header
     `bd00 1d01 0800 …` = 189x285, probably 8 frames. The format is UNVERIFIED;
     the contact sheet cannot decode it.
   - It is played by `fcn_0045144f` with speed `{5,3,2}[cfg+0 game speed]`
     (0x475264).
5. Draw the final faces. Die `i` uses `P3:(6i + v_i - 1)`, all anchored at
   (ax+85, ay+145). The negative origins place each die: die 0 top-left
   (ax+1, ay+243), die 1 (ax+96, ay+248), die 2 (ax+153, ay+244).
6. Flip the region (ax,ay)-(ax+189, ay+285), wait 500 ms, and return the sum.

## 7. Map-tile popup

The popup appears on mouse down over the board while the button is visible. It
is a partial decode of `fcn_00417559` (9646).

- The tile instance comes from `_rich4_get_instance_from_position(x, y-40)`.
- The frame is `D517:q` (189x116), where `q = (y ≥ h) + 2·(x > 440-w)`. Frames
  0–3 are the four pointer corners; origins put the pointer at the click.
- Text: 16 px, fg 0xf0f0f0, bg 0x101010, style 3 (bold + shadow).
  - Land: owner name, level/type name, and toll or "×%d" chain count.
  - Node name for nodes.
- The popup is restored on mouse up or move (`fcn_00417c67`, 10303).
- Text base is `y+42` (frame below the pointer) or `y-96` (above), centred at
  `x ± (w/2+9)`. Land: owner @+0, level name (`空  地`/`連鎖店`/0x475138) @+18,
  `N級` (0x475164) @+36, `$toll` / `$2000·index×chains` / unowned `$toll_by_level[level]` @+54.
  Facility: owner, then `空  地` or type name (0x475150) and, for hotel/mall, `N級` and
  `×(toll_by_level[level]·index)`; gas station `×1000`. Company: owner, name @+27,
  `餘股:%d` @+54. Node/landscape name @+27; player/special actor/object name @+25.
- Native: `tools/screens/tile_popup.cpp`. Touch shows it on a tap and hides it on the
  next tap; hit-testing uses sprite boxes around projected anchors plus
  `board::unproject` for named road tiles instead of the original ID buffer.

## 8. Text rendering

Text rendering is `rich4_text_drawing.asm`; the decompile is in
`csrc/font.c`.

**Font creation.** `_rich4_create_font(size, fg, bg, style, spacing)` creates a
GDI font named 細明體 (MingLiU), Big5 charset, height `-size` px.

- Weight is 400 if `style == 2`, otherwise 700.
- Style bit 0: drop shadow in `bg` at +1,+1.
- Style bit 2 (when bit 0 is clear): 1-px outline in `bg`; the fg is drawn at +1,+1.
- Style bit 3: clip to the draw area.
- Character extra spacing is `spacing - 1`. Vertical text advances
  `size + spacing (+1 if style & 6)` per character.

**Anchoring.** `_rich4_draw_text(dst, str, x, y, align)` positions the **ink
bounding box**, not the text cell:

| align | Meaning |
| --- | --- |
| 0 | Left/top |
| 1 | Right edge at x, top |
| 2 | Centred in x and y |
| 3 | Vertical (one glyph per line), centred in x and y |
| 4 | Multi-line centred |
| 5 | Left, vertically centred |
| 6 | Right, vertically centred |
| 7 | Centred x, bottom at y |

A leading `#NNNN` is a voice id and is stripped before drawing.

## 9. Native mapping

The native API is `include/rich4/game.hpp` and `world.hpp`.

| Element | Native source |
| --- | --- |
| Input gate (GO visible) | `session.phase() == GamePhase::human_ready` |
| Toolbar actions | See §2 table. Help, AI hosting and the full map dialog have no native screen |
| Tab per player | New UI state `std::array<uint8_t,9> tab` (original is not saved in the DAT; UNVERIFIED) |
| Name, portrait | `players[p].state.character` → name table; `map` entry 27+character frame 0 |
| Colour bar | `state.color` |
| Ally head | `status.allied_player` (one-based) |
| 資金 | `state.cash`, `state.bank_balance`, `stocks::player_wealth(world,p)` |
| 地產 | `world.lands[i].economy.owner_code == p+1`, `.chain_store`; `world.facilities[i].owner_code`, `.level` (+0x1a, UNVERIFIED) |
| 股票 | `players[p].stocks[k].amount`; price is `world.stocks[k].float_bits[2]` (stock +20); cost is `raw_cost_basis` (float bits); 經營權 is `world.commercials[i].owner_code == p+1` |
| 其他 | `state.points`, `state.loan`, `status.loan_due_date` with `calendar::days_between`, `status.days_assurance` |
| Price index | `world.header.price_index` |
| Date | `world.header.date`, plus `calendar::weekday` and `calendar::days_in_month` |
| Festival row | Not exposed. Needs `0x4521f0` (holiday row index) from `holidays` |
| Red day | `holidays::is_closed(date, map_id, stage_id)` (assumed = `0x4523d5`) |
| Minimap base | `map` entry `16 + stage_id*4 + map_id` |
| Minimap dots | `state.x/y` |
| Owner markers | `Land/Facility/Commercial` `x`, `y`, owner |
| Viewport box | Current player, or `special_actors[k].x/y` |
| Rotation | `World::camera_rotation`, `GameSession::set_camera_rotation` |
| Free-scroll centre | Native camera state |
| GO frames | `status.days_stopping`, `status.days_tortoise_walking` |
| Dice icons | `state.traffic_method & 3`, `state.dice_count`, `set_dice_count()` |
| Roll | `roll_dice()`; faces from `session.dice()`; facing `state.direction` |
| View mode / calendar sub-mode | Native settings (RICH4.CFG +5 / +12) |

**Native discrepancies found while reading `tools/native_game.cpp`:**

- Dice faces are drawn from `P7` frames 0–5, which are the GO button states
  (line 776). The dice faces are `P3`.
- The toolbar uses text buttons in a different order (line 708).
- `P0:0` is always drawn; there is no tab switching.
- The bottom panel is a paper fill rather than §4/§5.
