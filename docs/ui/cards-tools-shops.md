# Original UI spec: cards, tools, shops, magic house, hospital and prison

Scope: the use-card screen, the use-tool screen, picking targets on the map, the card art popup, the
steal (搶奪) picker, the card/tool shop (點數商店), the magic house (魔法屋), the hospital/prison
bail-out screens, and the item description texts. The goal is a faithful rebuild in the native SDL3
port.

Sources:
- The pinned disassembly in `.local/upstream/asm`. File and line references below are relative to it.
- MKF entries decoded with the native `assets::Archive`.
- For card art (Data.mkf 584) the output was checked against the upstream C decompressor.

Reference images are under `.local/ui-ref/cards-tools-shops/`. They are local only because they contain
commercial art.

| File | Content |
| --- | --- |
| `data-571-600-cards.png` | All 30 card pictures (Data.mkf 571–600, raw 165x256), in card-id order, 10 per row |
| `panel-10-12.png`, `shop/panel-10-all.png` | Panel 10 (shop, all 38 frames), Panel 11 (card/tool grids and tool icons), Panel 12 |
| `panel-11.png` | Panel 11 only: 11:0 card grid, 11:1 tool grid, 11:2–14 tool icons, 11:15–16 dismount icons |
| `data-0-cursors.png` | Data.mkf 0: all 43 mouse-cursor frames |
| `data-517-bubbles.png` | Data.mkf 517: message box (frame 5) and speech cloud (frame 6) |
| `magic/panel-18.png`, `magic/panel-19-hitmask.png`, `magic/data-2.png` | Magic house art, its hit mask shown in false colour, and the player portraits |
| `hospital-prison/panel-63-65.png` | Prison (63), large NPC art (64), hospital (65) |
| `help-texts-utf8.txt` | Every help.mkf text entry, decoded to UTF-8 |
| `src/rawsheet.cpp`, `src/dumpentry.cpp`, `src/upstream_decompress_driver.c` | Throw-away research helpers (not part of the build) |

## 0. Conventions

Notation and coordinates:
- `Panel 11:2` means archive `Panel.mkf`, entry 11 (0-based `read_mkf` index), frame 2. In code, frame
  N of an SPR/SMP buffer is at `buf + 0xc + 12*N`.
- **Anchor rule.** Blits put the frame hotspot `(gx,gy)` at the call point, so the top-left is
  `point − (gx,gy)` (`rich4_drawing.c:68`). This doc lists the call point, the origin, and the
  resulting top-left where it matters.
  - `fcn_004563f5` is an opaque blit.
  - `fcn_00456418` is a colour-key blit (pixel 0 is transparent).
  - `fcn_0045643d` blits a sub-rectangle.
  - `fcn_00456280` / `fcn_004562a5` blit into another graph, not onto the screen.
- All coordinates are on the 640x480 screen. The board viewport is x 0–439, y 40–479.

Shared helpers:
- **Pressed effect.** `fcn_00451b9e(rect{l,t,r,b})` (`rich4_graph_struct.asm`) shifts the rect contents
  1 px down-right and darkens the new top row and left column by brightness level −16.
  `fcn_00451d4e` undoes it. There is no separate hover art in the card and tool screens.
- **Fonts.** `_rich4_create_font(height, fg, second, flags, extra)` (`rich4_text_drawing.asm:242`):
  - Face 細明體 (MingLiU), Big5 charset (`rich4.asm:29285`).
  - flags bit0 = shadow (second colour at +1,+1), bit1 = bold (weight 700), bit2 = 4-way outline,
    bit3 = clip to the draw area.
  - Character spacing is `extra − 1`.
- **`_rich4_draw_text(dst, text, x, y, mode)` anchor modes** (jump table `ref_0044faa0`):

  | Mode | Anchor |
  |---|---|
  | 0 | top-left |
  | 1 | top-right |
  | 2 | centre |
  | 3 | centre, vertical text |
  | 4 | centre, multi-line, lines centred |
  | 5 | left, vertically centred |
  | 6 | right, vertically centred |
  | 7 | bottom-centre, multi-line |

  A text starting with `#NNNN` plays voice NNNN, and the prefix is not drawn
  (`rich4_text_drawing.asm`, `_rich4_draw_text` start).
- **Sounds.** Records at `0x48231a…` hold the Effect.mkf entry in their first word. See
  `docs/ui/finance-menus.md` "UI sound table".

  | Record | Effect.mkf | Use |
  |---|---|---|
  | `ref_00482322` | 1 | click/select |
  | `ref_0048232a` | 2 | target confirmed |
  | `ref_00482332` | 4 | right-click cancel |
  | `ref_0048233a` | 3 | refused / invalid |
  | `ref_00482402` | 0x3e = 62 | card-use popup |

- **Modal loop.** `Wait_0402_Message(proc, lParam)` runs a window proc until `Post_0402_Message(v)`
  is called, then returns v.
  - Every screen here handles 0x401 (init), WM_PAINT, 0x201/0x202 (left button down/up) and 0x205
    (right button up = cancel, returns 0).
  - `fcn_00402460(1/0)` shows or hides the software cursor.

## 1. Use-card screen (`_rich4_ui_use_card_entry`, rich4_ui_use_card.asm:900)

This screen opens from the top-toolbar card button (`rich4_ui_clicking_top_panel.asm:92`) and from
`rich4.asm:11640`. Human players only (`state+21 == 1`). AI players take the branch in §1.6.

### 1.1 Artwork and layout
- **Grid art.** Panel 11:0, 412x180, origin (0,0). It is a green marble 5x3 grid of 80x56 tiles,
  with the inner tile area starting about 5 px in.
- **Placement.** The grid is drawn opaque with `fcn_004563f5(…, 14, 130)` (l.946–952), so it
  occupies screen (14,130)–(426,310).
- **Saved background.** Before drawing, the rect (0,40)–(440,480) is saved (`fcn_00451e7e`, l.917–925).
  It is restored on exit (`fcn_00451edb(…, 0, 0x8028)`, l.967–971). Only the board viewport is
  covered; the side panel and toolbar stay visible.
- **No card pictures, descriptions or buttons appear on this screen.** Each tile shows only the card
  name.
- **Names** (`fcn_00441b0a`, l.874–897):
  - Drawn into a copy of the grid image with `create_font(20, 0xFFFFFF, 0x101010, flags 3 = shadow+bold)`.
  - Mode 2 (centred) at image (45 + 80·col, 33 + 56·row), i.e. screen (59 + 80·col, 163 + 56·row).
- **Hand order.** The hand is `_rich4_player_cards[player*15 + slot]` (15 bytes). Cards are drawn in
  slot order, skipping empty slots. The tile index used for hit-testing is the slot index directly
  (l.449–475), so the original relies on a packed hand.
  - The hand is never sorted or merged: two copies of 停留卡 are two tiles.
  - There is no count badge on card tiles.

### 1.2 Interaction (window proc `fcn_004416f0`, l.402–531)

| Message | Behaviour |
|---|---|
| 0x401 | Clear the selection, show the cursor, invalidate. |
| Left down 0x201 | Hit area x 19–418, y 135–302 (l.429–436). `slot = ((y−135)/56)*5 + (x−19)/80`. Empty slot: nothing. Otherwise press the tile with `fcn_00451b9e{20+80c, 136+56r, 98+80c, 190+56r}` (l.452–472), remember the card id and play the click (Effect 1, l.483–487). |
| Left up 0x202 | If a card is remembered, un-press, hide the cursor and post the card id (l.490–501). The release point is **not** re-tested, so pressing a tile and releasing elsewhere still uses it. |
| Right up 0x205 | Post 0 (cancel) (l.503–508). |

There is no keyboard path and no hover feedback.

### 1.3 After selection (l.973–1002)
1. Format `"使用%s"` (`ref_00465305` = Big5 A8CF A5CE "使用" + "%s") with `card_table[id].name`.
2. Show the **card art popup** `fcn_00441f73(id, text)` (§1.4). It appears even if the player then
   cancels target picking.
3. Call `_rich4_card_functions[id-1]` (table l.986–1016). Ids 18–21 (復仇, 嫁禍, 免費, 免罪) map to
   `fcn_004420d5`, which returns 0. They are passive cards and cannot be played, but they still get a
   tile and the popup.
4. If the function returns 0 (not used, or target cancelled), play refused (Effect 3) and **reopen the
   grid** (loop to `loc_00441c22`). Otherwise free the buffers and return.

### 1.4 Card art popup (`fcn_00441f73`, rich4.asm, label at `0x441f73`)

This popup also appears when an AI uses a card (§1.6) and for passive cards (`rich4_card_passive.asm:425`).

- **Card picture.** `read_mkf(Data, 0x23a + id)`, i.e. **Data.mkf entry 570 + card id**.
  - This is a raw 165x256 RGB555 bitmap, not SPR/SMP. The header `{w=165, h=256, x=0, y=0}` is
    built from `ref_00441204`.
  - Drawn opaque at (138,200), so it occupies screen (138,200)–(303,456).
- **Message box.** Frame `Data 517:5` (249x170, origin (123,101)), colour-keyed at anchor (220,129),
  top-left (97,28).
  - Text `"使用<name>"`: `create_font(16, 0xF0F0F0, 0x101010, flags 1 = shadow)`, mode 4, centred at
    (220,129).
  - The saved rect is (0,40)–(440,480), but the box top (y 28–39) overlaps the toolbar. UNVERIFIED
    whether those rows are transparent or leave a trace.
- **Sequence.** Play Effect 62, then wait 1500 ms (`fcn_004528b9(0x5dc)`, skippable with a mouse or
  key release; see messages.md §2.1), then restore.

### 1.5 Full card table (card_table, rich4_card_table.c; art = Data.mkf 570+id)

Column notes:
- Price is in points.
- Help entry is the `help.mkf` text entry (§5).
- `select` is the argument to `_rich4_select_instance_with_mouse` (§3). "—" means the card needs no
  map pick.
- Every card pick uses cursor frames 0:12–26 (spinning card, 15 frames).

| id | Name | Price | Card art | Help entry | Map pick `select` (mask / rule) | Source |
|---|---|---|---|---|---|---|
| 1 | 均富卡 | 200 | Data 571 | 63 | — (all players) | rich4_card_junfuka.c |
| 2 | 均貧卡 | 200 | Data 572 | 62 | 0x0E0C0410: players / rule 4 (another active player) | junpinka.c:19 |
| 3 | 購地卡 | 35 | Data 573 | 85 | — (current tile) | |
| 4 | 換地卡 | 25 | Data 574 | 77 | 0x0E0C0202 land / rule 2, or 0x0E0C0204 facility / rule 2 | huandika.asm:50, 256 |
| 5 | 換屋卡 | 20 | Data 575 | 78 | 0x0E0C0202 / 0x0E0C0204 | huanwuka.asm:51, 241 |
| 6 | 轉向卡 | 20 | Data 576 | 86 | 0x0E0C0010 players / any | zhuanxiangka.c:18 |
| 7 | 改建卡 | 15 | Data 577 | 64 | — | |
| 8 | 拍賣卡 | 20 | Data 578 | 66 | — | |
| 9 | 天使卡 | 160 | Data 579 | 57 | 0x0E0C0006 land+facility / any | tianshika.asm:35 |
| 10 | 惡魔卡 | 180 | Data 580 | 76 | 0x0E0C0006 | emoka.asm:33 |
| 11 | 怪獸卡 | 60 | Data 581 | 65 | 0x0E0C0506 / rule 5 (another owner, level > 0) | guaishouka.asm:34 |
| 12 | 拆除卡 | 15 | Data 582 | 67 | 0x0E0C0626 / rule 6 (rule 5 + map objects of type 0x10–0x12) | chaichuka.asm:39 |
| 13 | 搶奪卡 | 25 | Data 583 | 81 | 0x0E0C0410, then the steal picker (§4) | qiangduoka.asm:26, 87 |
| 14 | 停留卡 | 20 | Data 584 **(see note)** | 73 | 0x0E0C0010 | tingliuka.asm:25 |
| 15 | 冬眠卡 | 100 | Data 585 | 58 **(wrong text in original)** | — (all others) | |
| 16 | 夢遊卡 | 25 | Data 586 | 82 | 0x0E0C0710 / rule 7 (player not self) | mengyouka.asm:34 |
| 17 | 陷害卡 | 20 | Data 587 | 74 | 0x0E0C0710 | xianhaika.asm:31 |
| 18 | 復仇卡 | 20 | Data 588 | 75 | passive | |
| 19 | 嫁禍卡 | 40 | Data 589 | 80 | passive | |
| 20 | 免費卡 | 25 | Data 590 | 60 | passive | |
| 21 | 免罪卡 | 25 | Data 591 | 61 | passive | |
| 22 | 送神符 | 10 | Data 592 | 72 | — | |
| 23 | 請神符 | 20 | Data 593 | 84 | — | |
| 24 | 紅卡 | 50 | Data 594 | 70 | — (stock choice, UNVERIFIED UI) | |
| 25 | 黑卡 | 30 | Data 595 | 79 | — (stock choice, UNVERIFIED UI) | |
| 26 | 查稅卡 | 35 | Data 596 | 69 | 0x0E0C0410 | chashuika.asm:34 |
| 27 | 漲價卡 | 35 | Data 597 | 83 | 0x0E0C0006 | zhangjiaka.asm:27 |
| 28 | 查封卡 | 35 | Data 598 | 68 | 0x0E0C0006 | chafengka.asm:27 |
| 29 | 同盟卡 | 40 | Data 599 | 59 | 0x0E0C0410 | tongmengka.asm:26 |
| 30 | 烏龜卡 | 70 | Data 600 | 71 | 0x0E0C0010 | wuguika.asm:25 |

Notes on the table:
- **Card 14 art.** Data.mkf 584 decodes with its lower half (rows ~115–255) as noise. The native
  `Archive::decode` output is byte-identical to the upstream C `mkf_decompress`.
  - Either this copy of `Data.mkf` is damaged, or both decompressors share a bug.
  - **UNVERIFIED**: check 停留卡 in the original EXE under Wine before shipping. If the original
    shows it intact, fix the decompressor.
- **Help entry 58 (冬眠卡).** The text is a byte copy of 紅卡's text (entry 70) in the shipped
  help.mkf. Write a correct description for the native port, or keep the original bug deliberately.
- 紅卡 and 黑卡 are used from the stock screen. That flow is outside this doc (UNVERIFIED).

### 1.6 AI path (l.1041–1131)
This runs only when `state+21 & 6` and the profile has active-card AI (`state+22 & 1`).

1. Take up to 8 hand cards, starting at a random slot when the hand has more than 8.
2. For each, call the AI predicate `fcn_0041e69e(card)`. The first card that returns 1 shows the same
   card art popup (§1.4) and runs the card function.

There is no grid for AI players.

## 2. Use-tool screen (`_rich4_ui_use_tool_entry`, rich4_ui_use_tool.asm:1466)

### 2.1 Artwork and layout
- **Grid art.** Panel 11:1, 412x180, a brown 5x3 grid. Drawn opaque at (14,130) (l.1541–1552), the
  same rect as the card grid. The background rect (0,40)–(440,480) is saved with `fcn_00451a97`
  (l.1520–1531) and restored after every pick (l.1562–1603).
- **Tool icons** (`fcn_00447c6e`, l.1352–1464):
  - Tools are listed in **id order 1..13**. Only tools with count > 0 are shown, packed into the
    15 tiles.
  - `ref_0048c548[tile]` holds the tool id.
  - The icon is **Panel 11:(id+1)** (frames 2–14, centred origins). It is blitted into the grid image
    at (29 + 80·col, 33 + 56·row), so the icon centre is on screen at (43 + 80c, 163 + 56r).
- **Count.** `"×%d"` (`ref_004653e0` = Big5 A1D1 "×" + "%d"), `create_font(20, 0xFFFFFF, 0x101010, 3)`.
  Drawn mode 1 (top-right) at image (79 + 80c, 23 + 56r), i.e. screen top-right (93 + 80c, 153 + 56r)
  (l.1405–1431).
- **Dismount tile (下車).**
  - Shown when `state+17` (vehicle in use) is 1 (motorbike) or 2 (car).
  - Draws **Panel 11:15** (crossed-out motorbike) or **11:16** (crossed-out car), 80x56, into the grid
    at image (325,117), i.e. screen (339,247) (l.1484–1505).
  - This is always tile 14 (bottom-right), with `ref_0048c556 = 14`.
  - Tool function 14 is `fcn_00447c00` (l.1312). It returns the vehicle to the inventory (+1 機車 or
    汽車), sets walking (`state+17 = 0`, `state+18 = 1`), refreshes the sprite and returns 1.
- There are no descriptions, prices or buttons on this screen.

### 2.2 Interaction (window proc `fcn_00445c14`, l.101–286)
Input is identical to §1.2:
- Hit area x 19–418, y 135–302.
- The pressed rect is the same.
- Click sound plays on press, the action happens on release, and right-click cancels.

The returned id is dispatched through `_rich4_tool_functions[id-1]` (l.1600–1605).
- If the tool function returns 0 (target cancelled), the grid is redrawn and the loop continues
  (`loc_00447e7d`).
- There is **no "使用XX" popup and no art for tools** when a human uses them.

### 2.3 Tool table (tool_table, rich4_tool_table.c; icon = Panel 11:(id+1))

| id | Name | Price | Icon (size, origin) | Help entry | Map pick `select` | Cursor frames |
|---|---|---|---|---|---|---|
| 1 | 機器娃娃 | 15 | 11:2 (40x34, 20,17) | 99 | — | |
| 2 | 路障 | 30 | 11:3 (38x25, 19,12) | 95 | 0x00000001: road node | 0:0 STOP |
| 3 | 地雷 | 25 | 11:4 (39x39, 20,20) | 88 | 0x00010001: road node | 0:1 mine |
| 4 | 定時炸彈 | 25 | 11:5 (22x38, 11,19) | 90 | 0x00020001: road node | 0:2 bomb |
| 5 | 機車 | 80 | 11:6 (34x19, 17,11) | 97 | — | |
| 6 | 汽車 | 150 | 11:7 (39x20, 20,10) | 89 | — | |
| 7 | 飛彈 | 100 | 11:8 (14x32, 7,16) | 91 | 0x000300C0: everything + edge scroll | 0:3 |
| 8 | 遙控骰子 | 30 | 11:9 (38x38, 19,19) | 96 | — | |
| 9 | 機器工人 | 30 | 11:10 (32x40, 16,20) | 98 | 0x02090006: land+facility | 0:9–11 reticle |
| 10 | 時光機 | 40 | 11:11 (36x32, 18,16) | 92 | — | |
| 11 | 傳送機 | 95 | 11:12 (34x15, 17,7) | 94 | 0x01200036 source; then 0x02090802 / 0x02090804 / 0x02090001 destination | 0:32–33 paw, then 0:9–11 |
| 12 | 工程車 | 150 | 11:13 (34x19, 17,8) | 87 | — | |
| 13 | 核子飛彈 | 250 | 11:14 (14x36, 7,18) | 93 | 0x000400C0 | 0:4 |
| 14 | (下車) | — | 11:15 / 11:16 | — | — | |

Source lines:
- 路障 tool_luzhang.asm:35, 地雷 dilei.asm:35, 定時炸彈 dingshizhadan.asm:35
- 飛彈 feidan.asm:44, 機器工人 jiqigongren.asm:42
- 傳送機 chuansongji.asm:50, 95, 144, 190, 423
- 核子飛彈 hezifeidan.asm:44

Rules:
- Tools 1–8 are sold in the shop. Tools 9–13 are never sold (§6).
- The stack limit is 9.

### 2.4 AI path (l.1624–1737)
Applies when `state+21 & 6` and the profile allows tool AI (`state+22 & 2`).

1. Collect owned tools, excluding 時光機 (10).
2. Try up to 4 of them, starting at a random one if more than 4 are owned, with the predicate
   `fcn_00420e9a`.
3. The first hit shows the timed message box `"使用%s"` (`ref_004653e5`) for 1500 ms (messages.md
   §2.1), then runs the tool. This is a plain message box, with no art.

## 3. Picking a target on the map (`_rich4_select_instance_with_mouse(param)`, rich4_ui_use_tool.asm:1284; proc `_rich4_select_instance_callback` l.300–990)

**There is no highlight, outline or arrow drawn on the map.** All feedback comes from the **mouse
cursor**:
- an animated "valid target" cursor over valid instances;
- the red ✕ cursor (Data 0:5) elsewhere;
- direction arrows while edge-scrolling.

The player can hover anything in the viewport.

### 3.1 Parameter encoding (l.322–340)
`param = (frames−1)<<24 | cursor<<16 | rule<<8 | mask`

- **cursor**: first frame of the valid-target cursor in Data.mkf 0. It animates `frames` frames at
  speed 10 (`fcn_004021f8(cursor, frames, 10)`, rich4_mouse_utils.asm:331).
- **mask**, which instances are eligible:

  | Bit | Instances |
  |---|---|
  | 0x01 | road nodes, instance 1..1999 |
  | 0x02 | lands, 2001..3999 (`land_info` +0x34·i) |
  | 0x04 | facilities, 4001..5999 (`facility_info` +0x38·i) |
  | 0x08 | right-click cancel disabled |
  | 0x10 | players (instance 0x8000 \| player bits; the player must be active) |
  | 0x20 | map objects (0x8000 \| object<<8) |
  | 0x40 | becomes "everything" (mask := (mask & 0x80) \| 0x37) |
  | 0x80 | edge scrolling enabled |

- **rule** (jump table `ref_00445e2d`, l.330–630), 0 = no extra check:

  | Rule | Valid target |
  |---|---|
  | 1 | own land, or own facility with level > 0 |
  | 2 | a land (facility) different from the one under the current player; the player must stand on the same kind |
  | 4 | a player other than self (low nibble non-zero) |
  | 5 | another owner's land or facility with level > 0 |
  | 6 | rule 5, or a map object of type 0x10, 0x11 or 0x12 |
  | 7 | any player bit except self |
  | 8 | unowned land or facility at level 0 |

  Rule 3 is never valid.
- Hit-testing uses `_rich4_get_instance_from_position(x, y−40)`, a pick buffer built by
  `fcn_00409b18`.

### 3.2 Behaviour
- **Mouse move over the viewport** (x 0–439, y 40–479):
  - Valid instance: store it and switch to the valid cursor if not already shown (l.588–600).
  - Otherwise: cursor 5, the red ✕ (l.603–630).
  - Outside the viewport: cursor 5.
- **Edge scroll** (only when mask & 0x80, l.395–470):
  - Triggers: cursor at x = 0 (left, cursor 0:40), x ≥ 440 (right, 0:36), y ≤ 40 (up, 0:34), or
    y = 479 (down, 0:38). Cursor ids come from `ref_00475e0d + 4·dir`.
  - A 50 ms timer moves the camera by `speed` along a rotation-dependent unit vector
    (`ref_004751b0`, 16.16 fixed point; index `(dir*2 + rotation − 2) & 7`).
  - Speed starts at 8 and grows by 4 per tick until it passes 64.
  - The camera is clamped to 220..2084 on both axes.
  - Moving away from the edge stops the timer.
- **Left button up:**
  - With a valid instance: Effect 2, hide the cursor, set the default arrow 0:41, and return the
    instance id (l.640–660).
  - Otherwise: Effect 3 (refused), and the picker stays open.
- **Right button up:** unless mask & 0x08, Effect 4, cursor 0:41, return 0 (cancel) (l.662–680).

### 3.3 Cursor art (Data.mkf entry 0, SPR, 43 frames; see `data-0-cursors.png`)

| Frames | Use |
|---|---|
| 0:0 | STOP sign (路障) |
| 0:1 | mine |
| 0:2 | time bomb |
| 0:3 | missile |
| 0:4 | nuke |
| 0:5 | red ✕ (invalid) |
| 0:6–7 | crosshair |
| 0:8–11 | red reticle animation |
| 0:12–26 | spinning card (card targets) |
| 0:27 | hand |
| 0:28–31 | pen/syringe animation |
| 0:32–33 | paw (傳送機 source) |
| 0:34–41 | yellow arrows N, NE, E, SE, S, SW, W, NW. **0:41 doubles as the default pointer.** |
| 0:42 | blue ellipse |

Frame origins are the hotspots, e.g. 0:5 is 32x32 with origin (16,16).

## 4. Steal picker for 搶奪卡 and fortune (`fcn_0044192a(victim, receiver, allow_tools)`, rich4_ui_use_card.asm:533)

Called from `rich4_card_qiangduoka.asm:87` and `rich4_fortune.asm:651`.

**Human:**
- Reads Panel 11 and renders the victim's tool grid (`fcn_00447c6e`) and card grid (`fcn_00441b0a`),
  as in §1.1 and §2.1.
- Cards: grid 11:0 opaque at **(14,70)**.
- Tools: if `allow_tools == 1`, grid 11:1 at **(14,270)** (l.555–591).
- The background rect saved is (0,40)–(440,480).
- Window proc `fcn_004413ec` (l.104–400):
  - Card hit: x 19–418, y 75–242. The tile is pressed at {20+80c, 76+56r, 98+80c, 130+56r}.
  - Tool hit: x 19–418, y 275–442, only if tools are allowed. Pressed at
    {20+80c, 276+56r, 98+80c, 330+56r}. The result has bit 0x8000 set.
  - Release confirms; right-click returns 0.
  - **No click sound** on this screen (unlike §1.2).
- Result: a card is moved with `consume_card(victim)` + `receive_card(receiver)`. A tool is moved
  with `after_player_use_tool(victim)` + `receive_tool(receiver)` (l.611–633).

**AI** (l.616–640): picks with `get_ai_card_param_value(1)` and shows the message box
`"搶得%s的\n\n%s"` (`ref_004652f8`; victim name, then card name) for 1500 ms.

## 5. Item description texts

- **`rich4_card_strings.c` and `rich4_tool_strings.c` are not descriptions.** They are per-character
  speech lines, `[12 characters][3 variants][30 cards]` and `[12][26]`, each prefixed by a voice id
  `#NNNN`. They are spoken through the speech bubble (messages.md §1) when a card or tool is used.
- **The descriptions are in `help.mkf`** (100 entries).
  - Entry 0 is an SMP image. Entries 1–99 are uncompressed or compressed Big5 text.
  - Wrapped lines are NUL-separated, about 10 full-width characters each. A double NUL is a blank
    line.
  - Card and tool entries have the sections 功能 / 使用範圍 or 使用時機 / 注意 / 原價.
  - To load one: `Archive("help.mkf").decode(n)`, split on `\0`, then convert Big5 to UTF-8.
- **Index.** The help UI's category table `ref_004761c4` (`rich4_ui_help.asm:2066–2100`) lists
  `{name, item-name array, first entry, count}`.
  - Cards: first entry **57**, count 30, names `ref_00476108` in stroke order.
  - Tools: first entry **87**, count 13, names `ref_00476180`.
  - The id→entry maps are already resolved in the tables in §1.5 and §2.3.
- Other relevant help entries:

  | Entry | Topic |
  |---|---|
  | 4 | 點券 |
  | 10 | 卡片 command |
  | 18 | 道具 command |
  | 27 | shop |
  | 34 | prison |
  | 37 | hospital |
  | 38 | magic house |

- The descriptions appear **only** in the Help screen. The use-card, use-tool and shop screens never
  show them. Showing them as tooltips in the native port would be a native-only addition and should
  be marked as such.
- Full decoded texts are in `.local/ui-ref/cards-tools-shops/help-texts-utf8.txt`.

## 6. Card shop / tool shop (點數商店) (`_rich4_ui_shop_entry`, rich4_shop.asm:1898)

### 6.1 Entry
**Owner gift** (l.1905–1971):
- Applies if the shop building (0x1771–0x1F3F) is owned by the current player (`+0x18 == player+1`).
- With 50% chance the player gets a random tool, otherwise a random card.
- Message box `"歡迎董事長光臨\n\n送您%s！"` (`ref_00464378`), 1500 ms.
- Then a character reaction line by value (`fcn_0044f230`).

**Humans** then open the shop (§6.2 onwards); the AI branch is in §6.6.
- BGM 6 is pushed with `fcn_004549cf(6)` (l.2196) and restored on exit.
- The shop always opens in **card mode**. `mode` is in `ref_0048c310`: 0 = card shop, 1 = tool shop.

### 6.2 Artwork (Panel.mkf entry 10; tool shop uses frame + 16)

| Element | Card shop | Tool shop | Call point | Origin | Screen rect | Lines |
|---|---|---|---|---|---|---|
| Background | 10:0 | 10:16 | (0,0) opaque | 0,0 | full screen | 214–240 |
| Shopkeeper | 10:2 witch, 173x448 | 10:18 girl, 182x256 | (320,240) key | (−52,208) / (−21,229) | (372,32)–(545,480) / (341,11)–(523,267) | 241–266 |
| Speech balloon | 10:15, 270x219 (both) | same | (120,10) key | 0,0 | (120,10)–(390,229) | 271–282 |
| Item list | 10:1 "CARD", 222x462 | 10:17 "ITEM" | (x,10) opaque; slides to x=5 | 0 | (5,10)–(227,472) | 581–601 |
| Player's own grid | copy of 11:0 with names | copy of 11:1 with icons and counts | (x,293) key; slides to x=227 | 0 | (227,293)–(639,473) | 602–611 |
| Switch-shop button | 10:13 (hammer → tools); pressed 10:14 | 10:29 (CARD → cards); pressed 10:30 | (542,13) key | 0 | (542,13)–(627,98) | 565–569, 1140–1150 |
| EXIT button | 10:35; pressed 10:36 | same | (556,246) key | 0 | (556,246)–(636,286) | 570–578, 1190–1196 |
| Points box | 10:37, 90x40 | same | (230,246) key | 0 | (230,246)–(320,286) | 388–410 |
| Face expressions | 10:4, 5, 6 | 10:23, 24 | (405,60) / (417,50) opaque | 0 | 50x33 / 70x44 | 790–900 |
| Blink | 10:7, 8, 7, 5 (`ref_004755b8`) | 10:21, 22, 21, 23 (`ref_004755bc`) | same as faces | | | 694–789 |
| Mouth closed / open | 10:9 / 10:10 or 11 | 10:25 / 10:26 or 27 | (405,91) / (417,89) | | | 960–1088 |
| Waving arm on exit | 10:3, 93x133 | 10:19, 68x165 | (320,240) key | (−3,146) / (−21,212) | | 1730–1745 |

Frames 10:12, 10:28 and 10:31–34 are unused here.

### 6.3 Lists and text

**Fonts:**
- `create_font(20, 0xFFFFFF, 0x101010, 3)`.
- A bought row is redrawn with fg 0xA0A0A0, i.e. greyed (l.1373, 1559).

**Card list** (l.2034–2119):
- Rows: `rand()%10 + 6`, so 6–15 per visit.
- Each row is a stock-weighted random card drawn from a temporary copy of
  `_rich4_remain_card_amount`. Duplicates are possible.
- Name: mode 2 at screen (95, 93 + 24i).
- Price: `"$%d"` (`ref_00464374`) of `card_table.price`, mode 1 with its right edge at screen
  (199, 85 + 24i). It is shown with "$" but is **points**.

**Tool list** (l.2121–2177):
- Tools 1–8 whose global stock is > 0, in id order. Stock counts are not shown.
- Name: mode 2 at (95, 102 + 48i).
- Price: mode 1 at right edge (199, 94 + 48i).

**Own grid** (bottom right): the same rendering as §1.1 (card names, mode 2) and §2.1 (tool icons
plus "×n" counts, mode 1), using image-relative offsets on top of the grid's on-screen origin (227,293).

**Points:** `itoa(points)` (`state+48`), mode 1, at (310,257) inside 10:37. It is redrawn on message
0x40E.

**Speech balloon** (`fcn_0044ec30(10:15, 120, 10, −20, 0, 0x101010, 0)`; messages.md §1.5):
- Text: 20 px bold dark, mode 4, centred at (235,119).
- Stays up 2000 ms or until the voice ends. A click dismisses it.
- Strings are in `ref_004755c0`, indexed by `mode*6 + k`:

| k | Card shop | Tool shop |
|---|---|---|
| 0 greeting | #0000有什麼我能\n為你服務的嗎？ | #0005歡迎光臨\n道具店！ |
| 1 prompt | #0001請挑選你想要\n兌換的卡片。 | #0006您要兌換\n什麼道具？ |
| 2 too poor | #0002抱歉！\n你的點數不足！ | #0007對不起，\n您的點券不夠！ |
| 3 full | #0003對不起！\n您的卡片欄已滿！ | #0008很抱歉！您的\n道具欄已滿！ |
| 4 goodbye | #0004歡迎下次再來！ | #0010謝謝惠顧！ |
| 5 | — | #0009這個道具會員\n才能兌換！ (unused) |

### 6.4 State machine (proc `fcn_0042d37f`, l.299; 50 ms timer)
States: 0 greeting → 1 sliding → 2 prompt → 3 interactive → 4 leaving.

**Intro.** It plays once per mode per visit, and only if the animation config is on:
1. The list starts at x = −222 and the grid at x = 640.
2. Each tick: `listX += vx; gridX −= vy; vx −= 3; vy −= 7`, starting from vx = 40, vy = 80 and
   clamped at 5 / 227. This takes about 8 ticks.
3. Then the switch and EXIT buttons are drawn and the prompt balloon appears.

**Shopkeeper idle animation** runs every 100 ms:
- about 1/32 chance to blink;
- about 1/32 chance to change expression;
- mouth flapping while the balloon is up.

These use libc `rand()`; use a separate cosmetic RNG in the native port.

**Left button down in state 3** (hit tests in this order):
1. **Switch button**: x 542–627, y 13–98. Pressed frame.
2. **EXIT**: x 556–636, y 246–286. Pressed frame.
3. **Own grid → SELL immediately, with no confirmation.** x 233–631, y 299–465. Tile:
   `col = (x−232)/80`, `row = (y−298)/56`.
   - The value is `trunc(price × 0.9)` points (`ref_00464364`).
   - A sold tool with id ≤ 8 returns to the global stock.
   - The tile gets the pressed effect at {233+80c, 299+56r, 311+80c, 353+56r}.
4. **Card list → BUY immediately.** x 14–215, y 81–440, `row = (y−81)/24`.
   - If points < price: balloon k=2.
   - If the hand already holds 15 cards: balloon k=3.
   - Otherwise points −= price. The row text turns grey and the row becomes unclickable; each row
     can be bought once. The grid and points are redrawn.
5. **Tool list → BUY one.** x 12–213, y 80–463, `row = (y−80)/48`.
   - If points < price: k=2. If already 9 owned: k=3.
   - Otherwise buy one. The row greys out: **one purchase per row per visit**.

- Edge quirk: y = 441 (card list) or y = 464 (tool list) reads one byte past the row array
  (UNVERIFIED).
- In states 0–2 a click dismisses the balloon and snaps the slide to its end positions.

**Left button up:**
- Switch: toggle mode and redraw everything.
- EXIT: draw the waving arm and EXIT normal, show goodbye balloon k=4, state 4. The window closes
  when the balloon ends.
- Sell: un-press the tile and redraw.

**Right button up:** dismiss the balloon and close **at once**, without the goodbye.

**Owner revenue.** The window returns `ref_0048c343`, the shop owner's revenue:
- each buy adds price × 10;
- each sold card adds its price;
- each sold tool adds price × count.

It is added to the building's +0x28/+0x2c fields (l.2220–2232).

### 6.5 Limits
- Hand: 15 cards. Tool stack: 9.
- There is no cash path; everything is in points.

### 6.6 AI (l.2242–2688)
The AI uses no UI. It runs a sell/buy policy against the full remaining stock, not a displayed
shelf.
- Original bug: the "sell unwanted cards" step indexes `card_table` by hand slot (l.2262).
- The native `services::open_shop` already implements this policy (`include/rich4/services.hpp:37`).

## 7. Magic house (魔法屋) (`fcn_0043380a`, rich4_magic_house.asm:2225)

**Overview.** The **witch picks the target group at random**; the player picks **only the effect**.
There is no cancel once the effect choice is open.

### 7.1 Assets
**Panel.mkf entry 18 (SMP):**

| Frames | Content |
|---|---|
| 18:0 | Background: hexagram with the 12 effect icons |
| 18:1 | Witch idle, drawn at (241,140) |
| 18:2 | Witch casting, drawn at (182,142) |
| 18:3 | Eyes closed, at (286,188) |
| 18:4, 18:5 | Mouth frames, at (286,217) / (286,220) |
| 18:6, 18:7, 18:9, 18:10 | 142x120 label plates |
| 18:8 | 280x173 balloon, origin (140,86), anchored at (320,384), i.e. top-left (180,298) |
| 18:11–22 | 12 group icons, drawn in the ball at (326,296) |
| 18:23–34 | 12 lit effect icons |

Other entries:
- **Panel 19** is a raw 640x480 8-bit **hit mask**. Values: 1–12 = effect triangles, 13 = centre,
  0 = none. Use it directly.
- **Panel 20** is the full-screen exit animation, played with sound 0x3b. Its format is UNVERIFIED;
  see messages.md §3.4.
- **Data.mkf 0x229** (553) / **0x211** (529) are the on-map build and demolish animations for
  effects 5 and 9.

### 7.2 Effects table (`_rich4_magic_house_function_info`, l.2778; index = effect)

| idx | Text | Plate | Label (x,y) | Icon centre | Lit frame |
|---|---|---|---|---|---|
| 0 | 變賣所有卡片 | 18:9 | (208,167) | (322,91) | 18:23 |
| 1 | 抽取命運三張 | 18:10 | (510,150) | (414,82) | 18:24 |
| 2 | 立刻坐牢三天 | 18:7 | (545,88) | (453,157) | 18:25 |
| 3 | 原地停留一回合 | 18:7 | (568,147) | (509,242) | 18:26 |
| 4 | 存入所有現金 | 18:7 | (550,234) | (458,314) | 18:27 |
| 5 | 就地加蓋房屋 | 18:7 | (510,320) | (415,387) | 18:28 |
| 6 | 得一張卡片 | 18:7 | (422,320) | (322,394) | 18:29 |
| 7 | 向後轉 | 18:6 | (134,318) | (239,393) | 18:30 |
| 8 | 變賣所有道具 | 18:6 | (92,250) | (188,316) | 18:31 |
| 9 | 就地拆除房屋 | 18:6 | (72,130) | (131,222) | 18:32 |
| 10 | 住院檢查三天 | 18:6 | (77,85) | (184,161) | 18:33 |
| 11 | 拍賣當格土地 | 18:9 | (122,154) | (225,83) | 18:34 |

**Groups** (`ref_004756b8`; icon 18:(11+g)):
- #0046財產最多的人, #0047土地最多的人, #0048房屋最多的人, #0049現金最多的人
- #0050存款最多的人, #0051點券最多的人, #0052走路的人, #0053騎機車的人
- #0054開汽車的人, #0055神明附身的人, #0056所有男生, #0057所有女生

**Witch lines:**
- #0037進來魔法屋，就得\n完全照我的指示！
- #0038我選出符合條件的人。
- #0039你來決定他們的命運∼
- #0040嘿∼輪到你了！
- #0041天靈靈地靈靈∼

### 7.3 Flow (proc `fcn_004325c2`, 100 ms timer)
1. Intro balloons #0037 → #0038 → #0039. They are skipped when the config flag is set or on any
   click or right-click.
2. Eyes close for 10 ticks.
3. Random group: `rand()%12` until the group is non-empty. The group icon appears in the ball and
   the group name shows as a balloon.
4. #0040, then the effect choice opens.

**Hover** (hit mask):
- The lit icon is drawn at the icon centre.
- The label plate is drawn at the label point, with the text in
  `create_font(14, 0xE0E0E0, 0x202020, 3)`, mode 2.
- Sound id 0x27 plays.
- Unhovering restores the background.

**Click on effect 1–12:** the witch casts (18:2), balloon #0041 shows, the Panel 20 animation plays,
and the effect is applied to every player in the group. Clicking the centre (13) re-shows the group
balloon.

**AI:** a random group plus a random effect. If the AI is itself in the group, it chooses effect 6.
It shows `"%s\n\n%s"` for 1500 ms.

### 7.4 死神 picker
`_rich4_call_god_of_death_entry` (l.2384) is called from the resign path in rich4_ui_options.asm:2759.
It reuses this art:
- Balloon 18:8 at top-left (180,298).
- 72x72 portraits from Data.mkf 2[character] at y 348. x is {238, 330} for 2 candidates or
  {211, 284, 357} for 3.
- Hover draws a 2 px yellow (0xFFFF00) border.
- Right-click cancels.

## 8. Prison and hospital bail screens (rich4_ui_prison.asm:868, rich4_ui_hospital.asm:1477)

**What they are.** These are **not** "you are locked up" screens. A player who *lands on* the
prison or hospital node may pay **points** to free one occupant:
- 30 points for a player slot (0–3);
- 300 points for an NPC slot (4–7: 小偷, 強盜, 流氓, 間諜).

If no slot is occupied, the screen is skipped. No cash, cards or 免罪卡 are involved.

### 8.1 Prison (Panel 63, NPC art Panel 64; BGM 15)

**Scene:**
- Background 63:0, drawn opaque.
- Slot top-lefts: (33,24), (185,24), (336,24), (487,24), (33,183), (185,183), (336,183), (487,183).
- An occupied slot shows a portrait (63:(5 + character) for players, 63:(13 + i) for NPCs), then
  bars 63:3 on top.
- An empty slot shows the open door 63:4.
- Points box 63:21 at (542,432). The value is drawn mode 6 at (622,452) with the 20 px white shadow
  font.

**Hover** (hit rect `[x, x+121] × [y, y+137]`, occupied slots only):
- Tooltip 63:2 (100x95) at (x+20, y+120).
- Text in 16 px bold dark, mode 2, at tooltip-relative (57, 26 / 48 / 70): name, "保釋點數", "%d點".

**Left button up:**
- Points < cost: balloon 63:1 at (230,300) with "#0002抱歉！\n你的點數不足！".
- Otherwise `ui_yesno(320,240)`. On yes:
  - Redraw the slot as an open door.
  - Player slot: gratitude hostility of `−max(days,1) × 100 × price_index`, release, and **close
    immediately**.
  - NPC slot: draw big art 64:(i−4) at (365,450) and show a thank-you balloon:
    - #0124謝謝你！你真是\n我的再生父母！
    - #0125我先走了！大恩\n大德來日再報！
    - #0123太感激了！我一\n定會報答你的！
    - #0126受人點水之恩，\n必當湧泉已報！
  - In both cases deduct the points. Only one rescue is allowed per visit.

**Right button up:** close.

### 8.2 Hospital (Panel 65, NPC art 64; BGM 16)

**Scene:**
- Background 65:0, nurse 65:4 at (104,110).
- Slots at x = 297 / 481 and y = 1 / 121 / 241 / 361. Slots 0–3 are the left column.
- Patient art: 65:(14 + character) for players, 65:(22 + i) for NPCs.
- Points box 65:30 at (8,432). The value is drawn mode 6 at (88,452).

**Balloons** (frame 65:2 at (8,8)):
- Greeting "#0127您好！請問您要替誰\n辦理出院手續？".
- After paying: "#0129ＯＫ！您的朋友已經\n可以出院了！".
- On every exit: goodbye "#0130要保重身體喔！", with the nurse swapped to 65:9 at (91,112).

**Hover:** rect `[x, x+147] × [y, y+102]`. Tooltip 65:3 at (x−80, y), text at tooltip-relative x 41.

**Nurse animation:** blink with frames 65:5/6 or 65:10/11, talk with 65:7/8 or 65:12/13.

**AI (both screens):**
- 50% chance to act at all.
- The candidate slot depends on `state+23`.
- NPC slots need at least 700 points.
- Shows "保釋%s" for 1500 ms. There is no hostility change.

## 9. Native mapping

| Original element | Native API today | Gap / work |
|---|---|---|
| Use-card grid (§1) | `world().active_player().state.cards` (15 `CardSlots`, slot order preserved); `GameSession::use_card(CardUse{id, target, choice})`; `card_targets(id)` gives legal targets | **UI fix:** `draw_inventory` merges by id and shows a list. Rebuild it as the Panel 11:0 grid at (14,130) in slot order with no counts. Passive cards 18–21 are shown, and selecting one plays the popup then returns to the grid. |
| Card art popup (§1.4) | — | **Missing event.** The core emits no `card_used{player, card}` event, so the UI cannot show Data 570+id for AI, passive (免費/免罪/嫁禍/復仇) or human uses at the right moment. Add a `GameEventKind::card_used` carrying the card id, emitted before the target pick. |
| Map target pick (§3) | `DecisionKind::card_target` / `action_target` with `options[].location` | The native shows a numbered list and markers. The original uses hover-pick with animated cursors and no on-map marker. **Missing:** (a) a renderer hit-test `instance_at(screen x,y)` returning the original instance codes (node 1..1999, land 2001+, facility 4001+, 0x8000\|player bits, 0x8000\|obj<<8); (b) a per-card/tool cursor id and frame count. These are UI constants (tables §1.5, §2.3). Validity can reuse `card_targets()` / decision options. (c) No `tool_targets(id)` query exists for tools. Edge scrolling needs `set_camera` plus the clamp 220..2084. |
| Use-tool grid (§2) | `state.tool_amounts[13]`, `state.traffic_method`, `use_tool(id, target, destination)`, `dismount()` | UI only: Panel 11:1, icons 11:(id+1), "×n" counts, dismount tile 14 (11:15/16), which maps to `dismount()`. |
| Steal picker (§4) | `choose_inventory` → `action_target` decision listing card ids (deduplicated) plus tools | **Missing:** the victim slot in the decision (`GameDecision::player` is the actor) and the `allow_tools` flag. Cards are deduplicated, so the 15-slot layout is lost. Expose the victim index and present the victim's raw `cards`/`tool_amounts` in two grids at (14,70) and (14,270). |
| Shop (§6) | `DecisionKind::shop` options: value 100+card buy, 200+tool buy, 300+card sell, 401+tool-1 sell, 0 leave; points in `state.points` | **Behaviour differences:** (1) native `buy_tool` keeps the row until stock runs out, but the original allows one buy per row per visit; (2) native `buy_card` erases the row, but the original greys it in place (same rows, different visual); (3) right-click leaves without the goodbye, EXIT shows it. **Missing:** the shelf in display order with a bought flag per row (the shelf vector order is reachable only through option order); the gift card/tool event (`Shop::gift_card/gift_tool` is not surfaced as an event or notice with the original text); voice ids for balloons. Revenue is handled by the core. |
| Magic house (§7) | `magic_category` → `magic_effect` decisions; `services::magic_categories/effects` | **Mismatch:** the native asks the human for the category. The original picks it randomly (UI `rand()` retry loop) and the human picks only the effect, with no back/cancel. Labels are paraphrased; use the texts in §7.2. **Missing:** a "witch-chosen category" step (core RNG vs cosmetic RNG must be decided against the original's `rand()` order) and a hook for the effect 5/9 animations. |
| Prison / hospital (§8) | `DecisionKind::rescue` (options = slot index, −1 leave); `world.prison_roster` / `hospital_roster` | UI must read the rosters and `character` to place portraits. Labels should use the NPC names 小偷/強盜/流氓/間諜, not "特殊角色N". The yes/no confirm, the thank-you art and the hospital goodbye are UI-only. Verify native `rescue(…, true)` applies the gratitude hostility only for humans and only for player slots (UNVERIFIED). |
| Descriptions (§5) | none | Load help.mkf entries 57+ / 87+ (index tables §1.5 / §2.3) only if the native Help screen is built. |
| Voices in balloons | `GameEvent::voice_category` exists for speech | The shop, magic, hospital and prison balloon strings carry fixed voice ids (#0000–#0130). The UI can play them directly; no core change is needed. |

### UNVERIFIED items (to confirm against the original under Wine)
1. Whether Data.mkf 584 (停留卡 art) is intact in the original (§1.5).
2. Whether the toolbar overlap of the card-popup message box (y 28–39) is visible (§1.4).
3. The 紅卡/黑卡 stock-choice UI.
4. The meaning of the AI predicates `fcn_0041e69e` / `fcn_00420e9a`, which are only named here.
5. Which MIDI files BGM ids 6, 7, 15 and 16 correspond to.
6. The Panel 20 animation format.
7. The shop's one-byte-overflow row edge (§6.4).
