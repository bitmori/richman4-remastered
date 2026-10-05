# Original UI spec — finance and menu screens

Implementation spec for rebuilding the original 大富翁4 v3.11 finance and menu screens in the native
C++/SDL3 port. Derived from the pinned upstream disassembly (`.local/upstream/asm/*.asm`, `csrc/*.c`)
and the original archives in `game-assets/` (commercial, gitignored). Research only: nothing under
`tools/`, `src/`, `include/` was changed.

- Line references: `FILE:LINE` (relative to `.local/upstream/asm/` unless another directory is
  given), plus the asm label.
- Anything not proven from code or pixels is marked **UNVERIFIED**.
- Reference sheets and helpers live in `.local/ui-ref/finance-menus/` (gitignored, contain commercial
  art):
  - `frames.py ARCHIVE FIRST LAST` prints every frame's `W×H@(origin_x,origin_y)` (it wraps
    `build/native/rich4-tool extract`).
  - `asmdata.py str|hex|w16 ADDR …` dumps `.data` tables and Big5 strings.
  - Contact sheets: `data-1.png`, `newgame-*.png`, `bank-*.png`, `stock-*.png`, `sale-*.png`,
    `auction-*.png`, `lottery-*.png`, `saveload-520.png`, `options-3.png`, `ai-77.png`,
    `info-9.png`, `info-74.png`.

Priority, by how often a casual player sees a screen:

1. Main menu
2. New game setup
3. Bank (ATM on every pass, desk on stop)
4. Stock market
5. Player info / wealth
6. Save / load
7. Lottery
8. Auction
9. Player trading
10. Options
11. AI settings

Sections below follow that order.

---

## 0. Shared conventions (apply to every section)

### Archives
`mkf_data` = `data.mkf`, `mkf_panel` = `panel.mkf`, `mkf_effect` = `effect.mkf`,
`mkf_speaking` = `speaking.mkf` (`csrc/media_init.c:673-676`); jump.mkf and map.mkf are loaded per
screen or map. An SPR/SMP entry is a 12-byte header (`sig[4]`, `nchunk`, `start_offset`) followed by
`nchunk` `graph_st {int16 w,h,x,y; int32 data}` records. Frame *i* is at `base + 0xc + 12*i`; that
is the `+0xc` / `imul 12` pattern seen in every asm file.

### Placement
All blits go to the 640×480 RGB555 back buffer `0x48a08c`:
- `fcn_004563f5(buf, frame, x, y)` draws opaque.
- `fcn_00456418(buf, frame, x, y)` skips colour 0 (transparent).
- `fcn_004562a5(dstFrame, frame, x, y)` composes non-zero pixels into another frame.
- `fcn_0045643d(buf, src, x, y, sx, sy, w, h)` restores a rectangle from a background frame.

Every one of these places the frame's **top-left at (x − origin_x, y − origin_y)**
(`csrc/drawing.c`/`rich4_drawing.c:68 draw_image_in_rect`, `rich4_drawing_utils2.asm:338-436`).
Frames with origin (0,0) are therefore drawn at their stated top-left; centred-origin frames (buttons,
portraits) are drawn at their centre.

### Fonts
`rich4_create_font(size, colour, colour2, flags, spacing)` (`rich4_text_drawing.asm:242`):
- Colours are written 0xRRGGBB.
- Glyph height is `size` px. The face is the default GDI font (`CreateFontA` with charset 0x88 =
  CHINESEBIG5).
- `flags` bits:
  - bit0 (1): 1-px drop shadow in colour2 at +1,+1.
  - bit1 (2): bold (weight 700, else 400).
  - bit2 (4): 4-neighbour outline in colour2.
  - bit3 (8): transparent compose path. **UNVERIFIED** exact effect.
- `spacing − 1` is passed to `SetTextCharacterExtra`.

Typical values:
- `(16, 0xf0f0f0, 0x101010, 3, 1)`: white bold with shadow. The workhorse.
- `(16|20, 0x101010, 0, 2, 1)`: black bold.

### Text placement
`rich4_draw_text(dst, str, x, y, align)` (`rich4_text_drawing.asm:324`, jump table `ref_0044faa0`).
`dst = 0` draws on the screen; otherwise `dst` is a frame and the coordinates are frame-local.

| align | anchor |
|---|---|
| 0 | top-left at (x,y) |
| 1 | right edge x, top y |
| 2 | centre (x,y) |
| 3 | vertical text centred at (x,y) |
| 4 | multi-line (DT_CENTER, wrap width 512) centred at (x,y) |
| 5 | left edge x, vertical centre y |
| 6 | right edge x, vertical centre y |
| 7 | multi-line centred x, bottom at y |

A string starting with `#NNNN` first plays the Speaking.mkf voice clip NNNN (`fcn_0045441a`) and is
then drawn without the prefix. The voice IDs used are listed per screen.

### UI sound table
`0x48231a…` holds 8-byte records whose first word is the Effect.mkf entry (`rich4.asm:50864`,
loaded by `load_sound_from_mkf`, `csrc/media_init.c:591`):

| record | Effect.mkf entry | use |
|---|---|---|
| 0x48231a | 0 | hover |
| 0x482322 | 1 | click/confirm |
| 0x48232a | 2 | exit/back button |
| 0x482332 | 4 | cancel (right-click) |
| 0x48233a | 3 | error/refused |
| 0x48234a | 7 | key-pad key |
| 0x482352 | 9 | gauge drag |

### Common input rules
- **Right mouse button up** (WM 0x205) means cancel/back on every screen in this document.
- Buttons usually show their pressed art on mouse-down and act on mouse-up. The release point is
  usually not re-tested; exceptions are noted per screen.
- There is almost never separate hover or disabled art. Exceptions: main menu (hover frames), stock
  labels (grey text), bank director buttons.
- `fcn_00451b9e(rect)` gives a generic "pressed" bevel: content shifts 1 px down-right and the
  top/left edges are darkened; `fcn_00451d4e` undoes it.
- `fcn_004552e7(buf, x, y, w, h, level)` darkens a rectangle through the brightness table at
  `0x485d68`. Level −16 is about 50 %, −12 a moderate darken, −20 about 37 %. **UNVERIFIED** exact
  rounding.

### Cursor
- `fcn_00402460(0/1)` hides or shows the software cursor around redraws (`csrc/fcn_4021f8.c:21`).
- Cursor images are Data.mkf entry 0 frames (e.g. 0:27 in the ATM, 0:28 at the lottery, 0:41 is the
  default pointer).

### RNG caveat
Several original screens (bank clerk, auction, lottery, new-game auto-fill) call libc `rand()` for
cosmetic blinks. The native port must use a separate cosmetic RNG for those; the real picks are listed
per screen.

### In-game entry points (top toolbar)
`_rich4_ui_clicking_top_panel` (`rich4_ui_clicking_top_panel.asm:23-101`, jump table
`ref_00417d39`):

| idx | screen |
|---|---|
| 0 | help |
| 1 | options (mode 1) |
| 2 | AI settings |
| 3 | load (mode 1) |
| 4 | save |
| 5 | small map |
| 6 | player info |
| 7 | tools |
| 8 | cards |
| 9 | trading |
| 10 | stocks (mode 0) |

Each returns through `fcn_0040defe` → `fcn_004196f1`, which redraws the board.

---

## 1. Main menu

Sources: `rich4_ui_main.asm` (`_rich4_ui_main_entry` :432, `_rich4_ui_main_callback` :46, data
:503-511), equivalent C in `csrc/main.c` (`ui_entry`, `entryCallback`). Art is in
`.local/ui-ref/finance-menus/data-1.png`.

### Images (Data.mkf entry 1, SMP, 11 frames)

| frame | size @ origin | use |
|---|---|---|
| 1:0 | 640×480 @(0,0) | background (world map, 大富翁4 logo) |
| 1:1 / 1:2 | 108×105@(55,53) / 116×113@(59,57) | START normal / hover |
| 1:3 / 1:4 | 107×92@(51,51) / 114×99@(55,54) | LOAD normal / hover |
| 1:5 / 1:6 | 98×90@(51,42) / 103×98@(53,46) | OPTION normal / hover |
| 1:7 / 1:8 | 53×18@(27,9) / 58×19@(29,10) | EXIT normal / hover |
| 1:9 / 1:10 | 76×91@(37,45) / 90×107@(45,52) | NEW STAGE normal / hover |

### Buttons
Centre positions are in table `ref_0046cb28` (`rich4_ui_main.asm:503`, `csrc/main.c:22`). Index *i*
uses normal frame `2i+1` and hover frame `2i+2`. Both are drawn at the centre, so the top-left is
centre − origin.

| i | button | centre | normal rect (screen) | hover rect = hit rect |
|---|---|---|---|---|
| 0 | START | (190,380) | (135,327)–(243,432) | (131,323)–(247,436) |
| 1 | LOAD | (328,380) | (277,329)–(384,421) | (273,326)–(387,425) |
| 2 | OPTION | (468,378) | (417,336)–(515,426) | (415,332)–(518,430) |
| 3 | EXIT | (328,450) | (301,441)–(354,459) | (299,440)–(357,459) |
| 4 | NEW STAGE | (62,380) | (25,335)–(101,426) | (17,328)–(107,435) |

### Init
- At init, the 5 normal frames are composed into 1:0 (`fcn_004562a5`, `rich4_ui_main.asm:443-465`).
- "V3.11" is drawn into 1:0 with `font(16, 0xf0f0f0, 0x101010, 3, 1)`, align 6, at (638,470), i.e.
  right-aligned at the bottom-right corner (:467-480).
- Music track 0 plays (`fcn_004549cf(0)`).

### Behaviour (`entryCallback`)
- **Mouse move.** The hit test uses the *hover* frame rect (`csrc/main.c:126-133`). On entering a
  button:
  1. Draw its hover frame onto the screen.
  2. Play Effect 0.
  3. Restore the previously hovered button from 1:0 (a rect copy of the hover-frame size).

  Leaving all buttons restores the last one.
- **Left down** acts immediately (no release step) and plays Effect 1.

| button | status | action |
|---|---|---|
| START | 0 | new game with stage 0 (map set 1) |
| NEW STAGE | 4 | new game with stage 1 (map set 2) |
| LOAD | 1 | `_rich4_ui_load_game(0)`. If it returns −1 (no saves, or cancelled), redraw the menu and stay. |
| OPTION | 2 | stop MIDI, `_rich4_ui_options_entry(0)`, restart music, redraw |
| EXIT | 3 | quit |

### Native gap
- `tools/native_game.cpp` already blits Data 1:0 with invisible hit boxes, but it has:
  - no hover frames;
  - no EXIT button;
  - no Effect 0/1 sounds;
  - no "V3.11" text.
- Its hit rects differ from the table above.
- NEW STAGE leads to the new-game screen with a "地圖版本" toggle, instead of fixing `stage_id = 1`.

---

## 2. New game setup

Sources: `rich4_new_game.asm` (NG), `rich4_main.asm` (MAIN), `csrc/game_init.c`, `csrc/main.c`.
Sheets: `newgame-jump8.png`, `newgame-data2.png`, `newgame-jump9-14.png`, and `newgame-mock.png`
(a composite built from the coordinates below).

There is a **single screen**, flowing OK → auto-fill → exit animation.

### Entry
- Menu START (status 0) → `loc_00401cc8` (MAIN:364): `_rich4_init_new_game(0)` with stage
  `0x4991b6 = 0` (TAIWAN/CHINA/JAPAN/U.S.A.).
- NEW STAGE (status 4) → `loc_00401cbf` (MAIN:361) sets stage = 1 (STAR/ANCIENT/DINOSAUR/ISLAND)
  first.
- Before the menu, map `0x4991b8` is set to 0 (MAIN:~345).
- Returns 1 for OK and 0 for EXIT/cancel; 0 goes back to the main menu.
- The campaign continue mode (arg 1, after a stage is won; "恭喜勝利過關！！") pre-fills slot 0 and
  auto-presses OK after 10 ticks. **UNVERIFIED** as a player-visible flow.

### Resources (NG:3749-3830)

| use | archive / entry / frame | size @ origin | notes |
|---|---|---|---|
| backdrop | jump.mkf entry `stage*4+map` (0–7) | raw 640×480 RGB555 | Shown at brightness level −16 (≈50 %) (`fcn_004552b7`). Reloaded when the map changes (NG:1859). Scrolls left 2 px per 100 ms tick and wraps at 640 (`fcn_00456180`), i.e. a 32 s loop. |
| portrait panel | jump.mkf 8:0 | 440×155 | 6×2 grid of 72×72 wells |
| settings panel | jump.mkf 8:`20*stage+1` (8:1 or 8:21) | 192×461 | Map list, OK, EXIT, six value boxes with ▼ (NG:480-500) |
| OK pressed / EXIT pressed | jump.mkf 8:2 / 8:3 | 80×40 | |
| ▼ pressed | jump.mkf 8:4 | 24×25 | |
| drop-down lists | jump.mkf 8:5 (42×71, 3 rows), 8:6 (67×140, 6 rows), 8:7 (87×140, 6 rows wide) | | |
| map check ✓ | jump.mkf 8:8 | 27×25 | transparent |
| eliminated ✗ | jump.mkf 8:9 | 50×52@(25,26) | |
| stage-clear star / banners | jump.mkf 8:10, 8:11–20 | | stage-clear screen only |
| portraits | Data.mkf 2:0–11 | 72×72 | character id 0–11 |
| riding sprites | jump.mkf entry `9 + 3*character + traffic` (9–44) | SPR, bottom-centre origin (e.g. 9:0 128×143@(65,143)) | `_rich4_select_nth_player` (NG:1135) |

Panels are composed off-screen:
- Portrait panel (440×155) is blitted at **(4,10)**.
- Settings panel (192×461) is blitted at **(445,10)**.

### Portrait grid (`fcn_0040423c`, NG:204-460)
Character *i* sits in the well at `(8+72*(i%6), 15+72*(i/6))`. The state byte is `0x4990f4[i]`:

| state | meaning | drawing |
|---|---|---|
| 0 | available | plain portrait |
| 1 | picked | darkened −16 |
| 2 | eliminated | greyscaled (`fcn_004553da`), plus red ✗ 8:9 at the cell centre |

**Hover** on an available portrait (NG:1519):
- Redraw it raised at `(12+72c, 11+72r)`.
- Add a name tag:
  - Box: darken −20, 70×20, at `(13+72c, 83)` for row 0 or `(13+72c, 63)` for row 1.
  - Text: `font(16, 0xf0f0f0, 0x101010, 3, 1)`, align 2, at `(48+72c, 93)` or `(48+72c, 73)`.
- Play Effect 0.
- Names (stored with spaces): 約 翰 喬, 沙隆巴斯, 忍 太 郎, 錢 夫 人, 阿 土 伯, 莎拉公主,
  宮本寶藏, 糖 糖, 烏 咪, 孫 小 美, 小 丹 尼, 金 貝 貝.

### Settings panel (`_rich4_draw_game_config_menu`, NG:462-690)
- Map ✓ (8:8) at screen `(595, 30+32*map)`.
- Labels: `font(15, 0xffffff, 0x101010, 3, 0)`, align 5, at x=453.
- Values: `font(15, 0x101010, 0x101010, 2, 1)`, align 6, right edge x=600.

| row | label | centre y | values (index: text) — default | written on OK |
|---|---|---|---|---|
| 0 | 遊戲人數 | 238 | 0 二人, 1 三人, **2 四人** | `num_players = idx+2` (0x499114) |
| 1 | 總 資 金 | 274 | 0 300000, **1 200000**, 2 100000, 3 50000, 4 30000, 5 10000 (plain `%d`) | 0x49908c |
| 2 | 行進方式 | 310 | **0 步行**, 1 機車, 2 汽車 | 0x499118 (global; all players; dice = t+1; tool supply [t+3] −1) |
| 3 | 土地權限 | 346 | **0 無限期**, 1 二年, 2 一年, 3 六個月, 4 三個月, 5 一個月 | lease mode 0x499110 |
| 4 | 遊戲時間 | 382 | same six labels → days {0,730,365,182,91,30} — **0** | day limit 0x49911c |
| 5 | 勝利條件 | 418 | fund × {0,100,50,10,5,3}; 0 shows 無限 — **0** | target wealth 0x499108 |

### Hit rects
Table `0x46cc18`; right/bottom are exclusive (NG:1626).

| idx | rect | element |
|---|---|---|
| 0 | (8,15)–(440,159) | portrait cell `((y−15)/72)*6 + (x−8)/72` |
| 1 | (456,176)–(535,215) | OK (8:2 when pressed) |
| 2 | (544,176)–(623,215) | EXIT (8:3) |
| 3–8 | (602,226+36k)–(625,250+36k) | ▼ for rows 0–5 (8:4) |
| 9–12 | (457,31+32m)–(625,62+32m) | map rows 0–3 |

### Drop-downs
`fcn_0040482c` (NG:700); rects at `0x46cc88`, frames at `0x46ccb8`:

| row | list rect | frame | opens |
|---|---|---|---|
| 0 | (561,251)–(602,320) | 8:5 | down |
| 1 | (536,287)–(602,425) | 8:6 | down |
| 2 | (561,323)–(602,392) | 8:5 | down |
| 3 | (536,195)–(602,333) | 8:6 | **up** |
| 4 | (536,231)–(602,369) | 8:6 | **up** |
| 5 | (516,267)–(602,405) | 8:7 | **up** |

- Items are 23 px apart. Text is right-aligned at `right−2`, with item *n*'s centre at `top+13+23n`.
- The hovered item gets a 0xaa0000 fill at `(left+2, top+2+23n, w−3, 20)` and white text; other items
  are black.
- Any button-down commits the highlighted item and closes the list; right-click just closes it.
- Lowering the player count drops excess picks.
- Changing traffic reloads all sprites.

### Behaviour
- **Portrait click** (NG:1669-1711) toggles a pick and plays Effect 1. Picked characters are
  **human**, in click order.
- **Picked strip.** Each sprite animates with its feet at y=440 (one frame per 100 ms tick). x per
  slot (`0x46cb58`):

  | players | x per slot |
  |---|---|
  | 2 | 330, 110 |
  | 3 | 366, 220, 74 |
  | 4 | 385, 275, 165, 55 |

  The first pick is rightmost.
- **Buttons.** Pressed art and sound on down: OK = Effect 1, EXIT = Effect 2, ▼ = Effect 0. They act
  on up without re-testing the rect (NG:1991).
- **Map row** (NG:1859): move the ✓, play Effect 1, reload the backdrop. The map is restricted to the
  stage set chosen in the menu.
- **OK with ≥1 pick** (NG:2003):
  1. Auto-fill. Every 11 ticks (≈1.1 s) a random available character
     (`libc_rand() % available`, **gameplay PRNG**) fills the next slot as a computer (flag 0x80).
  2. Exit animation (NG:2578). The portrait panel slides up, the settings panel slides right and the
     sprites ride off right, all accelerating (step 6, increment 4..30). Any click or key skips it.
     Returns 1.
  3. OK with no picks does nothing.
- **EXIT / right-click** returns 0.
- **Applied on OK** (NG:4040-4170):
  - Controller: 1 = human (picked), 2 = computer.
  - Human cash = fund/2, bank = rest.
  - Computer cash = `trunc(init_cash_ratio × fund/100)`, bank = rest.
  - `fcn_00445a4d(player,k)` for k ∈ {1,2,3,4,8,9}: starting items, **UNVERIFIED** semantics.
- **Init clamp** (NG:1302): if non-eliminated characters ≤ player count, the count becomes
  available − 1.
- Music: `fcn_004549cf(0x8001)`. Track **UNVERIFIED**.

---

## 3. Bank

Source: `rich4_ui_bank.asm` (BK). The numeric pad is in `rich4.asm`. Sheets: `bank-panel23.png`,
`bank-panel2.png`, `bank-numpad-panel21.png`, `bank-atm-panel24.png`.

The original has **three bank screens plus a shared numeric pad**. Native merges all of them into one
`DecisionKind::bank` paper dialog.

Call order (`rich4_player_core_actions.asm:2549-2555`):
- On stop: the ATM, then the loan desk with the building id (unless flag `0x46caf8`).
- At L2709: the ATM only. Presumably passing; **UNVERIFIED** which path is which.

### Player fields

| record +off | meaning | native field |
|---|---|---|
| +21 | controller (1 = human) | |
| +28 | cash | `PlayerState::cash` |
| +32 | deposit | `bank_balance` |
| +36 | loan | `loan` |
| +40 | special financing | `special_finance` |
| +44 | loan due date | `PlayerStatus::loan_due_date` |
| +59 | bank refuses all business (`&0x7f` days) | `days_rejected_by_bank` |
| +60 | lending/withdraw suspended (`&0x7f` days) | `days_bank_no_loans` |

### 3.1 ATM (`_rich4_ui_bank_atm_entry` BK:5054, callback `fcn_00436ef8` BK:4184) — most-seen bank screen

**Gate.** If +59 is set: popup `"銀行拒絕往來\n\n還剩%d天！"` (1000 ms), then return. Computer
players take the AI path (BK:5136).

**Panel.mkf entry 24 frames.**

| frame | size | use |
|---|---|---|
| 0 | 320×338 | ATM body, transparent at **(60,71)** over the board → (60,71)–(380,409) |
| 1 / 2 | 80×41 | lit 提款 (withdraw, mode 0) / lit 存款 (deposit, mode 1) |
| 3 | 43×41 | lit EXIT |
| 4 | 204×26 | lit gauge |
| 5–16 | 33×17 | pressed keys 7 8 9 4 5 6 1 2 3 C 0 ← |
| 17 / 18 | 49×25 / 57×25 | pressed MAX / Enter |
| 19–28 | 18×32 | display digits 0–9 (frame = ASCII − 0x1d) |
| 29 | 29×29@(14,14) | red "prohibited" mark |

**Hit rects.** Table `ref_00475888` (`rich4.asm:40984`), inclusive, shown in screen coordinates; hit
test at BK:4373. The pressed image is frame (index+1) at the rect's top-left.

| idx | rect | control |
|---|---|---|
| 0 | (117,120)–(197,161) | 提款 (mode 0) |
| 1 | (199,120)–(279,161) | 存款 (mode 1) |
| 2 | (281,120)–(324,161) | EXIT |
| 3 | (113,208)–(328,237) | gauge |
| 4–6 | x 118 / 157 / 196 (w 33), y 282–299 | 7 8 9 |
| 7–9 | y 301–318 | 4 5 6 |
| 10–12 | y 320–337 | 1 2 3 |
| 13–15 | y 339–356 | C 0 ← |
| 16 | (243,304)–(292,329) | MAX |
| 17 | (235,331)–(292,356) | Enter |

**Init** (BK:4226-4330).
- Frame 1 at (117,120); mode 0; max = deposit.
- If +60 is set:
  1. Frame 29 at (157,141) over 提款.
  2. Frame 2 at (199,120); force mode 1 with max = cash.
  3. Popup `"銀行暫停放款\n\n還剩%d天！"` (1500 ms).
- The value starts as "0". Cursor: Data 0:27 on entry, 0:41 on exit.

**Display** (`fcn_00436d3a` BK:4027).
- Digits are right-aligned: the last digit's left edge is at x=304, y=172; each earlier digit is 20 px
  further left; at most 10 characters.
- Gauge: copy the first `trunc(v/max×34)×6` px (0–204) of frame 4 to (118,210). The lit part is on
  the left.

**Behaviour.**
- **Mode buttons** act on down and play Effect 1. Switching resets the value to "0" and sets max to
  deposit (mode 0) or cash (mode 1).
- **Gauge** (down or drag, Effect 9). With x' = mx − 118:
  - x' < 0 → 0
  - x' ≥ 204 → max
  - otherwise min(max, (x'/6+1)·(max/34+1))
- **Other keys** play Effect 7 on down and act on up (BK:4770):
  - digit: max 10 characters; "0" is replaced; the value is clamped to max
  - C: reset to "0"
  - ←: drop the last character
  - MAX: set to max
  - EXIT: return 0
- **Enter** (BK:4902):
  - v = 0 → exit
  - mode 0: deposit −= v, cash += v, then `fcn_00436b0a(1)`. This may show
    `"銀行資金準備\n\n不足%d元\n\n由經營者%s墊付！"`.
  - mode 1: deposit += v, cash −= v
  - **The ATM closes after one transaction.**
- **Keyboard** (BK:4627):

  | key | action |
  |---|---|
  | `0`–`9` | digit |
  | `M` | MAX |
  | `C` | clear |
  | Backspace | ← |
  | Enter | Enter |
  | `H` | click the gauge at (220,223) |

- **Right-click**: exit.

### 3.2 Loan desk (`_rich4_ui_bank_entry` BK:3490, callback `fcn_00435062` BK:1889)
Full-screen office scene. Return immediately if +59 is set.

**Entry setup.**
- Credit snapshot = `rich4_calculate_player_wealth(p)`, taken **once at entry**.
- Director flag = building id in 6001–7999 and `commercial[id−6000].owner == p+1`.
- Music track 4.
- Computer players: AI repay/loan with popups (out of scope).

**Panel.mkf entry 23 frames.**

| frame | size | use |
|---|---|---|
| 0 | 640×480 | clerk office; EXIT baked at (548,431)–(628,471) |
| 1 | 344×240@(62,198) | closed blinds, drawn at (320,240) when the player is not the director |
| 2 | 640×480 | director office |
| 3–7 / 8–10 | 80×40 / 80×30 | clerk eyes / mouth |
| 11–12 / 13–14 | | manager eyes / mouth |
| 15 | 200×280 | account card |
| 16 / 17 | 114×40 | director button normal / lit |
| 18 / 19 | 80×40 | EXIT normal / lit |
| 20 | 137×165 | director info table |
| 21 | 195×142 | clerk heart balloon |
| 22 | 250×110 | manager balloon |
| 23 | 29×29@(14,14) | prohibited mark |

Panel.mkf 2:0–7 are the calendar season pictures (200×200).

**Static draw** (`fcn_00434186` BK:686).
- `font(26, 0x101010, -, 2, 1)`, align 2: "申請貸款" at (345,345) and "償還貸款" at (530,345). If +60
  is set, frame 23 goes at (345,345).
- Director mode adds a grey label: `font(14, 0x808080, -, 2, 1)`, "特別融資", centred at (492,245).
- Balloon: `fcn_0044ec30(frame21, 240, 80, dx=20, dy=0)`. Text is `font 20 bold`, align 4, centred at
  (357,151). It closes when the voice ends or after ≥2000 ms (`fcn_0044ee18`, `rich4.asm:23425`).

**Hit rects** (`ref_004757f8`, inclusive, tested on down, Effect 1):

| idx | rect | action |
|---|---|---|
| 0 | (548,431)–(628,471) | EXIT |
| 1 | (282,324)–(408,366) | 申請貸款 |
| 2 | (470,326)–(590,366) | 償還貸款 |
| 3 | (268,51)–(591,273) | window → director's office (director only) |

**Flow** (50 ms timer, state `c3dd`):
- **Open** (BK:1975).
  - If +60: popup "銀行暫停放款…".
  - If the greeting option (`cfg+1`) is on: `"#0075歡迎光臨\n大富翁銀行！"`, then
    `"#0076需要我為您\n服務嗎？"`. A click skips them.
- **申請貸款** (BK:2813; ignored if +60). The side panel slides in.
  - If loan ≥ snapshot: `"#0081很抱歉！\n金額已超過\n許可額度。"`.
  - Otherwise: `"#0078請輸入您要貸款\n的金額！"`, then the pad with max = snapshot − loan.
  - On v > 0:
    1. deposit += v, loan += v.
    2. Due date = date + 90 days, pushed past holidays (`fcn_00433b7e` BK:193).
    3. `"#0079您的貸款手續\n已經完成。"`, then `"#0080請於三個月內\n還清貸款。"`.
- **償還貸款** (BK:2839; ignored if loan = 0).
  1. `"#0082請輸入您預備\n還款的金額！"`, then the pad with max = loan.
  2. If v > cash + deposit: `"#0083很抱歉！\n您的現金不足。"` and re-prompt.
  3. Otherwise deposit −= v (any overflow taken from cash), loan −= v; at 0 the due date is cleared.
     `"#0084您的還款手續\n已完成。"`.
- **EXIT or right-click**: frame 19 while pressed, 18 on release. `"#0085謝謝您的惠顧！"`, then
  close.
- **Side panel slide.** The account card is at (x,0) and the calendar at (x,280). x moves 640→440 at
  −40 px per tick, and back out the same way.

**Account card** (`fcn_00433d6e` BK:363, at x=440).
- Frame 15, plus the player portrait at (482,40) minus its origin. Portrait archive **UNVERIFIED**.
- `font 12 white, outline 0x101010`: "現  金" at (450,80), "存  款" at (450,145), "貸  款" at
  (450,208), align 0.
- `font 22 white, outline, bold`: player name at (522,28).
- "$"+`num_to_currency(cash / deposit / loan)` right-aligned at x=620, y = 100 / 164 / 228.

**Calendar** (`fcn_00433f24` BK:494, panel top y=280).
- Background:
  - Normal day: Panel 2 frame `season[month−1]`, with `{3,0,0,0,1,1,1,2,2,2,3,3}`.
  - Holiday: a raw Data.mkf picture (`ref_00475208[stage*4+map]` + holiday). **UNVERIFIED**.
- Text is 0x101010 with a white outline, bold:

  | text | size | position | align |
  |---|---|---|---|
  | day | 60 | centre (500,376) | 2 |
  | 星期X | 16 | centre (454,352) | 3 (vertical) |
  | year | 24 | (580,288) | 0 |
  | "%d月" | 28 | centre (500,328) | 2 |
  | "距還款日%d天" (only if a loan exists) | 20 | x=460, y-centre 456 | 5 |

**Animation** (BK:2480-2715).
- Clerk eyes rect (140,114)–(220,154): frames [7,6,7,5], or a glance to frame 3+rand%3.
- Mouth rect (140,152)–(220,182): frames 9/10 while talking, 8 at rest.
- Uses `libc_rand` (cosmetic).

### 3.3 Director's office (`fcn_00434492` BK:924)
Reached through the window rect. Returns 1 if a transaction happened, which closes the desk.

**Layout** (on Panel 23 frame 2).
- Frame 20 at (10,125).
- Frame 16 at (11,305) and (11,362); frame 18 at (11,419).
- `font(20, 0xf0f0f0, bold)`: "週轉現金" centred at (67,324) (frame 23 over it if +60 is set) and
  "歸還款項" centred at (67,382).
- `font(16, 0x202020, bold)`: "客戶存款總額" / "目前融資金額" / "尚可融資金額" centred at (78,147),
  (78,195), (78,243).
- Values (`fcn_00433c20` BK:254) right-aligned at x=128, y = 163 / 211 / 259. They show T, special
  financing, and T − special, where T = the sum of the **other** players' deposits.
- Balloon: frame 22 at (214,50), dy −10.

**Hit rects** (`ref_00475818`): (11,305)–(125,345), (11,362)–(125,402), (11,419)–(91,459). Hover
lights frame 17 or 19.

**Flow.**
- Opening line: `"#0086董事長親自蒞臨\n不知有何指教？"`.
- **週轉現金.**
  - If special ≥ T: `"#0088行裡現在沒有\n這麼多現金。"`.
  - Otherwise: `"#0087請輸入您要\n週轉的金額∼"`, then the pad with max = T − special. Result:
    deposit += v, special += v.
- **歸還款項.**
  - If special = 0: `"#0090董事長您別開玩笑了∼"`.
  - Otherwise: `"#0089…還款的金額∼"`, then the pad with max = special. If v > cash + deposit, show
    #0090. Otherwise deposit −= v (overflow from cash), special −= v.
- **Exit**: `"#0091董事長慢走！"`.

### 3.4 Shared numeric pad (`fcn_00453544`, `rich4.asm:27476`; callback `fcn_00452c02` :26722)
Used by the loan desk, the director's office, stock buy/sell and trading.

**Panel.mkf 21 (SPR).**

| frame | size | use |
|---|---|---|
| 0 | 128×192 | pad body |
| 1 | 108×12 | gauge strip |
| 2–15 | key-sized | pressed images for key ids 2–15 |
| 16–25 | 9×19 | digits 0–9 (frame = ASCII − 0x20) |

**Panel.mkf 22** is a raw 128×192 per-pixel hit-id map (verified by histogram), in pad-local
coordinates:

| id | area | control |
|---|---|---|
| 1 | everywhere else | drag the pad |
| 2 | (8,64)–(54,87) | MAX |
| 3 | (64,64)–(119,87) | Enter |
| 4 / 5 / 6 | y 96–111 | C / 0 / ← |
| 7–9 | y 121–135 | 7 8 9 |
| 10–12 | y 144–159 | 4 5 6 |
| 13–15 | y 168–183 | 1 2 3 |
| 16 | (9,41)–(118,54) | gauge |

Key columns start at x = 8, 48, 88. Pressed-image rects are in `ref_0047e6d8`.

**Layout.** The pad starts at (256,144) with the background saved; the value starts as "0".

**Display** (`fcn_0045297e` :26505).
- Digits are right-aligned at pad + (107,11), 12 px apart, at most 9.
- Gauge: frame 1 at pad + (10,42). Its leftmost `ref_0047e725[trunc(v/max×33)]` px (3, 6 … 107) are
  overwritten with frame-0 pixels. Which shade reads as "filled" is **UNVERIFIED**.

**Input.**
- Dragging id 1 moves the pad, clamped to x 0–512 and y 0–288.
- Keys play Effect 7 and act on up:
  - MAX: v = max
  - Enter: return the value
  - C: "0"
  - ←: backspace
  - digits: clamped to max
- Gauge drag (Effect 9): v = max·i/33.
- Keyboard: `0`–`9`, `M`, `C`, Backspace, Enter, `H` (gauge at pad + (64,47)).
- **Right-button-up cancels** (Effect 4) and returns 0. Callers cannot tell cancel from an entered 0.

---

## 4. Stock market

Source: `rich4_ui_stock.asm` (ST). Sheets: `stock-75.png`, `stock-76.png`, `stock-21.png`.

Entry: `_rich4_ui_stock_entry(mode, …)`. The main callback is `fcn_0042aaff` (ST:1729).

| mode | used by |
|---|---|
| 0 | normal, from the toolbar |
| 1 | red-card (紅卡) target, `rich4_card_hongka.asm:90` |
| 2 | black-card (黑卡) target, `rich4_card_heika.asm:68` |

### Artwork

| image | archive:entry:frame | size | placement |
|---|---|---|---|
| price page | Panel 75:0 | 640×480 | (0,0); green grid and top bar |
| holdings page | Panel 75:1 | 640×480 | (0,0); orange grid with 7 columns |
| company-info popup | Panel 75:2 | 587×375 | (26,52) |
| company pictures | Panel 75:3–28 | 80×112 | (50,107) inside the popup |
| dividend table | Panel 76:0 | 592×432 | (24,24) on a black screen |
| quantity pad | Panel 21 / 22 | | §3.4 |

Grid geometry (from the black grid-line pixels):
- Frame 0 column lines at x = 15, 136, 240, 320, 416, 520, 624.
- Frame 1 column lines at x = 15, 128, 208, 288, 368, 448, 536, 624.
- Top bar y 8–39; header row y 48–79; data row k (0–11) at y `80+32k … 111+32k`.

### Top bar
Hit table `ref_004754c8` (ST:3708). The test is strict on all sides and only runs when y < 39
(ST:1920-1950).

| code | rect | label (centre) | enabled |
|---|---|---|---|
| 1 | (16,9,124,39) | Page 0 shows "持有股數表", page 1 shows "股價表" (the label names the other page); (70,24) | always |
| 2 | (128,9,198,39) | 買進 (163,24) | row selected, mode 0 |
| 3 | (202,9,272,39) | 賣出 (237,24) | row selected, mode 0 |
| 4 | (276,9,410,39) | 上市公司資訊 (343,24) | row selected, mode 0 |
| 5 | (552,8,623,40) | EXIT (in the art) | always |

- Labels: `font(16, 0xf0f0f0 or 0xa0a0a0 when disabled, 0x101010, 3, 1)`, align 2
  (`fcn_004296c1` ST:177-268).
- Pressed state is the generic bevel (`fcn_00451b9e`); there is no hover art.
- The box at x 414–548 shows the **bank deposit** (+32), right-aligned (align 6) at (540,24),
  font 16 white with shadow (ST:333-347).

### Page 0 — 股價表
Headers are baked into the frame: font 16 white with shadow, centred at y=64. Row centre
y = 96 + 32i.

| column | header centre x | value (`fcn_004297f7` ST:333-600) |
|---|---|---|
| 股票名稱 | 76 | name centred, baked; cyan 0x00f0f0 if listed on this map (`flags_4` ≠ 0), else white |
| 成交價 | 188 | price `float_bits[2]`, right-aligned x=225; `%.2f` if <15, `%.1f` if <150, else `%.0f` (ST:154-175) |
| 漲跌 | 280 | `float_bits[2] − [1]`, right-aligned x=305, `%+.2f` / `%+.1f` / `%+.0f` |
| 交易量 | 368 | `"%d"` of `flags_10`, right-aligned x=401; "暫停交易" centred if `suspension_days` ≠ 0 |
| 持有股數 | 468 | holding, currency format, right-aligned x=504; blank if 0 |
| 平均成本 | 572 | `raw_cost_basis` as float `%.2f`, right-aligned x=609; only when holding > 0 |

Trend colouring (`stocks::trend`, ST:270-455):

| trend | name / price | change |
|---|---|---|
| rising | red 0xff0000 | red |
| limit_up | fill (144, y−10, 89×20) with 0xd00000, white bold text (flags 2) | red |
| falling | green 0x00ff00 | green |
| limit_down | fill the same rect with 0x00d000, black text | green |
| unchanged | white | white |

**Selection.**
- Click a row in normal mode, page 0: inside 15<x<625, 80<y<464, row = ⌊(y−80)/32⌋.
- It gets a 1 px white outline (`fcn_0045620f`) at (15, 48+32·(stock+1), 610×32). The old outline
  is erased with colour 0.
- Clicking the selected row again opens the info popup (ST:2207).

### Page 1 — 持有股數表
- Baked text (ST:2796-2840):
  - "股票名稱" at (71,64).
  - Player names centred at (168+80p, 64).
  - "保留股份" (492,64) and "累積盈餘" (580,64).
  - Stock names centred at (71, 96+32i).
- Per player: shares right-aligned at x = 192+80p.
- Manager cell (listed company and `owner_code == p+1`): fill (x−56, y−10, 64×20) with blue 0x0000ff,
  yellow 0xf0f000 bold text.
- 保留股份 = `shares_remaining`, right-aligned x=520. 累積盈餘 = `funds`, right-aligned x=609
  (listed companies only).
- Rows are not selectable on this page.

### Actions (ST:2017-2165)
- **買進.**
  - Ignored if suspended.
  - If limit_up: popup "漲停無法買進！".
  - Otherwise max = min(`flags_10`, trunc(bank/price)); ignored if 0.
  - qty = pad(max). Then a sound, `buy_stock(p, i, qty, from_market=1)`, redraw.
- **賣出.**
  - Needs holding > 0 and not suspended.
  - If limit_down: "跌停無法賣出！".
  - Otherwise qty = pad(holding), then `sell_stock(..., credit_bank=1)`.
- **Code 1** toggles the page. **Code 4** opens the popup. **Code 5 or right-click**: on page 1, go
  back to page 0; on page 0, close (Effect 4).
- After closing: `fcn_0041906a(1)` (redraw the board) and `fcn_00436b0a(0)` (financing
  reconciliation).
- **Market closed** (holiday or `news_effect_days`; ST:2722):
  - Frame 0 with "本日休市" in font 72 bold: black at (324,244), then white 0xf0f0f0 at (320,240),
    both centred.
  - Any click closes it. This check also pre-empts card-target modes.
- **Card modes 1 and 2** (ST:1786-2205):
  - Rows are selected by hover outline.
  - On release: set `news_movement` = 0x20 (red card) or 2 (black card), then
    `update_stocks(stock+1)`, redraw, wait 1000 ms, return stock+1.

### Company-info popup (`fcn_00429d65` ST:761-1712)
- Saves the rect (26,52,613,427), then draws Panel 75:2 at (26,52).
- Title: stock name, font 20 white with shadow, centred at (320,82).
- **Picture** (listed companies only): frame from `ref_00475530[(stage*4+map)*12 + commerce_type−1]`
  (ST:3746). Value 15 adds (level − 3); value 24 adds (level − 2). Drawn at (50,107).

  | maps | frames for commerce types 1–12 |
  |---|---|
  | m0, m1, m3 | 11,0,7,5,6,9,3,0,0,4,10,0 |
  | m2 | 11,0,8,5,6,9,3,0,0,4,10,0 |
  | m4 | 11,0,7,12,6,9,3,0,0,4,10,0 |
  | m5 | 11,23,7,14,6,9,3,0,0,4,10,15 |
  | m6 | 11,0,7,13,6,9,3,0,0,4,10,0 |
  | m7 | 11,24,7,5,6,9,3,0,0,4,10,0 |

- **Labels** (font 16, align 6, right edges):
  - x=212: 本月盈餘 y123, 平均盈餘 y162, 經 營 者 y203.
  - x=395: 成交價 y123, 漲  跌 y162, 週均價 y203.
  - x=531: 交易量 y123, 漲跌幅 y162, 月均價 y203, 歷史高價 y243, 歷史低價 y283.
  - Font 12: "半年內股價走勢線圖" centred (236,248); "持股比例" centred (502,318).
- **Values** (right-aligned):

  | value | right edge, y | source |
  |---|---|---|
  | earnings | x=309, y123 | `funds` |
  | average earnings | x=309, y162 | `funds_change` ÷ `month_counter` (raw value if the counter is 0) |
  | manager | centred (269,203) | player name |
  | price | x=453, y123 | current price |
  | change | x=453, y162 | Δprice |
  | week average | x=453, y203 | mean of the last 6 non-zero samples |
  | volume | x=589, y123 | `flags_10` |
  | change % | x=589, y162 | Δ/prev·100, `%.2f` |
  | month average | x=589, y203 | last 24 samples |
  | high / low | x=589, y243 / y283 | over all 144 non-zero samples; low starts at 10000 |

- **Price-trend chart** (ST:1450-1635). GDI, white 1 px pen.
  - History is 144 floats at `0x497328 + stock*576`, ring cursor `0x499100` (native
    `stock_history_bits` / `stock_history_cursor`).
  - M = (H+L)/2, R = H−L. Scale s = 109/R if R/M > 0.3, else 109/(0.6·M).
  - Point k: x = 92 + 2k, y = trunc(324 − (v−M)·s).
  - The walk starts at the cursor if `history[cursor]` ≠ 0 (ring full), else at 0, and stops at the
    first zero sample. MoveTo the first point, LineTo the rest.
  - H and L labels: font 12 white, right-aligned at x=88, at y(H)−8 and y(L)+8.
  - The background has six coloured month bands of about 48 px over x 91–381, y 254–384 (measured;
    ±2 px **UNVERIFIED**).
- **Shareholding pie** (ST:1262-1449). f = player shares / 10000. Black pen.
  - Base ellipse (414,343,591,401) in blue 0xd00000 (COLORREF BGR, i.e. RGB blue).
  - Side edges: x=414 and x=590, y 365–372.
  - Top face (414,336,591,394): the unowned part in blue, the owned part in red. The angle starts at
    12 o'clock and runs clockwise. Edge point: x = 502 + 88cosθ, y = 365 − 29sinθ, θ = π/2 − 2πf.
  - If f > 0.25, flood-fill the rim at (589,374).
- **Close**: L or R button up restores the rect.

### Monthly dividend screen (`fcn_0042ba97` ST:3052; called monthly from `rich4_player_core_actions.asm:4838`)
Black screen with Panel 76:0 at (24,24).

| element | font | position |
|---|---|---|
| title "上市公司分紅" | 28 white with shadow | centred (320,49) |
| corner labels "人名" / "公司" | 12 black bold | (128,106) align 6 / (42,118) align 5 |
| player names | 16 black bold | centred (184+98c, 112) |
| "本月盈餘" | 16 black bold | centred (566,112) |
| company rows | 16 black bold | centred (86, 140+24r) |
| per-player dividend | 16 black bold | right-aligned x = 222+98c |
| company earnings | 16 black bold | right-aligned x=596 |
| totals row "紅  利" | 16 black bold | label at (86,428); totals right-aligned at (222+98c, 428) |

- Dividend = trunc(funds × shares / Σ active shares); the company's `funds` is then reset. Payouts go
  to the bank deposit.
- Effect 61 plays once on open (preset `ref_004755a8`).
- Window proc `fcn_0042b3eb` (ST:2514): black 640×480, 76:0 opaque at (24,24), `SetTimer(1000 ms)`.
  It closes on a left **or right** button-up, or by itself on the third tick (about 3 s). `0x46cb01` is
  only the app-active flag (`WM_ACTIVATEAPP`, rich4_main.asm:169/175): the timer pauses while the
  window is inactive.
- The screen is shown every 15th (core_actions:4836, before the lottery draw), with a row per listed
  stock (`stocks_on_map+4` ≠ 0). After it closes, each active player's total is added to the deposit
  (`+0x20`); a negative deposit is moved to cash, and negative cash is zeroed and bankrupts the
  player (`_rich4_player_bankrupt`). The share ratio is FDIVP stored as binary32 (`fstp dword`); the
  payout is FILD funds × ratio, truncated. Native: `pay_dividends` in `src/game.cpp`, screen
  `tools/screens/monthly.cpp`.

---

## 5. Player info / wealth view (查詢)

Source: `_rich4_ui_player_info_view_entry` (`rich4_ui_player_info_view.asm:677`). The drawing code is
in `rich4.asm:12808-14430`. Sheets: `info-9.png`, `info-74.png`.

Note: the full-screen Panel.mkf entry 9 pages belong to **this view**, not to stocks or the bank.

### Assets
- Panel 9:
  - 9:0 / 9:1 / 9:2: pages (640×480).
  - 9:3 selected player tab, 88×33. 9:4 other tabs, 88×32.
  - 9:5 EXIT, 58×19@(29,9). 9:6 EXIT pressed, 53×18@(27,9).
  - 9:7–10 scroll arrows, 30×30.
  - 9:11 category tab, 75×33.
  - 9:12 pressed page button, 97×40.
  - 9:13–24 god figures.
- Panel 74:0–12: tool icons (centred origins).

### Common chrome (`fcn_00423070`, `rich4.asm:13766`)
- Page frame at (0,0). EXIT 9:5 at (547,23).
- Portrait at (60,100).
- If `god_info` ≠ 0:
  - God figure 9:`table[god]` at (60,188). Table `0x475464`: 0, 13..22, 0, 23.
  - `"%d天"` remaining days at (60,234), `font(16, 0x101010, 0, 2, 0)`, align 2.
- **Page buttons** "資產清單 / 地產清單 / 股票清單":
  - Centred at (60, 302+64i).
  - Selected: `font(20, 0xffffff, 0x101010, 3, 0)`; others the same font in 0xc0c0c0.
  - Hit x 12–109, y 282+64i … 322+64i. Down draws 9:12 at (12, 282+64i).
- **Player tabs** (active players):
  - Tab i at x = 16+88i: 9:3 at (x,14) if selected, otherwise 9:4 at (x,15).
  - Name at (x+44, 30): font 20, 0x101010, bold, align 2.
  - Hit x…x+88, y 14–47.
- **EXIT**: hit (492,9)–(602,38). Down draws 9:6. Release exits; right-click also exits.

### Page 0 — 資產清單 (`rich4.asm:13976`)
- Labels (font 20 black bold, align 2):

  | position | labels |
  |---|---|
  | (142, 88/136/184/232) | 現  金 / 存  款 / 貸  款 / 總資產 |
  | (430, same y) | 股  票 / 點  卷 / 保險期 / 企  業 |
  | (142, 296/344/392/440) | 土  地 / 連鎖店 / 房  屋 / 設  施 |

- Values: `font(28, 0x101010, 0, 2, 0)`, align 6.
  - Right edge x=330: cash, deposit, loan, wealth (`stocks::player_wealth`).
  - Right edge x=602: Σ holding×price, points, `"%d天"` insurance, `"×%d"` 企業.
  - Counts `"×%d"` right-aligned at x=250 for the lower-left labels. Exact counters **UNVERIFIED**.
- **Tool grid**: icon 74:type at (cx−16, cy), `"×N"` right-aligned at (cx+30, cy), font 16 white with
  shadow. cx = 300, 372, … ≤ 588 (wrapping); cy from 281, step 32.
- **Card grid**: the 15 hand slots in hand order, name centred at (cx, cy); cy from 385.

### Page 1 — 地產清單 (`rich4.asm:14394`)
- **Category tabs** 全  部 / 住宅區 / 商業區 / 房  屋 / 連鎖店:
  - At x = 120+75i, y 64–97. The selected tab is drawn at (120+75i, 64), frame 9:11 (**UNVERIFIED**).
  - Labels at (157+75i, 80), font 16.
- **Column headers** at (168/264/356/448/540, 112): 地  點 / 開發狀況 / 價  格 / 收  費 / 租  期.
- **Rows**: 10 visible at y = 144+32r. The redraw clears (120,128) 464×320.
  - Lease column shows `"%02d/%d/%d"` or 無限期.
  - Per-column value mapping **UNVERIFIED**; it matches the trading property picker (§9).
- **Scroll**:
  - Up: hit (593,369)–(623,399), pressed 9:7. Down: hit (593,417)–(623,447), pressed 9:8.
  - Keyboard: hotkeys at cfg+66 / +68 (向上換頁 / 向下換頁).

### Page 2 — 股票清單 (`rich4.asm:14430`)
- Headers at (204/332/476, 68): 股票名稱 / 持有張數 / 總 市 價.
- 12 rows at y = 100+32r:
  - name centred at 204;
  - holding right-aligned at 332;
  - value right-aligned at 476.
- Whether zero-holding rows are skipped is **UNVERIFIED**.

---

## 6. Save / load slots

Sources: `rich4_ui_save_load.asm` (SL), `rich4_save_files.asm`, `csrc/loadsave.c`. Sheet:
`saveload-520.png`.

### Assets (Data.mkf entry 520)

| frame | use |
|---|---|
| 520:0 | LOAD window 555×451 (tab, baked slot digits 0–5) |
| 520:1 | SAVE window 555×381 (digits 1–5) |
| 520:2–9 | map thumbnails 72×72, frame = 2 + map_id + 4·stage_id |
| 520:10 | pink date tile 72×72 |

Portraits come from Data 2:character.

### Files
- `SAVE%d.DAT`. LOAD shows rows 0–5, where **0 is "AUTO"** (load only). SAVE shows rows 1–5.
- A file counts as valid only when its first dword is 0x26 (SL:750).
- The preview header holds: date u32, map u16, stage u16, player_count u32, then 4 player records of
  0x68 bytes each (character at +0x13).
- Row font: `font(16, 0xf0f0f0, 0x101010, 3, 1)`.

### Load (`_rich4_ui_load_game(mode)` SL:647-996)
- If no valid slot exists: no window, return −1.
- Window 520:0, transparent, at (40,15).
- Row s has top y = 24 + 72s:
  - Date tile at (129, y).
  - "AUTO" at (165, y+15) (slot 0 only).
  - Year at (165, y+36); `"%d/%d"` at (165, y+57); all align 2.
  - Map thumbnail at (209, y).
  - Portraits at (289+72k, y).
- **Hover** (SL:111):
  - Band x 130–576, y 25–455; slot = (y−24)/72.
  - Effect 0.
  - Yellow 0xffff00 double outline: 452×76 at (127, 22+72s) and 450×74 at (128, 23+72s).
- **Click** on a valid row: Effect 2, return the slot. Empty rows ignore clicks.
- **Right-click**: Effect 4, return −1.
- Mode ≠ 0 (in-game) saves and restores the screen underneath.

### Save (SL:998-1069)
- Window 520:1 at (40,48). Slot n = 1..5 has row y = 57 + 72(n−1); band y 58–416.
- Clicking **any** row saves immediately, **with no overwrite confirmation**: Effect 2,
  `_rich4_save_game_to_file(n)`, redraw, about 500 ms pause, close.

---

## 7. Lottery (樂透)

Source: `rich4_ui_letou.asm` (LT). Sheets: `lottery-12-15.png`, `lottery-data517.png`.

### 7.1 Ticket purchase (`_rich4_ui_letou_bar_entry` LT:2901, callback `fcn_0042f7fc` LT:560)
Music 6, 100 ms timer.

Computer players get no UI: if cash > 1000, they take `rand()%free` and pay 1000 into the pool.

**Artwork.**

| use | archive:entry:frame | size | placement |
|---|---|---|---|
| bar with 36-number board | Panel 12:0 | 640×480 | (0,0) |
| cat girl normal / wink | Panel 12:1 / 12:2 | 358×298 / 414×302 | (210,−5) / (154,−8) |
| eye blink | Panel 12:3, 4, 3 | 70×42 | (273,63) |
| mouth | Panel 12:5 / 12:6 | 70×18 | (273,105) |
| number ring | Panel 12:7 | 58×47@(28,25) | (cell_x+32, cell_y+24) |
| balloon | Panel 12:8 | 237×192 | top-left (360,20); text centred at (498,116) |
| jackpot bar | Panel 12:9 | 172×28 | (28,27) |
| jackpot digits | Panel 13:0–9, 13:10 ",", 13:11 "$" | | right-aligned ending at x=184, y=41; 18 px per digit, 12 px per comma |
| neon sign | Panel 14 (FLC 0xAF12, 5 frames, 213×68) | | (8,8), advanced every tick |

**Grid.**
- Cell n (0-based) is at x = 30+64(n%9), y = 271+48(n/9), size 64×48. Hit area x 30–606, y 271–463.
- Sold cells are darkened −10 at (31+64c, 272+48r, 62×46).
- Picking n=2 or n=3 redraws the cat girl's paw (LT:1170).

**Flow.**
1. **Cash < 1000**: `"#0015太可惜了！\n您的現金不足∼"`, then leave.
2. **Intro** (only if `cfg+1` is on; a click skips it):
   - `"#0011哈囉！\n一券在手，\n希望無窮！"`
   - `"#0012只要一千元，\n就有獲得大獎\n的機會！"`
   - `"#0013請圈選您的\n幸運號碼∼"`
3. **Pick a free cell**:
   1. Draw the ring.
   2. Owner = p+1, cash −1000, pool +1000.
   3. Sound, then the wink with `"#0014拜拜！祝您中獎！"`.
   4. Close.
4. **Right-click**: `"#0016下次再來吧！"`, then close.
5. **One ticket per visit.**
6. Cursor: Data 0:28.

### 7.2 Monthly draw (`_rich4_ui_letou_kaijiang_entry` LT:3007, callback `fcn_0043010c`)
Music 8, 50 ms timer. Opens only if at least one ticket is sold. There is no mouse input.

**Artwork** (Panel 15 unless noted).

| use | frame(s) | placement |
|---|---|---|
| stage | 15:0 | (0,0) |
| right host | 15:1 | (472,66) |
| left host with board | 15:3 | (7,66) |
| host sweating | 15:4 | (489,116) |
| host cheering | 15:5 | (505,66) |
| host sign-up | 15:6 | (0,0) |
| faces | 15:7–21 | blink tables at (512,102) and (52,89) |
| bubbles | 15:22 / 15:23 / 15:24 | (300,47) / (320,200) / (320,200) |
| player heads | 15:25+char | |
| number balls | 15:37+digit, 71×70@(35,35) | tens at (286,405), units at (358,405) |
| big green digits | **Data 517:8+digit**, 35×43@(17,21) | (300,220) and (340,220) |
| cage spin | Panel 16 (FLC, 42 frames) | (183,75) |
| winner animation | Panel 17 (FLC, 37 frames) | (205,0) |

**Text.**
- Board "累積獎金": `font(20, 0x4f35b1, bold)` centred at (77,193); "$"+amount in red 0xff0000 at
  (77,228). On the winner layout they move to (91,19) and (91,56).
- **Ticket panels**, one per active player:
  - Origins: (16,340), (16,410), (328,340), (328,410). Each is a 296×60 area darkened −16.
  - Numbers as `"%02d"` pairs in Panel 13 digits at x = px+54+40i (second digit at +16).
  - Up to 6 numbers on one row at y+30; 7–12 use two rows at y+15 / y+45; more than 12 are not shown.

**Flow.**
1. `"#0017嗨！\n又到了每月\n十五號樂透\n開獎時間∼"` (only if `cfg+1` is on; when it is off, #0018 is
   skipped too and the cage starts on the first tick).
   Full timeline (bubbles, cage/winner FLC ticks, host faces, ticket panels): §14.2.
2. `"#0018現在馬上為您\n開出這一期的\n號碼．．．。"`.
3. The cage FLC plays for at least 20 ticks.
4. The number is drawn **inside the UI** (LT:2087):
   - If any player holds more than 10 tickets: `rand()%sold` over sold numbers.
   - Otherwise: `rand()%36+1`.
5. **Sold**:
   - `"#0019本月份的得主\n是．．．。"`.
   - Winner name (e.g. `"#0020約翰喬"`) in `font(28, red, 0x400000, 3, 4)` centred at (320,180).
   - `"#0032恭喜您獨得\n所有獎金！"`.
6. **Unsold**:
   - 500 ms pause, then the sweating host and `"#0033SORRY！\n本月份沒有人\n得獎∼"`.
   - `"#0034獎金將累積\n到下個月．\n．．．。"`.
7. Both paths end with `"#0035希望下次\n得獎者就\n是您！"` and `"#0036行動要快喔！"`.
8. On close:
   - Winner: payout, then clear the pool and all 36 tickets.
   - No winner: keep both.

---

## 8. Auction (拍賣)

Source: `rich4_ui_auction.asm` (AU). Sheets: `auction-26a.png`, `auction-26b.png`,
`auction-portraits0.png`.

Entry: `_rich4_ui_auction_entry(seller_slot, property_code, play_music)` (AU:2458).
- `property_code` 2001+ is land; 4001+ is a facility.
- Music 5 plays only if the third argument is non-zero.
- 100 ms timer; callback `fcn_0043a2dd`.

### Artwork (Panel 26 plus per-character sprites)

| use | frame | size | placement |
|---|---|---|---|
| room | 26:0 | 640×480 | (0,0) |
| auctioneer idle | 26:25 | 154×352 | (123,24) |
| auctioneer gavel (sold) | 26:21 | 199×349 | (124,27) |
| assistant | 26:17 | 109×388 | (67,63) |
| property on the table | 26:N | e.g. 26:30 55×53@(26,−8) | anchor (232,180) |
| announcer balloon | 26:2 | 244×100@(122,50) | (288,10)–(532,110) |
| bid starburst | 26:1 | 142×113@(74,59) | anchor (480, y−15) |
| bid buttons normal i=0..6 | 26:(2i+4) | 94×46@(43,19) | anchor (406, Y[i]) |
| bid buttons pressed | 26:(2i+3) | 87×39@(43,19) | same anchor |
| current-bidder outline | 26:78+char | | (590, y) |
| bidder: bidding / cannot bid / winner | Panel (27+3c) SPR 5 frames / (28+3c) 1 frame / (29+3c) 5 frames | | feet at (590, y) |
| blink and mouth overlays | 26:26–29, 26:22–23, 26:18–19 | | AU:1866-2245 |

Property picture index N (AU:2484-2620):
- Vacant land with no owner: 90.
- Vacant land owned by a player: 91+owner_char.
- Built land: 29+5·type+level (115+… on stage 1).
- Facilities: separate tables. **UNVERIFIED** in detail.

Buttons, top to bottom: PASS, +100, +500, +1000, +5000, +10000, 放棄 (give up).
- Y[] = 133, 181, 229, 277, 325, 373, 421 (`ref_00475b84`).
- Hit test: x 363–449, y Y[i]±19.

Bidder column: slot k sits at (590, 80+120k).

### Text
- Setup font `(18, white, 0x101010, 3)`: "標價：" at (182,242) align 0; the price `"%d元"`
  right-aligned at (272,262).
- **Bidder status.** Cash `"%d元"` at (620, y+14), align 6. A status string replaces it, centred at
  (590, y+14): 住宿中 / 消失中 / 坐牢中 / 住院中 / 冬眠中 / 夢遊中 / 賣方. Ineligible bidders are
  drawn, then excluded.
- **New bid**: `font(18, 0xf0d020, black, 3)` at (620, y+25), align 1. The centre price is redrawn.
- **Starburst text**: `font(16, black, 2)` centred at (480, y): `"%d元"`, ＰＡＳＳ or 放棄.

### Lines
- `"#0131公開拍賣土地一處。"` (on open).
- `"#0132底價%d元\n請意者出價。"` (start).
- `"#0148無人出價，宣佈流標。"` (no bids).
- `"#0135%d元成交"` (sold).
- `"#0136…#0147恭喜<name>購得此地！"` (per-character congratulations).

### State machine (AU:412-2420)
- **Human bidder.** All 7 buttons are always shown; there is no disabled art.
  - Down shows the pressed frame; up posts the action. The release point is not checked.
  - Price > cash triggers an automatic give-up.
  - A raise above the bidder's cash is silently ignored.
- **Computer bidder.** The buttons are hidden by restoring (363,114)–(457,448).
- **Bid animation** (about 1 s):
  - Starburst, plus sprite frames 1..4,0.
  - The next non-passed bidder follows. When one bidder remains after a bid, the property is sold.
- **Sold sequence**:
  1. Gavel pose and "成交".
  2. Winner celebration for 60 ticks (about 6 s).
  3. Congratulations line.
  4. Close.
  5. Map redraw after a 1 s wait; the winner pays.

---

## 9. Player sale / trading market (公佈欄)

Source: `rich4_ui_sale.asm` (SA). Sheets: `sale-73.png`, `sale-74.png`, `sale-portraits.png`.

### Entry and lifecycle
- Entry: toolbar index 9 → `_rich4_ui_sale_entry`. Computer players run the AI path `loc_0042886e`
  (SA:5636).
- On entry: `fcn_0042483e` (prune offers).
- On exit: `fcn_0041906a(1)` and `fcn_00436b0a(0)`.
- Offers age by 1 per day (`fcn_00428475` SA:5299).

### Artwork (Panel 73, all origins (0,0); Panel 74 = tool icons)

| frame | size | use / placement |
|---|---|---|
| 73:0 | 596×348 | cork board with SALE and EXIT buttons, at **(22,66)** |
| 73:1 | 336×416 | stock picker, at (152,32) |
| 73:2 | 416×416 | property picker with 5 tabs, at (112,32) |
| 73:3 / 73:4 | 360×128 | tool / card picker, transparent at (140,160) |
| 73:5 | 184×88 | prompt box, at (227,42) (above the pad) or (227,196) (timed message) |
| 73:6 / 7 / 8 | 192×224 / 192×256 / 192×288 | detail card: tool or card / stock / property; centred, i.e. (224,128) / (224,112) / (224,96) |
| 73:9–12 | 72×72 | offer icons, pink: stock, property, tool, card |
| 73:13–16 | 72×72 | offer icons, blue |
| 73:17 | 144×96 | SALE pop-up menu, at (464,116) |
| 73:18 | 21×21 | pressed X |
| 73:19 | 80×32 | active property tab |
| map.mkf 27+char frame 0 | | seller portrait, centred at (66, 150+72p) |

- Offer icon frame = `8 + kind + 4*sex` (kind 1 stock, 2 property, 3 tool, 4 card). Pink for sex 0;
  the gender reading is **UNVERIFIED**.
- Fonts (SA:5361-5606):

  | font | args | use |
  |---|---|---|
  | A | `(16, 0xf0f0f0, 0x101010, 3, 1)` | general text |
  | B | `(16, 0xffffff, 0x800000, 3, 1)` | red buttons |
  | C | `(20, 0x101010, 0, 2, 1)` | tab labels |
  | D | `(20, 0xffffff, 0, 2, 1)` | column headers |

### Board
- **Offer grid**: cell (p, c) at **(104+72c, 114+72p)**, 72×72. Slots are compacted from column 0.
- **SALE** (x 465–535, y 75–113):
  - If slot 6 is in use: "公佈欄已滿\n\n請先撤件！" (1500 ms).
  - Otherwise, press-drag-release over pop-up 73:17. Cell = `(x−473)/63*2 + (y−125)/39`; hovered cells
    are darkened −12.
  - Release opens a picker: 0 stock, 1 property, 2 tool, 3 card.
- **EXIT**: x 537–607, y 75–113. Right-click also closes.
- **Offer cell**: press darkens it; release opens the detail card.

### Pickers
- **Stock picker.**
  - Headers 股票名稱 / 持有張數 / 總 市 價.
  - Rows from y=80, step 32: name centred at x=200; holding right-aligned at 332; value right-aligned
    at 478.
  - White hover outline 336×32.
  - Flow:
    1. "請輸入欲賣出的張數": pad with max = holding.
    2. "請輸入欲拍賣的價格\n\n（市價：%d元）": pad with max = 10× market.
    3. Post.
- **Property picker.**
  - Tabs 全  部 / 住宅區 / 商業區 / 房  屋 / 連鎖店 at y 33–61.
  - Columns 地  點 / 開發狀況 / 價  格 / 收  費 / 租  期 at x = 147c / 231c / 319r / 395r / 471c
    (c = centred, r = right-aligned).
  - 11 rows at y = 112+32r.
  - Scroll column x 513–527: X / ▲ / ▼.
  - Price prompt as above.
  - Filtering and sorting (`fcn_00423b3b`) **UNVERIFIED**.
- **Tool / card pickers.**
  - 5×3 cells of 72×32.
  - Tool cell: icon plus `"×%d"`. Card cell: the 15 hand slots in order.
  - EXIT cell: x 429–499, y 161–191.
  - Market = table byte × 100 × price index; price max = 10×.

### Detail card
- Seller name; value rows by kind (SA:3923-4330).
- Left red button: "撤 件" for your own offer (removed with no confirmation), otherwise "購 買"
  (`rich4_ui_yesno(320,240)`, then buy).
- Right button: EXIT.
- Messages:
  - "您的現金不足！" (not enough cash)
  - "恭喜您獲得經營權！" (you gained control of the company)
  - "道具欄已滿\n\n無法購買！" (tool inventory full)
  - "卡片欄已滿\n\n無法購買！" (card inventory full)

---

## 10. Options

Source: `_rich4_ui_options_entry(mode)` (`rich4_ui_options.asm:2788-3077`). Sheet: `options-3.png`.

Mode 0 comes from the main menu; mode 1 from the in-game toolbar.

### Assets (Data.mkf entry 3)
- 3:0 window 347×363, centred at **(147,59)**. All coordinates below are window-local.
- 3:3 / 3:4: 取消 / 確定 pressed.
- 3:5: volume or speed block.
- 3:6: pressed overlay for the big buttons.
- 3:9: red check dot.
- 3:10 / 3:11: big-button panel for mode 0 / mode 1.
- 3:1: hotkey sheet. 3:2: date dialog.

### Labels
Baked at entry with font 15 bold black:

| label | position, align |
|---|---|
| 遊戲速度 | (14,25), 5 |
| 動畫過程 | (14,58), 5 |
| 音 樂 | (14,90), 5 |
| 音 效 | (14,122), 5 |
| 自動存檔 | (14,155), 5 |
| 樂  曲 | (49,202), 2 |
| 視  窗 | (209,202), 2 |
| 日、月曆 / 縮小地圖 / 組合畫面 | (286, 226/258/290), 2 |

- 取 消 at (224,328) and 確 定 at (296,328), font 20.
- Big buttons (on panel 3:10+mode, copied to (168,2)):
  - mode 0: 日期更改 / 熱鍵設定 / 遊戲說明;
  - mode 1: 重新遊戲 / 認輸投降 / 結束遊戲.

### Config
A 16-byte copy of `_global_rich4_cfg` (0x497158) is edited and committed on 確定.

| offset | meaning | values |
|---|---|---|
| +0 | speed | 0–2 |
| +1 | animation / dialogue | 0/1; gates the bank and lottery greetings |
| +2 | music volume | 0–4 |
| +3 | sound volume | 0–4 |
| +4 | autosave | 0/1 |
| +5 | window set | 0–2 |
| +8 | start date | |
| +66 / +68 | page-up / page-down hotkeys | |

### State drawing (`fcn_0040fd49`)
- Speed: (speed+1) blocks of 3:5 at (81+16i, 17).
- Volume blocks at (89+16i, 81) for music and (89+16i, 113) for sound.
- Check dots: animation (98,50), music (66,82), sound (66,114), autosave (98,146), window set
  (218, {218,250,281}).
- **Song list**: 8 songs at (26, 233+15i), `font(12, 0xf0f0f0, 0x101010, 3, 1)`, align 5. The playing
  track gets a red 0xff0000 159×14 bar.
  1. 1.星際總動員
  2. 2.重回侏儸紀
  3. 3.夢幻伊甸園
  4. 4.打拼為將來
  5. 5.椰林風情畫
  6. 6.浪漫月世界
  7. 7.熱情的夏夜
  8. 8.漫步星空下

### Hit table
`0x474b92`, half-open rects, window-local.

| # | rect | action |
|---|---|---|
| 0 | (81,17)–(128,33) | speed = (x−81)/16 |
| 1 | (89,81)–(152,97) | music volume = (x−89)/16+1 |
| 2 | (89,113)–(152,129) | sound volume, same formula |
| 3–5 | (227,14)–(327,49), (227,68)–(327,103), (227,119)–(327,154) | big buttons |
| 6 | (18,226)–(177,345) | play song (preview only, not stored) |
| 7 | (194,314)–(256,344) | 取消 |
| 8 | (266,314)–(328,344) | 確定 |
| 9 | (98,50)–(113,65) | animation toggle |
| 10 | (66,82)–(81,97) | music toggle |
| 11 | (66,114)–(81,129) | sound toggle |
| 12 | (98,146)–(113,161) | autosave toggle |
| 13–15 | (217,214)–(324,236), (217,246)–(324,268), (217,278)–(324,300) | window set 0–2 |

**Big buttons**:
- Mode 0: date dialog / hotkey editor / help.
- Mode 1: confirm with `rich4_ui_yesno(320,200)`, then:
  - 重新遊戲: restart;
  - 認輸投降: `_rich4_player_surrender`;
  - 結束遊戲: quit.

**確定** writes the config file and applies music and volume.

---

## 11. AI settings (託管AI)

Source: `_rich4_ui_ai_settings_entry` (`rich4_ui_ai_settings.asm:842-1094`). Sheet: `ai-77.png`.

### Assets (Panel.mkf entry 77)
- 77:0: window 435×355, at **(102,62)**.
- 77:1 / 77:2: tab / selected tab.
- 77:3: red dot.
- 77:4 / 77:5: pressed arrows.
- 77:6–17: faces, indexed by character + 6.

### Baked text (screen coordinates)
- `font(20, 0xf0f0f0, 0x101010, 3, 1)`, align 2: 託管AI (275,88), 個  性 (275,176),
  資金運用比例 (351,308).
- Font 16, align 2: 使用卡片 (346,115), 使用道具 (346,146), 乖寶寶 (346,203), 普通人 (346,235),
  大老奸 (346,268).
- 現金 right-aligned at (293,339) / 存款 left-aligned at (407,339).
- 股票 right-aligned at (293,372) / 資金 left-aligned at (407,372).
- Vertical text, `font(20, 0x101010, 0, 2, 1)`: 確定 (499,187), 取消 (499,275).

### Players
- Only human players (controller bit0) are listed, slot i at y offset 83i.
- Each listed player gets tab 77:1 and face 77:(char+6) centred at (182, 110+83i).
- Selected tab: 77:2 at (110, 70+83i). 託管-on dot: 77:3 at (121, 106+83i).

### Edited bytes

| offset | meaning | native name |
|---|---|---|
| +21 | bit 0x4 = 託管 | `controller_flags` |
| +22 | bit0 = cards, bit1 = tools | `card_strategy` |
| +23 | 個性 0/1/2 | `rescue_strategy` (misnamed) |
| +25 | cash vs deposit, 0–100 | `init_cash_ratio` |
| +26 | stock vs funds, 0–100 | `stock_investment_ratio` |

### Hit rects
Exclusive, screen coordinates; act on mouse up.

| rect | action |
|---|---|
| tab x 110–226, y 70+83i … 153+83i | select the player; clicking the already-selected tab toggles 託管 |
| (287,106)–(383,124) | toggle 使用卡片 |
| (287,137)–(383,155) | toggle 使用道具 |
| (287,194/226/259)–(383,+18) | 個性 = 0 / 1 / 2 |
| (298,328)–(309,350), (392,328)–(403,350) | cash ∓10 |
| (298,361)–(309,383), (392,361)–(403,383) | stock ∓10 |
| bars (310,327)–(390,351) and (310,360)–(390,384) | drag: value = ((x−310)/8)·10 |
| 確定 (479,159)–(519,215) | write back and close |
| 取消 (479,247)–(519,303) | close, no write |

- Bars are drawn as ten 7×22 red 0xff0000 cells at (311+8i, 328) and (311+8i, 361).
- Dots: 使用卡片 (288,107), 使用道具 (288,139), 個性 (288, {195,227,259}).

---

## 12. Native mapping

The native API is in `include/rich4/game.hpp` (`GameSession`, `DecisionKind`),
`include/rich4/world.hpp` (`NewGameOptions`, `NewGamePlayer`, `World`), `stocks.hpp`, `trading.hpp`,
`auction.hpp` and `lottery.hpp`. The UI is in `tools/native_game.cpp` (`Screen` enum, `draw()`,
`draw_stocks`, `draw_trading`, `draw_decision`).

| original screen | native entry today | API to drive it | notes |
|---|---|---|---|
| Main menu | `Screen::menu`, Data 1:0 plus hit boxes | — | Add hover frames, EXIT, sounds and "V3.11". NEW STAGE should set `stage_id=1` and open setup. |
| New game | `Screen::new_game` dialog | `NewGameOptions{map_id, stage_id, initial_fund, lease_mode, players[{character, controller 1/2, traffic_method}]}` → `World::from_map_resource` | Count = `players.size()` (2–4). Humans are the picked characters in click order; auto-filled players get `controller=2`. Traffic is one global choice applied to every player. |
| ATM / loan desk / director | `DecisionKind::bank` paper dialog | `respond(option, amount)`; values 1 存款, 2 提款, 3 贷款, 4 还贷, 5 融资, 6 归还融资, 0 离开 | ATM 提款 → 2, 存款 → 1. Desk → 3 / 4. Director → 5 / 6. Send 0 after one ATM transaction to match the original. |
| Numeric pad | `amount_control` (+/- control) | the amount passed to `respond` / `buy_stock` / `post_offer` | Replace with Panel 21/22 art and behaviour. |
| Stock market | `Screen::stocks` table | `buy_stock(i, qty)`, `sell_stock(i, qty)`, `close_stock_market()`; `stocks::trend`; `World::stocks`, `stock_history_bits`, `stock_history_cursor`, `commercials`, `month_counter` | The chart and pie can be drawn from existing World data. |
| Red / black card stock target | card-target flow | `use_card` / `card_targets` | Check that `CardTarget` covers stock indices. |
| Monthly dividend | `tools/screens/monthly.cpp` | `EffectKind::dividend` + `EffectEvent::monthly` | Rule ported in `pay_dividends` (was missing). |
| Player info | `Screen::wealth` paper list | `World.players[].state/status`, `stocks::player_wealth`, `lands`, `facilities`, `commercials` | |
| Save / load | fixed `Save1.DAT` / `Autosave.DAT` | `SaveFile`, `SaveHeader` (date, map_id, stage_id, player_count), `PlayerState.character` | Enumerate SAVE0–5. |
| Lottery purchase | `DecisionKind::lottery`, options "1".."36" plus 离开 | `respond(option)`; `World::lottery_tickets`, `public_fund` | Becomes a 9×4 grid on Panel 12:0. |
| Lottery draw | `tools/screens/monthly.cpp` | `EffectKind::lottery_draw` + `EffectEvent::monthly` (tickets before the draw, pool, number, winner) | |
| Auction | `DecisionKind::auction`, legal options only | `respond(option)` mapped to `AuctionAction` 0–6 (same order as the original buttons) | Show all 7 buttons. |
| Trading | `Screen::trading` | `open_trading`, `post_offer`, `cancel_offer`, `buy_offer`, `close_trading`; `trading::quotes`, `listable_items`, `listing_limits` (`maximum_price` = 10× market, matching the original) | |
| Options | `Screen::settings` (speed ×1/2/4, music, sfx, fullscreen) | — | Mostly app-level config. |
| AI settings | none | — | Needs a mutator (see the list below). |

### Data and features the native core does NOT expose yet

**New game**
1. 遊戲時間 day limit (0x49911c) and 勝利條件 target wealth (0x499108) have no fields in
   `NewGameOptions` and no end rule. `winning_player()` only reports the last solvent player.
2. There is no eliminated-character roster (0x4990f4), campaign continue flag, cleared-maps state, or
   stage-clear flow.
3. The starting items `fcn_00445a4d(p, {1,2,3,4,8,9})` need checking against native setup
   (**UNVERIFIED**).
4. Original auto-fill consumes the gameplay `rand()` during the UI. Native picks before seeding, so
   random streams differ (document it, don't emulate it).

**Bank**
5. `GameDecision` lacks:
   - a phase flag (passing = ATM only, stopped = ATM then desk);
   - a director flag;
   - per-operation limits (`max_amount` is 0 for bank). Needed: the loan ceiling (wealth snapshot
     taken **at entry** minus loan), the financing ceiling (others' deposits minus special), and the
     others' deposit total.
6. Failure reasons. `respond` only returns false, but the UI needs: over limit (#0081/#0088), not
   enough cash (#0083/#0090), suspended or refused with days remaining.
7. There is no helper for loan due date → displayable date, days until due, or the holiday picture
   for the calendar.

**Stocks**
8. There is no `GameSession` query for "market closed today", which the 本日休市 screen needs.
9. Commercial building level (byte +0x19) is not modelled; it is used for company pictures 15 and 24.
10. ~~There is no monthly dividend event carrying the per-player and per-company table.~~ Done: `EffectKind::dividend` (§14).
11. Price-limit refusal reasons (漲停 / 跌停) are not reported; native only returns false. Note also
    that the original UI checks only `flags_10` and bank balance, not `shares_available`.

**Lottery and auction**
12. (Draw show: done, `EffectKind::lottery_draw`, §14.2.) Lottery draw: no pending "draw show" event with {number, winner, payout, sold_numbers_only},
    tickets per player, and the pool before payout. There is also no `cfg+1` dialogue flag.
13. Auction: the bidder table is private (slots, status codes 1–8, seller flag, cash, current and last
    bidder, initial price). There are no per-action outcome events and no property picture index.

**Trading**
14. Not exposed:
    - original Traditional-Chinese failure reasons (native messages are English or Simplified);
    - the card hand slot order (native merges cards by ID);
    - property-picker categories, toll, lease date and development label;
    - a board-full pre-check before opening the menu.

**Info view, save/load, AI settings, options**
15. Info view: no helpers for the 企業 / land / chain / house / facility counts, god remaining days, or
    original display names.
16. Save/load: no SAVE0–5 enumeration with the 0x26 validity check. Whether the AUTO slot maps to
    SAVE0.DAT is **UNVERIFIED**.
17. AI settings: no `GameSession` mutator for `controller_flags & 4` (託管), `card_strategy`,
    personality (+23), `init_cash_ratio` or `stock_investment_ratio`. `world()` is const. It is also
    unverified whether the native turn loop auto-plays a human player with 託管 set.
18. Options: no surrender API (認輸投降), no restart-in-place, no persisted 16-byte config, no
    autosave toggle, no 3-level speed, no volume 0–4, no song picker, no window set (needs the small
    map), no date dialog (native always uses the local clock), no hotkeys.

**Assets and decoders**
19. The native graphics layer has no FLC (0xAF12) decoder. It is needed for Panel 14, 16 and 17
    (lottery), and the same format appears in Panel 4, 5 and 6.
20. Not yet loaded: jump.mkf, and Panel 2, 9, 12–17, 21–24, 26–29+, 73–77.
21. Speaking voice clips referenced through `#NNNN` (bank 75–91, lottery 11–36, auction 131–148) need a
    `#NNNN` → Speaking.mkf mapping in the native text/voice layer.
22. All cosmetic `rand()` uses (blinks, mouths, glances) must use a separate UI RNG in native.

---

## 13. Open questions (UNVERIFIED summary)

- Exact brightness-table rounding for `fcn_004552e7` / `fcn_004552b7` levels −10, −12, −16 and −20.
- Font flag bit 3 (value 8) compose semantics.
- Which shade of the number-pad gauge reads as "filled".
- Whether Speaking.mkf entry = NNNN directly.
- The new-game music track; the meaning of starting items 1, 2, 3, 4, 8, 9; the campaign-continue
  flow.
- Calendar holiday picture entries; the account-card and AI-tab portrait archive (table `0x498eb0`).
- Info view: lower-left counters, property column mapping, frame 9:11 usage, whether zero-holding
  stock rows are shown.
- Trading: `fcn_00423b3b` filter and sort; the sex-to-colour mapping; property-picker hotkeys.
- Auction: Panel 26:20 / 26:24 usage; facility picture tables.
- Stock chart band edges (±2 px).
- Which bank call path (L2549 vs L2709) is passing versus stopping.

---

## 14. Monthly cycle (month-end report, lottery draw timeline)

`fcn_0041cf67` (rich4_player_core_actions.asm:4767) runs once per day. `edi` = `fcn_00452117`'s
"month changed" result. On day 15 it calls `fcn_0042ba97` (dividend table, §4) and then
`_rich4_ui_letou_kaijiang_entry` (§7.2, §14.2); on a month change it calls `fcn_00439bfa`
(the report below), then re-places objects 13/14. Native: `GameSession` emits
`EffectKind::dividend` / `lottery_draw` / `month_report` with `EffectEvent::monthly`
(`MonthlyFigures`), drawn by `tools/screens/monthly.cpp`.

### 14.1 Month-end bank report (`fcn_00439bfa` rich4.asm:18988, window proc `fcn_00437e61` :16815)

**Setup.** Sounds preset `0x475b17` = Effect 27, 28, 60. Panel 25 is loaded and music 9 starts
(`fcn_004549cf(9)`; restored by `fcn_00454bcc` on close). Listed players = slots with `+0x15 ≠ 0`, in
slot order (n = 2..4). **There is no ranking/sort**; frames 25:5–10 (MONEY board, digits 1–4, coin)
are unused here.

Initial picture: 25:0 opaque at (0,0); lady 25:19 at (24,70); per listed player i the figure
25:(3c+47) (centred origin) at (600, Y[n][i]), c = character (`+0x13`).

| table | n=2 | n=3 | n=4 |
|---|---|---|---|
| Y `0x475918` (column y) | 120, 360 | 100, 240, 380 | 60, 180, 300, 420 |
| X `0x475930` (lineup x) | 407, 490 | 324, 407, 490 | 324, 407, 490, 573 |
| spotlight x `0x475948` | 334, 414 | 259, 334, 414 | 259, 334, 414, 494 |
| spotlight frame `0x475960` | 16, 17 | 15, 16, 17 | 15, 16, 17, 18 |

Cards: frame 25:(11+i), drawn at (360, Y−36); text pre-rendered into the card with font
(18, 0x101010, 0, bold): "存款：" (4,6) align 0, deposit `+0x20` at (154,6) align 1, "利息：" (4,46),
then red 0xff0000 "貸款中" at (154,46) align 1 when the loan `+0x24 ≠ 0`, else trunc(deposit × 0.1)
(`ref_00464ea0`). The card shows the deposit **before** interest.

Bubbles (generic `fcn_0044ec30` / `fcn_0044ecb6` / `fcn_0044ee18`, rich4.asm:23272–23425): frame at
(x,y) − origin, text font (20, 0x101010, 0, bold) align 4 at the frame centre + (dx,dy), `#NNNN`
plays Speaking NNNN. A bubble stays ≥ 2000 ms and until its voice ends; state handlers only run on
timer ticks with no bubble up. Default config: 25:1 at (190,10), (0,−30).

**Timer** 100 ms. A left/right button-up (any state ≠ 0) removes the bubble at once, skips a playing
reaction FLC (flags bit 1) and sets `c42e`, which also skips the next wait in states 5, 0xF, 0x16.

| state | action |
|---|---|
| open (0x405) | state 1; `cfg+1`: bubble `#0092各位客戶辛苦了！\n又到了每月銀行\n結算的日子。` |
| 1 | lady 25:24 at (28,70); bubble `#0093大富翁銀行將\n根據您的存款，\n加發１０％的儲金利息。` → 2 |
| 2 | figures become 25:(3c+49); cards appear. **Interest is credited here**: loan-free players get deposit = trunc(deposit × 1.1) (`0x464e88`). Pick the 悲情人物 (`fcn_00437d1a`) and the champion (`fcn_00437dfe`). → 5, or → 0xF if there is no 悲情人物 or it is the champion (`cfg+1` off: → 0x16 with 30 ticks) |
| 5 | count; tick 20: lineup (25:0, lady 19, figures 3c+47 at X[n][i], bottom on y=330); tick 30: lady 25:32 at (35,67), bubble `#0095本月悲情人物是．．。`, Effect 27 → 6 |
| 6 | spotlight on the 悲情人物 (opaque, at (x,0)), lady 25:37 at (0,89), lineup; bubble `#0096+c` name; Effect 60 → 7 |
| 7 | lady 32, award panel 25:2 at (440,405) (origin 139,49): font (16, 0x101010, bold) labels x=320 align 0, values x=560 align 1, rows y 370/388/406/424: 獲獎原因： / 本月意外損失： `+0x5c` / 本月意外之財： `+0x60` / 本月倒楣天數： "%d天" of `+0x42`; lineup without that player → 8 |
| 8 | Data **417+2c** FLC at (X+dx417, 330+dy417), plays = flags bits 8..15; then lady 25:38 at (17,65) → 9 |
| 9 | bubble 25:3 at (190,10), (0,+20): `#0108別灰心，再加油喔！`; count = 19 → 0xF |
| 0xF | count; tick 20: lineup; tick 30: lady 38, bubble (25:1) `#0109本月冠軍是．．。`, Effect 27 → 0x10 |
| 0x10 | spotlight on the champion, lady 25:42 at (27,64), lineup; bubble `#0110+c` name; Effect 28 → 0x11 |
| 0x11 | 25:0 + spotlight + award panel (no lady): 獲獎原因： / 現金： `+0x1c` / 存款： (after interest) / 總資產： `_rich4_calculate_player_wealth`; lineup without the champion → 0x12 |
| 0x12 | Data **416+2c** FLC at (X+dx416, 330+dy416); then lady 25:45 at (6,60) → 0x13 |
| 0x13 | bubble 25:4 at (213,37), (0,+20): `#0122其他人還要\n更努力喔！`; count = 10 → 0x16 |
| 0x16 | count down; at 0 (1 s) or on a click the screen closes by itself |

On close, every listed player's `+0x42` (倒楣天數), `+0x5c` and `+0x60` are zeroed.

**Champion** `fcn_00437dfe`: the listed player with the strictly largest wealth (start 0, ties keep
the first; the first listed player if nobody is above 0). **悲情人物** `fcn_00437d1a`: score =
(`+0x5c` − `+0x60`) + `+0x42` × price index × 2500 + (word `+0x44`) × 10; M = the top score
(start 0), S = the top among scores ≠ M; a winner only when M ≠ 0, S ≠ 0 and (M − S)/M > 0.4
(`0x464d58`), else none.

**Reaction table** `0x4759f7` {dx416, dy416, flags416, dx417, dy417, flags417} by character:
0 (−50,−94,0x603,−77,−148,0x003), 1 (−48,−97,0x403,−48,−90,0x603), 2 (−38,−97,0x603,−50,−87,0x603),
3 (−31,−61,0x603,−64,−109,0x303), 4 (−41,−87,0x403,−42,−77,0x603), 5 (−44,−76,0x403,−31,−60,0x203),
6 (−36,−100,0x603,−36,−103,0x603), 7 (−50,−89,0x403,−50,−87,0x003), 8 (−51,−90,0x403,−51,−92,0x003),
9 (−31,−60,0x403,−51,−95,0x303), 10 (−103,−188,0x403,−39,−87,0x603), 11 (−40,−75,0x403,−40,−75,0x403).
Flags: bit 0 overlay, bit 1 click-skippable, bits 8..15 play count (**UNVERIFIED** reading).

**Lady animation** (every tick, rich4.asm:18259–18943; libc `rand()` → cosmetic RNG in native):
blink when `rand()>>10 == 0` through `0x4759e8` — mode 0 (lady 19) eyes 21,20,21 at (76,120);
mode 1 (24) 28,27,28 at (103,120), restoring with 25 (glance 25/26 when `rand()>>10 == 1`); mode 2
(32) 34,33,34 at (87,118); mode 4 (38) 39,40,39 at (83,123). Mouth only while a bubble is up:
1-in-4 per tick opens for (rand&7)+1 ticks — mode 0 22/23 at (76,160), mode 1 30/29 at (103,155),
mode 2 35/36 at (87,161), mode 5 44/43 at (90,134). Modes 3, 4, 6 have no mouth.

**Native differences.** The core credits the interest at the month change (`close_month`) and the
report shows the figures captured around it (`MonthlyFigures::deposit/interest/balance/total`). The
`cfg+1` dialogue option is not modelled (always on). Music switch: TODO(audio) hook in `monthly.cpp`.

### 14.2 Lottery draw timeline (`fcn_0043010c`, rich4_ui_letou.asm:1304)

Timer 50 ms. The window proc handles only 0x401/0x405/0x113/0xF: **no mouse or key input** (native:
a tap ends the current bubble early). Bubble config default 15:22 at (300,47), (0,−10).

| state | trigger | action |
|---|---|---|
| open | | 15:0, hosts 15:1 at (472,66) and 15:3 at (7,66), board, tickets; `cfg+1`: bubble #0017 → 1 |
| 1 | bubble gone | bubble `#0018現在馬上為您\n開出這一期的\n號碼．．。` → 2 |
| 2 | bubble gone | host 15:2 at (418,66); Effect 57; cage FLC Panel 16 at (183,75) prepared → 3, cnt 1 |
| 3 | each tick | cnt 20: the host's arm is restored (bg (418,171,80×80), 15:1 src (0,51,38×90) at (472,117)); from cnt 20 one cage frame per tick; at the end the result: balls 15:(37+d) at (286,405)/(358,405), green Data 517:(8+d) at (300,220)/(340,220) |
| → 4 | sold | bubble `#0019本月份的得主\n是．．．。` |
| 4 → 5 | bubble gone | winner layout: hosts erased, 15:6 at (0,0), 15:5 at (505,66), plate 15:24 at (320,200), name font (28, red, 0x400000, 3, 4) at (320,180) with voice `0x475630[c]` = #0020+c; board at (91,19)/(91,56); Effect 58 |
| 5 | each tick | static to cnt 30, then one Panel 17 frame per tick at (205,0); at the end restore (150,0,330×360) (movie, plate, name, green digits go) and bubble `#0032恭喜您獨得\n所有獎金！` → 6 |
| 6 | bubble gone | hosts 15:1/15:3 and the board return → 8 |
| → 7 | unsold | 500 ms wait; right host replaced by 15:4 at (489,116), left face 15:21 at (52,89); bubble config 15:23 at (320,200), (0,0); `#0033SORRY！\n本月份沒有人\n得獎∼` |
| 7 | bubble gone | `#0034獎金將累積\n到下個月．\n．．．。` → 8 |
| 8 | | hosts back, config back to 15:22; `#0035希望下次\n得獎者就\n是您！` → 9 |
| 9 | bubble gone | `#0036行動要快喔！` → 10 |
| 10 | bubble gone | close; with a winner the pool is paid and the tickets cleared |

Host faces (states 1–3, 9–10 only): `rand()>>9` 0 → right blink 15:8,7,8,10 at (512,102); 1 → left
blink 15:16,17,16,15 at (52,89); 2–3 → right eyes 15:(9+rand&1); 4–5 → left eyes 15:(14+rand&1).
Right mouth at (512,119) while a bubble is up: 1-in-4 per tick 15:12/13 for 1–15 ticks, then 15:11.

Ticket panels (`fcn_0042f417`, LT:206): listed players only, origins column-major (16,340),
(16,410), (328,340), (328,410); area 296×60 darkened −16; head 15:(25+c) at (px+20, py+30);
numbers "%02d" in Panel 13 digits at x = px+54+40i (units +16), y+30 (≤ 6) or y+15/y+45 (7–12).
