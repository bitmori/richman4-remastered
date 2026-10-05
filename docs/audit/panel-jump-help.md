# Asset coverage audit: Panel.mkf, jump.mkf, help.mkf

Input: `.local/coverage/used.txt`, a trace of every UI gallery plus 30-day games on all 8 maps with
effects on. The trace binary is older than `tools/screens/spinners.cpp`, so Panel 67–71 (slot machine
and cupid wheels) count as implemented. Contact sheets are in `.local/audit/panel-jump-help/`.

Classes:
- **A**: the native code handles it, but this run did not exercise it.
- **B**: the original uses it and native does not implement it.
- **C**: the original does not use it either.

How the original loads these archives:
- Panel indices come from every `_read_mkf(_rich4_panel_mkf, n)` site.
- jump.mkf is loaded locally (`'JUMP.MKF'`) in rich4_new_game.asm and rich4.asm:7173.
- help.mkf is loaded only by `_rich4_ui_help_entry`.

No entry in these three archives is class C. Every entry has an original reader.

## Panel.mkf (113 entries; used: 0–5 7 9–13 18 19 21 23 24 26 27 30 33 36 57 63 65–75)

| entries | content | original use (asm) | class | native path / player impact |
|---|---|---|---|---|
| 6 | dice FLC for 3 dice | `_rich4_init_panel_resources` loop 4..6 (rich4.asm:11946) | A | `main_screen.cpp:187` decodes `3+dice count`. The run never rolled 3 dice. |
| 8 | GO-button hit mask | init_panel_resources (rich4.asm:11939) | A | `main_screen.cpp:271` (decoded lazily, never logged) |
| 14 | lottery bar neon sign FLC (5 frames) | `_rich4_ui_letou_bar_entry` LT:2932 | B | Lottery shop has no animated neon at (8,8). Cosmetic; every lottery visit. |
| 15, 16, 17 | monthly lottery draw: stage/hosts/balls (SPR), cage spin FLC (42 f), winner FLC (37 f) | `_rich4_ui_letou_kaijiang_entry` LT:3033–3057, called on the 15th from core_actions:4839 | B | Native only emits a "彩票开奖：N" notice (`src/game.cpp:3309`). Happens monthly whenever a ticket was sold. Spec: finance-menus.md §7.2. |
| 20 | magic-house cast FLC (hexagram, 25 f, 640×480, sound 0x3b) | `fcn_0043380a` magic_house.asm:2256 | B | The native magic house applies the effect with no cast animation. Every magic-house use. |
| 22 | number-pad raw hit-id map | `fcn_00453544` rich4.asm:27503 | A | Replaced by the `pad_keys` rectangles in `finance.cpp:25`. |
| 25 | month-end bank report: 大富翁4 wallpaper, bank lady 25:19 with eye/mouth frames, MONEY board 25:5, rank digits 1–4, player cards, bubbles | `fcn_00439bfa` rich4.asm:18993, called at each month boundary (core_actions:4844). Shows 存款／利息／總資產 per player, 貸款中, ranking, voice "#0122其他人還要更努力喔！" | B | Native credits the 10% interest silently (012-native-turns.md). A full-screen report the player sees **every month**. |
| 28–29, 31–32, 34–35, 37–56, 58–62 (i.e. 27+3c..29+3c except frame-0 portraits seen) | auction bidder sprites per character: 27+3c bidding (5 f), 28+3c "can't bid" (1 f), 29+3c winner (5 f) | `_rich4_ui_auction_entry` auction.asm:2798–2830 | A for 27+3c (`market.cpp:495`; only the characters that were in the run got logged). **B** for 28+3c and 29+3c. | Native draws only frame 0 of 27+3c. It has no out-of-bid pose, no winner animation and no bidding animation. Cosmetic; every auction. |
| 64 | big NPC art (4 NPCs) for the prison/hospital rescue thank-you | prison/hospital entries (prison.asm:903, hospital.asm:1512); cards-tools-shops.md §8.1–8.2 | B | The native rescue screen (`items.cpp:974+`) has no NPC thank-you scene (64:i−4 plus balloons #0123–#0126). Rare. |
| 72 | remote-dice picker (bar + faces 1–6) | `_rich4_use_tool_yaokongtouzi` tool_yaokongtouzi.asm:39 | B (cosmetic) | Rule works through `choose_tool_target` with a generic picker. The original 6-face bar at (92,300) is missing. |
| 76 | monthly dividend table (592×432) | `fcn_0042ba97` ST:3064, on the 15th (core_actions:4838) | B | No 上市公司分紅 screen. Dividends are paid silently. Monthly, once anyone holds stock. Spec: finance-menus.md "Monthly dividend screen". |
| 77 | AI settings (託管) panel + 12 head icons | `_rich4_ui_ai_settings_entry` ai_settings.asm:850, toolbar icon 2 | B | Toolbar shows "託管功能尚未提供" (`native_game.cpp:1205`). Low use. |
| 78, 79 | READY/GO FLC; big score digits | all three mini-games (small_games.asm:4240/4248, 4446/4454, 4532/4540) | B | See the mini-game rows below. |
| 80–90 | 企鵝挖寶 (penguin treasure dig): ice field 80:0 with HUD, igloo/penguins 80–85, treasures/explosion/coins/gems 86–90 | `_rich4_ui_game_penguin_treasure` small_games.asm:4223 | B | Playground tiles (node kind 6–8, core_actions:2368–2381) run an interactive game for human players when cfg+1 (animation) is on. Native always uses the cfg+1=0 auto result `rand%20+50` points (`src/game.cpp:3128`). Several times per game. |
| 91 | 七彩氣球 (balloon pop): fun-town background + balloons 1–9, ×2, ÷2, ? | `_rich4_ui_game_balloon` :4429 | B | same |
| 92–99, 100–111 | 喜從天降 (things fall from the sky): 92 bg, 93 財神 throwing (19 f), 94 goblin with bomb, 95 chests, 96 sacks, 97 gold bars, 98 coins, 99 bombs; 100+char = the player's catcher sprite (24 f each) | `_rich4_ui_game_xicongtianjiang` :4515 | B | same |
| 93, 100–111 (also) | 財神 + winner character sprite on the stage-clear screen | `fcn_004075c1` new_game.asm:4381 (rich4_main.asm:462/476) | B | See the jump 0–8 row. |
| 112 | "PLAY AGAIN ?" + balloon digits 0–9 countdown | `fcn_00407842` new_game.asm:4600. Called when the only human goes bankrupt (player_bankrupt.asm:247) or on surrender/game end (core_actions:5766) | B | Native shows a plain "遊戲結束" dialog (`native_game.cpp:1238`). Once per lost game. |

## jump.mkf (71 entries; used: 0–8 18 39 45–49 57 59–61 69)

| entries | content | original use | class | native path |
|---|---|---|---|---|
| 0–7 (unused subset) | raw 640×480 stage×map backdrops | `_rich4_init_new_game`; also the stage-clear screen `fcn_004075c1` (backdrop `stage*4+map` + jump 8 plates) | A | `menus.cpp:164` (new-game backdrop). The stage-clear reuse is B (below). |
| 9–17, 19–38, 40–44 | rider sprites 9+3·char+traffic | `_rich4_select_nth_player` new_game.asm:1172 | A | `menus.cpp:245` (only the picked character/vehicle combos were logged) |
| 50–56, 58 | jump-out FLC 47+char | `fcn_00415872` rich4.asm:7166 | A | `intro.cpp:57` |
| 62–68, 70 | parachute FLC 59+char | same | A | `intro.cpp` |

All of jump.mkf is covered by native code. The one jump-related gap is the **stage-clear screen**
(`fcn_004075c1`):
1. Mark the map cleared (0x4990f0).
2. Draw backdrop `stage*4+map` with "cleared" map plates from jump 8.
3. Draw the winner's sprite (Panel 100+char) and 財神 (Panel 93), with music 6.
4. Wait for a click.
5. Once all 4 maps are cleared, play END%02d/THANKS/END/OVER.AVI. These are stubs, so in practice
   the screen ends at step 4.

## help.mkf (100 entries; used: 57 60 64 65 67 69 71 72 75 77 81 83 88 90 91 95 96 98 99)

| entries | content | original use | class | native path / impact |
|---|---|---|---|---|
| 0 | Help window SMP: 400×400 frame with green category column, red topic list, yellow text pane; 0:1–3 tabs/highlights, 0:4–7 list scroll arrows, 0:8–11 page arrows | `_rich4_ui_help_entry` ui_help.asm:1211 | B | No Help screen |
| 1–56 | Big5 help texts. Pages are split by `@` and lines are NUL-separated. | Help screen categories (`ref_004761b4`/`ref_004761c4`): 操作說明 1; 遊戲畫面 2–7 (日月曆, 地產/其他/物價/股票/資金資料); 遊戲指令 8–19 (LOAD, SAVE, 卡片, 交易, …, 股市, 前進, 道具, 說明); 房地產 20–22; 特殊地點 23–38 (七彩氣球, 公園, 企鵝挖寶, 百貨公司, 命運, 得10/30/50點, 喜從天降, 新聞, 監獄, 銀行, 樂透, 醫院, 魔法屋); 特殊人物 39–56 (乞丐, 土地公, 財神, …, 天使, 強盜, 惡犬) | B | Toolbar "?" shows "說明功能尚未提供" (`native_game.cpp:1203`). The Options menu also calls `_rich4_ui_help_entry(-1,-1)` (ui_options.asm:2719). |
| 57–86 / 87–99 (the unused subset) | card (30) and tool (13) descriptions | Help screen categories 卡片 / 道具 | A | `items.cpp:19–21, 233` shows them as a native-only touch help in the shops and item screens. The run only tapped some items. |

**The original Help screen** (`_rich4_ui_help_entry(x,y)`):
- It is a draggable 400×400 window. It opens at (20,60) from the toolbar "?" (icon 0, hotkey cfg+64
  輔助說明) and centred from Options.
- 12 px dark font (0x101010).
- Left column: 8 category buttons. Middle (red): the topic names of the chosen category, scrolled
  with 0:4–7.
- Right (yellow): the help.mkf text for that topic. `@` splits pages, which are flipped with 0:8–11.
- Clicks use sound `ref_00482322`. Right-click or outside closes it.
- It is pure reference text and has no game-state effect.

## B gaps, ranked by player impact

1. **Interactive mini-games** (Panel 78–111; 企鵝挖寶 / 七彩氣球 / 喜從天降). Every human landing
   on a playground tile. Port `small_games.asm` per game behind the cfg+1 animation switch. The
   reward then replaces `rand%20+50` for humans (RNG order must be checked).
2. **Monthly lottery draw show** (Panel 15–17 + 13). Monthly. Add a `lottery_draw` event
   {number, winner, payout, tickets} and a screen per finance-menus.md §7.2. The FLC decoder exists.
3. **Month-end bank interest/ranking report** (Panel 25). Monthly. Emit per-player
   {deposit, interest, total, loan} at the month boundary and draw `fcn_00439bfa`'s layout. Spec it
   first, because no doc covers it yet.
4. **Monthly dividend table** (Panel 76). Monthly. Emit a dividend event (company × player matrix)
   and draw per finance-menus.md.
5. **Game end: PLAY AGAIN? countdown** (Panel 112) and **stage-clear screen** (jump 0–8, Panel 93,
   100+char). Once per game, at the climax. Replace the plain "遊戲結束" dialog with
   `fcn_00407842` / `fcn_004075c1`.
6. **Help screen** (help.mkf 0–56 + 0). Toolbar "?". Implement a scrollable 3-pane window on
   help 0, reusing the items.cpp Big5 text loader.
7. **Magic-house cast animation** (Panel 20). Every magic-house effect. Play the FLC with sound 0x3b
   before applying the effect.
8. **Auction bidder poses** (Panel 28+3c / 29+3c, animated 27+3c). Every auction. Switch the sprite
   by bidder status and animate the frames.
9. **AI settings / 託管** (Panel 77). Rare. Needs a GameSession mutator for controller flags and
   strategy (finance-menus.md item 17).
10. **Remote-dice picker art** (Panel 72). Rare. Draw the 6-face bar at (92,300) in place of the
    generic picker.
11. **Lottery neon sign** (Panel 14). Cosmetic. Loop the FLC at (8,8) in `draw_lottery`.
12. **Prison/hospital NPC rescue thank-you** (Panel 64). Rare. Draw 64:(i−4) at (365,450) with
    balloons #0123–#0126.
