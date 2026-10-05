# Player-facing gaps: unfinished rules, text quality, hover-only UI

Audit date 2026-10-05, run against the working tree with a merge in progress. Research only; no code was changed.
Priority: **P1** = dad will probably hit it in his first few games. **P2** = he will notice it eventually. **P3** = polish, or not reachable.

**Frequency sweep.** `build/native/rich4-native.app` with `--headless --start-new`, maps 0-7 × seeds 1/2/3/5 at 40 rounds, plus map 7 seed 12. That is 33 games, and they produced two kinds of dialog:
- `这个设施的规则尚未完成` once, on map 2 seed 5 (node 84, code 8002 = the prison building).
- The English news-15 text twice, both on map 7 seed 12.

All other games were clean.

**How these reach the screen.** `unsupported()` (src/game.cpp:268) and `UnsupportedRule` caught by `run_rule` (game.cpp:2700-2704) behave differently by player:
- For a human, they open a `DecisionKind::notice` paper dialog with the raw text and a `继续` button.
- For an AI, the turn just ends.

`GameEvent::message` is never drawn; `messages::from_event` uses only the original announcement and speech tables. So the roughly 200 Simplified literals in events/gods/special_actors/game.cpp only reach logs, `events.jsonl` and report.json.

## 1. Unfinished rule paths

| Site | Trigger | Original behaviour | Likelihood | Correct native behaviour | Pri |
|---|---|---|---|---|---|
| game.cpp:3113 `这个设施的规则尚未完成` | Landing on a kind-0 node with code 8001 (hospital building) or 8002 (prison building). Map loader `rich4_load_map.asm:549/559`. These nodes exist on 7 of 8 maps: m0 n1/n23, m1 n144, m2 n84, m4 n128, m5 n42/n135, m6 n135/n141, m7 n101. | No message. The landing dispatcher (`rich4_player_core_actions.asm:369-371`) does the range checks at `loc_0041a168`/`loc_0041a998`, then falls into the common tail `loc_0041b077`. That tail runs `fcn_0040f381` and `fcn_00448a7e` (likely the after-property god check and flight bombardment; not decoded). | 1 hit in 33 sweep games. These nodes are on the road, so a human lands on them now and then. | Handle any unmatched code like an empty property: run the `property()` tail (`gods::after_property`, `flight_bombardment()`), then `finish_turn()`. No dialog. | **P1** |
| events.cpp:407 news 15, shown in English: `Event requires a built ordinary land (…divides by zero)` | `news_eligible(15)` (events.cpp:220-222) counts any built property, facilities included. The handler (events.cpp:402-404) only picks `level!=0 && code<4000`. The dialog appears when facilities are built but no ordinary land is. | Crash. `rich4_news.asm fcn_0044a453` divides by the number of matching lands (`idiv esi`) with `esi=0`. | Map 7 (1 ordinary land, 21 facilities): plausible once a facility is built. 2 of 5 map-7 sweeps hit it, both on seed 12. Other maps: very rare. **It also repeats.** `apply_news` (events.cpp:349) doesn't advance `news_cursor` on failure, so every later news tile shows the same English dialog until an ordinary land gets built. | Make eligibility match the handler: `p.level != 0 && p.code < 4000`. The deck then moves on, and no RNG is consumed. Also advance or reroll the cursor whenever an event can't run. | **P1** (map 7) |
| events.cpp:395/420/430/457/529/577/598/619/674 (data guards for news 4/20, 6/14, 7, 8-10, 18, 27/28, 29, 30-35 and fortune 0/1) | Candidate set is empty | `idiv 0` crash (e.g. news 29/30 `fcn_0044b25b`/`fcn_0044b374`) | Unreachable: eligibility matches, and every map has lands, stocks and at least 4 companies | Keep as a defensive skip with neutral Traditional text, `本次事件略過。` | P3 |
| events.cpp:361-369, 654-667 (callback-absent guards) | `EventCallbacks` field missing | n/a | Unreachable: `event_callbacks()` game.cpp:2226-2247 wires them all; only unit tests hit these | None, or a debug assert | P3 |
| game.cpp:768/818/991/1265 `房产记录无效`, `房产所有者无效`, `设施记录无效`, `商业公司记录无效` | Node code out of the table's range, or a bad owner | Reads garbage memory | Unreachable on shipped maps | Silent skip, or Traditional text | P3 |
| game.cpp:3160 `<name>规则尚未完成；继续将略过本次事件` | Node kind ≥ 17 | Silent. `cmp ebx,0x10; ja loc_0041b3d0` (core_actions:361) ends the turn with no effect | No map has kind > 16 | Silent `finish_turn()` | P3 |
| game.cpp:2843 `这项魔法的效果尚未完成`; gods.cpp `…尚未接入` ×10; special_actors.cpp ×6; tools.cpp ×5 / `未知道具`; cards.cpp `Required card rule callback is unavailable` | `!callbacks.X` guards | n/a | Unreachable: every callback is assigned in game.cpp | Debug assert or Traditional text | P3 |

**Unported, but these skip silently and show no dialog:**
- Minigames (node kinds 6-8) always use the original's "minigames off" auto-reward (game.cpp:3127; STATUS:43).
- Facility types ≥ 5 just call `finish_turn` (game.cpp ~1000). The shipped maps appear to use only types 0-4.

All 36 news and 49 fortune ids have handlers.

**Rule gap outside the unsupported paths: the monthly stock dividend (上市公司分紅).**
- The original calls `fcn_0042ba97` (rich4_ui_stock.asm) on day 15 (`core_actions:4835-4838`). It pays company funds to shareholders pro rata and shows Panel 76 (finance-menus.md "Monthly dividend screen").
- I found no dividend payout anywhere in src/: company `funds` only accumulate (services.cpp:196, game.cpp:1403, events.cpp:626-632).
- docs/audit/panel-jump-help.md:33 says dividends are "paid silently". The code doesn't back that up; recheck before fixing.
- Priority **P1**: shareholders never get income, which changes the stock game.

## 2. Text quality (strings that reach the screen)

Two places draw decision prompts and labels: `draw_map_decision` (native_game.cpp:1363/1381) and the generic `draw_decision` (native_game.cpp:1456/1477). The bank, stock, lottery, auction, shop, magic, rescue, spinner and buy/upgrade yes/no decisions draw original artwork instead, so they don't use the core prompt or labels.

| String today | Location | Original Big5 (cite) / proposed Traditional | Pri |
|---|---|---|---|
| Unsupported notice: English `missing()` text, or `…尚未完成` / `…无效`, with button `继续` | game.cpp:268-273, events.cpp:101 | No original exists; fix the rules in §1. Interim: `本次事件略過。` / `繼續` | **P1** |
| Free card: `使用免费卡免除这笔费用？`, `使用免费卡` / `保留卡片` | game.cpp:1713-1714 | `"%s\n\n是否使用免費卡？"` (0x465388, rich4_card_passive.asm:409). The original is a yes/no box, so route it through `messages::draw_yes_no` | **P1** |
| Build facility: `选择要建设的设施` / `选择魔法建设的设施`, `旅馆 百货公司 加油站 研究所` | game.cpp:1018, 1028, 1851-1856 | `"請選擇設施類別"` (0x465289, rich4.asm:20371) with the icon strip Data 517:4. Labels: `旅館 百貨公司 加油站 研究所` | **P1** |
| Remote dice (遙控骰子): `选择道具目标`, `1点`…`6点`, `取消` | game.cpp:2471-2474 | The original has no text, only the Panel 72 picker at (92,300) (effects.md:198). Interim: `選擇點數`, `1點`… | **P1** |
| Tool/card target side panel: `选择道具目标`, `选择目的地`, `<卡名>：选择目标`; labels `道路N`, `房产N`, `设施N`, `特殊角色N`, `地图物件N` | game.cpp:2250-2266, 2355, 2471-2491 | The original picks with the cursor only and has no prompt (cards-tools-shops.md §3). Use `選擇目標` / `選擇目的地` / `<卡名>：選擇目標`. Keep the `目的` and card-name prefixes, because items.cpp:425/441 match on them. Labels: real names via `messages::property_name`, `小偷/強盜/流氓/間諜`, the tool name, `道路` | **P1** |
| 紅卡/黑卡 target `股票1`…`股票N` | game.cpp:2259 | Company names from tools/native_stock_names.hpp. The original stock-choice UI is unverified (cards-tools-shops.md:195) | P2 |
| 改建 facility type `选择设施类型`, `空置 旅馆…` | game.cpp:2314-2315 | `"請選擇設施類別"` (0x465289); `空地 旅館 百貨公司 加油站 研究所` | P2 |
| Steal: `选择要取得的卡片或道具` | game.cpp:2370 | Grid picker with no prompt. Result `"搶得%s的\n\n%s"` (0x4652f8). Interim `請選擇要搶的卡片或道具` | P2 |
| Fortune card pick: `选择一张卡片`, `放弃` | game.cpp:1808, 1816 | `請選擇一張卡片` / `放棄`, or better the §4 grid picker | P2 |
| Frame card: `使用嫁祸卡？`, `嫁祸给<名>` / `保留卡片` | game.cpp:1742-1745 | One target: `"是否嫁禍給%s？"` (0x46534e). Several: `"請選擇嫁禍對象..."` (0x46535d). Then `"%s\n\n嫁禍卡生效！"` (0x46533d) | P2 |
| Construction company: `选择工程公司建设的地产`, `房产N`, `放弃选择` | game.cpp:1195-1202 | `"%s\n\n請選擇欲加蓋地點"` (0x463a4a, core_actions:1776). Rename the option to `取消`; the cancel button at native_game.cpp:1406 only matches that text | P2 |
| Toasts `說明功能尚未提供`, `託管功能尚未提供` | native_game.cpp:1203/1205 | Developer-ish. Hide the buttons or implement the features (§4) | P2 |
| Game over: `遊戲結束`, `<名>獲勝！` / `本局已結束` | native_game.cpp:1240-1244 | `"恭喜勝利過關！！"` (0x463176, rich4_new_game.asm:3061), plus End*.avi / Over.avi | P2 |
| Primary stock: `認購原始股\n<公司>每股N元\n最多M股` | finance.cpp:719 | `"%s\n\n每股售價%d\n\n是否認購股份？"` (0x463b75, core_actions:4994) | P3 |
| Settle-failure toasts: `目前無法完成股市/買賣結算`, `目前無法下車/兌換` | native_game.cpp:654-819, 1269-1289 | `目前無法離開`; for stocks use `本日休市` (0x4640e8) where it applies | P3 |
| Touch hints (`點地圖上的目標，再點一次確認`, `按住卡片可看說明，放開就使用。`, …) | native_game.cpp:1375-1400; items.cpp:322-929 | Native additions. Already Traditional and fine | — |
| ~200 core notice/news/fortune/god/AI literals in Simplified | events.cpp, game.cpp, gods.cpp, special_actors.cpp | Log-only; bulk s2t is optional | P3 |

Checked and faithful: speech tables, yes/no boxes, toll, coupon and absence boxes, spinner captions, magic house, shop and bank balloons, stock, options and new-game labels. (`點  卷` is a typo in the original.)

Side bug (report only): the notices say `使用免费卡` and `嫁祸`, but `observe()` matches the Traditional `免費卡`/`嫁禍卡`. As a result report.json's `cards_used_` misses them.

## 3. Hover/right-click-only information

| Info | Original trigger | Native exposure | Gap / fix | Pri |
|---|---|---|---|---|
| **Map-tile popup**: owner, level or type, toll (`_rich4_calculate_land_toll`), chain ×N, name | Left **press** on the board, cleared on release (rich4.asm:11026-11033 → `fcn_00417559`/`fcn_00417c67`; main-screen.md §7) | **None.** There's no board hit-test in tools/screens/main_screen.cpp or native_game.cpp; a tap on the board does nothing | Press-and-hold popup with the D517:0-3 frame, using renderer `instance_at` and the core toll helper. Dad can't check a tile's price or owner before landing | **P1** |
| Prison/hospital bail tooltip (name, 保釋點數 %d點) | Hover over a slot | First tap shows tooltip 63:2 / 65:3, second tap confirms (items.cpp:1001-1013, 1036+) | Covered. Could add a `再點一次保釋` hint | P3 |
| Magic-house effect label plate and lit icon | Hover hit-mask (§7.3) | First tap lights it and shows `…？再點一次就施法`; second tap casts (items.cpp:903-931) | Covered. Separate deviation: native lets the player pick the category, while the original rolls it (STATUS:19) | P3 |
| Card/tool help text (help.mkf 57-99) | Help screen only | Help strip while a grid item is held (items.cpp:342-357); shop strip after the first tap (items.cpp:732-750) | Covered, and better than the original | — |
| Target under the cursor (animated cursor, red ✕, edge scroll) | Hover over the viewport (main-screen §3.2) | Cursor sprite on every valid target; first tap selects and centres, second confirms; side list with 確認目標/取消 (items.cpp:418-481, native_game.cpp:1351-1411) | Covered | — |
| Toolbar hover frame (no text in the original) | Mouse move with y<40 | Frame follows the last touch point (main_screen.cpp:307-334) | Nothing lost. Optional: label on long-press | P3 |
| New-game portrait name tag | Hover over a portrait (NG:1519) | Shown only on the touched cell, and that touch also picks the character (menus.cpp:206-222) | Show the name on press, or under the picked strip | P3 |
| Stock company info (chart, pie, earnings) | Click (not hover) | stocks.cpp:155,195,280-313 | Covered | — |
| Right-click = cancel/close/NO everywhere; minimap right-click = re-follow player | Right button up | On-screen 返回/取消/離開 buttons; Android Back → Escape (native_game.cpp:379-394, 546); full map closes on a tap (main_screen.cpp:661); minimap re-follows on the next turn or roll (main_screen.cpp:171-176) | Covered | P3 |
| Right panel 資金/地產/股票/其他 | Click | main_screen.cpp:345-362 | Covered | — |

## 4. Missing features a casual player would notice

| Feature | State | Fix hint | Pri |
|---|---|---|---|
| Monthly dividend (上市公司分紅) | No payout or screen (see §1) | Pay on the day-15 tick, emit an event, draw Panel 76 | **P1** |
| Help (?) toolbar button | Toast only (native_game.cpp:1203) | Simple help.mkf topic viewer, or hide the button | P2 |
| 託管 auto-play | Toast only (native_game.cpp:1205); the core can't toggle `controller_flags & 4` | `set_managed(player)` plus the Panel 77 dialog | P2 |
| 遊戲時間 / 勝利條件 | Greyed out (menus.cpp:91-94); the game ends only when one solvent player is left (native_game.cpp:1238-1247) | Add day-limit and target-wealth fields plus an end check. The default 無限 matches the original, but games run long | P2 |
| 認輸投降 (surrender) | Greyed out (menus.cpp:392-399); no core API | Resign = bankrupt the player, behind a confirm dialog | P2 |
| Lottery draw show | Only the log notice `彩票开奖：N` (game.cpp:3309); the winner's cash changes silently | Draw event plus the Panel 14/16/17 FLC (`rich4_ui_letou` kaijiang) | P2 |
| Interactive minigames | Auto-reward (STATUS:43) | Port later | P3 |
| Options: 動畫過程, 自動存檔 toggle, 樂曲, 日/月曆, 縮小地圖 | Inert or greyed (menus.cpp:370-420) | Low value on a phone | P3 |
| Voices for cards/tools, loop sounds, toll blinking, hospital/prison FLC | STATUS:19 | Polish | P3 |
| Save/load (AUTO + 5 slots), full map | Done (native_game.cpp:616-680; main_screen.cpp:632-661). main-screen.md §2/§9 is out of date | Update the doc | — |

## Ranked fix list

1. **Hospital/prison building nodes (8001/8002)**, game.cpp:3112: run the empty-property tail, then `finish_turn`, with no dialog.
2. **News 15**: set `news_eligible(15)` to `level!=0 && code<4000`, and stop `news_cursor` from getting stuck on events that can't run.
3. **Make `unsupported()` silent for humans** (log it, finish the turn). Any leftover text becomes `本次事件略過。`/`繼續`.
4. **Press-and-hold map-tile popup**: owner, level, toll and chain, using the D517 frame and `instance_at`.
5. **Monthly dividend**: day-15 payout plus the Panel 76 table. Check first whether any payout already exists.
6. **Free card prompt**: yes/no box `%s\n\n是否使用免費卡？`.
7. **Facility build choice**: `請選擇設施類別` with Traditional labels (the 517:4 icon strip later).
8. **Remote dice**: Panel 72 picker, or at least `選擇點數` / `1點`.
9. **Target side panel**: Traditional prompt and real place, actor and object names, keeping the `目的` and card-name prefixes.
10. **Construction company**: `請選擇欲加蓋地點`, and `取消` instead of `放弃选择`.
11. **Frame card, steal, fortune pick, 紅/黑卡, 改建**: original strings, company names, and `嫁禍` spelling.
12. **?/託管 buttons**: hide them until they're implemented, or ship a minimal help viewer and a 託管 toggle.
13. **Surrender, plus time and victory settings**.
14. **Lottery draw screen, and game-over `恭喜勝利過關！！`**.
15. **Bulk s2t of the log-only core literals** (P3).
