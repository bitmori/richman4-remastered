# Asset coverage audit: Effect.mkf, Speaking.mkf, map.mkf, and MIDI music

Inputs: `.local/coverage/used.txt` (all UI galleries, plus 30-day games on all 8 maps with effects on).
The trace binary predates the slot machine and cupid wheel, so Effect 1/51/52 now play
natively (`tools/screens/spinners.cpp`). Scratch work is in `.local/audit/audio/`:
`asmbytes.py` (an asm data-byte reader), `presets.txt` (every sound-preset play site),
`speaking.md`, `map.md`, `music.md`, `sheets/` (map.mkf contact sheets), and `effect/` (dumped WAVs).

Classes: **A** = handled natively but not exercised by the trace; **B** = used by the original but
missing natively; **C** = unused by the original too.

## 1. Effect.mkf (115 entries)

The original has exactly three ways to play an Effect. Every one was enumerated:

1. **Preset tables** `{u32 index, u32 buffer}…, -1`. They are loaded by `_rich4_init_sound_effect_info`, played by
   `_rich4_play_sound_effect(&slot, loop)` and stopped by `fcn_004542e9`. There are 16 tables:
   - global `ref_0048231a` = 0 1 2 4 3
   - board `ref_0048234a` = 7 9 10 32 33 34 35 36 37 38 43 44 45 46 53 47 48 49 50 54 55 56 15 62
   - new-game 5; intro 25 26; magic house 39
   - penguin 11–18; balloon 19–21; 喜從天降 22 23 24 15
   - lottery booth 31; lottery draw 57 58
   - stock 40 41; dividend 61; auction 29 63
   - monthly report 27 28 60; slot machine 51; cupid wheel 52
2. **The FLC player** `fcn_0045144f(flc,x,y,flags,sound)` (rich4.asm:25526). Its 5th argument is a direct
   index. All 55 call sites are listed in `.local/audit/audio/callargs.py fcn_0045144f 5`.
3. **The holiday table** at 0x47ff4c, sound field +6: 90 (New Year fireworks) and 114 (Christmas).

There is no other `_rich4_effect_mkf` reader.

Used in the trace: 0–4, 7, 9, 10, 25, 26, 33, 34, 39, 49, 50, 55, 62, 80, 82, 91, 102–106, 108–112.

| Entries | Content | Original use (asm) | Class | Native path / player impact |
| --- | --- | --- | --- | --- |
| 1, 51, 52 | Stop click, slot-machine rattle, cupid-wheel rattle | presets `0x48231a`[1], `0x475d3c`, `0x475d4c` (rich4.asm:19549, 19972) | A | spinners.cpp:35 (added after the trace) |
| 81, 83 | Missile, nuclear missile | feidan.asm:96, hezifeidan.asm:96 | A | effects.cpp `missile`/`nuclear_missile` |
| 90 | Fireworks at a level-5 building (also the New Year holiday) | rich4.asm:4553 `fcn_0040b0cd`; holiday row | A (building) / B (holiday) | effects.cpp `top_floor_sparkle` |
| 95, 97, 100 | Demon puff, demolish card, bankruptcy | gods.asm:1415, chaichuka.asm:266, bankrupt.asm:234 | A | effects.cpp |
| 107, 113 | 大窮神 and 死神 appearances | gods.asm:921, 1178 | A | effects.cpp `god_appear` table |
| **44 45 46 53 / 47** | **Walk loop** on foot / motorbike / car / 工程車, and on water | board slots 0xb+(traffic&3) and 0xf, kept in `0x4749d4`; started in `fcn_0040dd1f` (player_utils.asm:1043–1118) and `_rich4_update_player_sprite` (rich4.asm:5495–5761); stopped and restarted throughout `_rich4_player_move_one_step_done` | **B** | Silent on every move of every turn. Native plays no movement sound at all |
| **92** | Ambulance (sent to hospital) | hospital_utils.asm:107 (Data 524) | **B** | Missing FLC and sound. Frequent (mines, dog, cards) |
| **94** | Police car (sent to prison) | prison_utils.asm:107 (Data 538) | **B** | Missing FLC and sound. Frequent |
| **32** | Time-bomb tick, looped while carried | board[3]: core_actions.asm:2704–2877, player_utils.asm:664/746 | **B** | `Audio::play_loop` exists. Every time-bomb game |
| **54** | God leaves (detach) | board[19]: gods.asm:238 `_rich4_detach_god` (expiry core_actions:4483, 送神符, replacement) | **B** | Every god expiry |
| **35 / 36** | Pick-up of the tool gift (object 13) / points gift (object 14) | board[6]/[7]: core_actions.asm:2981, 3070 / 3121, 3167 | **B** | Every pickup |
| **98 / 99** | Item / point-coupon square marker (Data 537/536 at (204,180)) | core_actions.asm:2402–2515 | **B** | Missing FLC and sound. Common squares |
| **85 / 93** | Dog: vehicle bump (Data 552) / fight on foot (Data 532, then hospital) | core_actions.asm:2927 / 2953 | **B** | Missing FLC and sound |
| **84** | UFO abduction (Data 533; news UFO Data 539) | player_utils.asm:290 `fcn_0040d375`; news.asm:589 | **B** | Fortune or landing, and news |
| 86, 87, 88, 89 | News: meteor or light dome, explosion, tornado ×2 | news.asm:400, 1999, 2712, 2575 | **B** | News FLCs not ported (text and voice are) |
| 96 | Airplane trip (Data 558) | player_utils.asm:377 `fcn_0040d4e5` (core_actions:4139) | **B** | Rare |
| 15 | 工程車 demolish swing (pose 12) | board[22]: player_utils.asm:918 `fcn_0040d7c4` state 6; also penguin[7] and 喜從天降[3] | **B** | Every 工程車 demolition |
| 56 | Forced turn-around | board[21]: rich4.asm:6424 `_rich4_change_player_direction` (magic house:524) | **B** | Magic-house reverse |
| 38 | Robot doll (機器娃娃, slot 8) walk loop | board[9]: player_utils.asm:1116 | **B** | While the doll walks |
| 59 | Magic-house exit movie (Panel 20) / god-of-death exit | magic_house.asm:1181, 1813 | **B** | Every magic-house visit |
| 27, 28, 60 | Monthly bank-interest statement | month table: rich4.asm:17214, 17935, 17346 (`fcn_00437e61`) | **B** | The screen is missing. Monthly |
| 61 | Monthly stock dividend screen | `ref_004755a8`: ui_stock.asm:2583 (`fcn_0042ba97`) | **B** | The screen is missing. Monthly |
| 57, 58 | Lottery draw (roll, result) | ui_letou.asm:1560, 1752 | **B** | The screen is missing. 15th of each month |
| 31 | Lottery booth pick | ui_letou.asm:1227 | **B** | market.cpp lottery grid plays only the click |
| 29, 63 | Auction bid / gavel | ui_auction.asm:1776, 502 | **B** | market.cpp `draw_auction` plays only the click |
| 40, 41 | Stock buy / sell confirm | ui_stock.asm:2107, 2184 | **B** | stocks/market screens play only the click |
| 114 | Christmas (Santa, Data 554) | holiday table, `fcn_00452444` rich4.asm:26116 | **B** | Holiday movie, sound and music all missing |
| 101 | Game-end sequence (Data 556 after the player's line) | new_game.asm:4713 `fcn_00407842` (from bankrupt.asm:247, core_actions:5766) | **B** | Once per game |
| 5 | Setup-screen timer sound | new-game table: new_game.asm:2401 (`_rich4_init_new_game_callback`, timer state 2) | **B** | Minor |
| 11–14, 16–18; 19–21; 22–24 | Mini-game sounds: penguin treasure, balloon, 喜從天降 | small_games.asm (tables 0x475057 / 0x47509f / 0x4750bf) | **B** (whole feature) | The mini-games are not ported at all (core_actions.asm:2368–2381) |
| 37, 43, 48 | Loaded in the board table, never played (no reference to slots 8/10/16) | load_map.asm:600 only | C | — |
| 6, 8, 30, 42 | Not in any table or FLC call | — | C | — |
| 64–79 | Empty (0-byte) entries | — | C | — |

## 2. Speaking.mkf (1374 entries, 0–1373; 166 used, 1208 unused)

Every clip is triggered by a string that starts with `#NNNN`. `_rich4_draw_text` (rich4_text_drawing.asm:349–381)
and `_rich4_player_say` (rich4.asm:23696) both call `fcn_0045441a(NNNN)`, and nothing else does.
**There are no entries at 1374 or above.** Native: `messages::Overlay` → `Audio::play_speech`
(native_game.cpp:2304). Voices come from tools/screens/messages.cpp (event l.121, card/tool l.146,
news 149+id l.191, fortune 185+id l.228). **No other native screen plays speech.**

| Entries | Content | Original use | Class | Native path / player impact |
| --- | --- | --- | --- | --- |
| 1050+c·27+0…23 (other characters) | Event voices: luck, misfortune, income, toll, fortune, build, grudge, prison/hospital, god | rich4_player_say_on_events.asm etc. | A | game.cpp / gods.cpp / events.cpp voice categories. Characters were unseated in the trace |
| 1050+c·27+**13** | "Owner absent, toll waived" after the box "%s坐牢中/住院中/… 免收%s！" | core_actions.asm:5441 | **B** | game.cpp:822–828 / 1045–1050 just `finish_turn()`, with no box or voice. Frequent |
| 1050+c·27+**24 / 25 / 26** | Winner / bankrupt player / game-over line | bankrupt.asm:293, 484; new_game.asm:4684 | **B** | Never emitted (gallery only) |
| 426–1049 (except card 7 rows 1–2), 234–425 | Card lines (user/self/victim) and tool lines | rich4_card_*.asm, rich4_tool_*.asm | A | cards.cpp `say`, tools.cpp:231, game.cpp |
| 24 ids of card 7 (改建卡) rows 1–2 (457, 465, 509 … 1037) | "蓋什麼好呢？" / "竟然破壞我的好事？！" | No reference | C | — |
| 149–184, 185–233 | News and fortune announcer | news.asm, fortune.asm | A | messages.cpp |
| 0–4 / 5–10 | Card shop / tool shop clerk | rich4_shop.asm | **B** | items.cpp:530/532 shows the text with no voice. Every shop |
| 75–91 | Bank clerk and director | rich4_ui_bank.asm | **B** | finance.cpp `say()` is text only. Every bank pass |
| 11–16 | Lottery kiosk | rich4_ui_letou.asm (bar) | **B** | market.cpp:423 is text only |
| 17–36 | Monthly lottery-draw announcer (20–31 = winner name) | `_rich4_ui_letou_kaijiang_entry` | **B** | The screen is missing (game.cpp:3306 shows a notice) |
| 92–122 | Monthly bank report: interest, 本月悲情人物 96–107, 本月冠軍 110–121 | rich4.asm `fcn_00439bfa` | **B** | The screen is missing (game.cpp:3177 shows a notice) |
| 37–41, 46–57 | Magic-house witch and the 12 target groups | magic_house.asm `fcn_004325c2` | **B** | items.cpp:846/893/932 is text only |
| 131–132, 135–148 | Auction announcer (136–147 = buyer name, 148 = no sale) | rich4_ui_auction.asm | **B** | `draw_auction` has no voice |
| 127–130 / 123–126 / 42–45 | Hospital nurse / thanks from rescued NPCs / god-of-death prompts | ui_hospital, ui_prison, magic_house:2515 | **B** | Rare |
| 58–74, 94, 133, 134 | Real clips with no `#NNNN` string anywhere in rich4.exe | — | C | — |

## 3. map.mkf (298 entries; 113 unused)

The native code mirrors every index formula from `_rich4_load_map` (load_map.asm:71–500), in
board_renderer.cpp, main_screen.cpp and messages.cpp. It decodes lazily, so "unused" mostly means
"off camera, or that level was never reached".

| Entries | Content | Original use | Class | Native path / player impact |
| --- | --- | --- | --- | --- |
| **26 f0–3** | Yellow land/facility overlay for streets under 漲價卡 (0x50) / 查封卡 (0x51) | `fcn_0040829d` rich4.asm:2170/2354 (tmp_state +0x17; set at zhangjiaka.asm:84, chafengka.asm:84) | **B** | The core sets `property.temporary` (cards.cpp:665), but nothing draws it. Affected streets are invisible for the whole effect period |
| 26 f4 | Node circle stamped into a mask buffer on full redraw | `fcn_00409b18` rich4.asm:2918 | B? (UNVERIFIED, likely low) | Check what reads 0x474938 first |
| 31, 34, 35 | Faces for 阿土伯, 糖糖, 烏咪 | 0x1b+char | A | `'m', 27+character`. Not seated in the trace |
| 41–73 (various) | Land buildings, levels 2–5 | 0x27+m·5+lvl-1 | A | board_renderer land branch |
| 79–85 | Chain store, maps 0–6 | 0x4f+m (改建卡) | A | board_renderer `property.type` |
| 87–171 (various) | Facility sets | 0x57+i / 0x68+stage·17+i | A | `facility_base()` |
| 297 | Fortress scenery (map 0) | 0x26+259 | A | `scenery_resource` |
| 74–78, 86 | Map 7 land buildings and chain store | Map 7 has 0 land records | C | — |

## 4. Music (MIDI) by context

The original works like this (rich4_media_music.asm):
- `fcn_004549cf(i)` pushes the current track and loops `MIDI(i+1)` (table `ref_0047e793`: MIDI01–13, 14-1, 14-2, 15, 16). Bit 0x8000 means no push.
- `fcn_00454bcc` pops the saved track and resumes it at the saved position.
- `fcn_00454d91(n)` plays the board playlist RICH08, 16–22, and advances to the next track when one ends.

Native (native_audio.cpp) only ever plays that 8-track playlist, starting at launch. The only switch is the Options song pick (native_game.cpp:986–990).

| Context | Original MIDI | asm ref | Native | Class |
| --- | --- | --- | --- | --- |
| Main menu | MIDI01 | ui_main.asm:483 (and :187 after Options) | playlist | **B** |
| New-game setup | MIDI02 (no push) | new_game.asm:4009 | playlist | **B** |
| New-game intro | silent, then RICH08 when the board starts | rich4.asm:7236, 7259 | playlist keeps running | B (minor) |
| Board play | RICH08 → RICH16…22, looping | `fcn_00454d91`, notify `fcn_00454d2c` | same | A |
| Load save / time machine | next playlist track | save_load.asm:978, shiguangji.asm:54 | no change | B (cosmetic) |
| Bankruptcy movie | MIDI03 | bankrupt.asm:216 | playlist | **B** |
| Bank counter, loan foreclosure | MIDI05 | ui_bank.asm:3557, 3743 | playlist | **B** |
| Auction (and bankruptcy liquidation) | MIDI06 | ui_auction.asm:3139, bankrupt.asm:442 | playlist | **B** |
| Shop / lottery booth / victory sequence | MIDI07 | shop.asm:2196, ui_letou.asm:2938, new_game.asm:4531 | playlist | **B** |
| Magic house / god of death | MIDI08 | magic_house.asm:2269, 2512 | playlist | **B** |
| Lottery draw (15th) | MIDI09 | ui_letou.asm:3063 | — (no screen) | **B** |
| Monthly bank report | MIDI10 | rich4.asm:19212 | — (no screen) | **B** |
| Mini-games 喜從天降 / balloon / penguin | MIDI11 / 12 / 13 | small_games.asm:4638, 4481, 4335 | — (no feature) | **B** |
| Christmas (maps 0–3; 1 day) / Lunar New Year (TW, CN; 3 days) | MIDI14-1 / MIDI14-2 | `fcn_00452444` rich4.asm:26085 (table 0x47ff4c, flag 4; hold `0x46cb06`) | playlist | **B** |
| Prison rescue / hospital | MIDI15 / MIDI16 | ui_prison.asm:917, ui_hospital.asm:1529 | playlist | **B** |
| Stock market, monthly dividend | no change | ui_stock.asm | playlist | A |
| — | MIDI04 | No caller | — | C |

This also settles cards-tools-shops.md UNVERIFIED #5: BGM 6 = MIDI07, 7 = MIDI08, 15 = MIDI15, 16 = MIDI16.

## 5. Ranked B gaps (by player impact)

1. **Walk-loop sounds 44/45/46/53/47** (every move). Hint: when MainScreen starts the walk, call `play_loop(44 + traffic)` (traffic 3 → 53, water → 47), and call `stop_loop()` on arrival.
2. **Context music** (menu, setup, bank, shop, lottery, magic, auction, prison/hospital, bankruptcy, victory, holidays). Hint: add `Audio::push_music(file)` / `pop_music()`, which saves the fluid tick and loops the context MIDI, and call them at each screen entry and exit.
3. **Bank clerk voices 75–91 and shop clerk voices 0–10** (every bank or shop square). Hint: give finance.cpp `say()` and the items.cpp:530/532 tables a `#NNNN` id, then `play_speech`.
4. **Hospital/prison movies + Effect 92/94** (frequent). Hint: add `EffectKind::ambulance` (Data 524 at (0,210), commit 30) and `police` (Data 538, commit 18) to effects.cpp.
5. **Owner-absent toll waiver** (box + event voice slot 13). Hint: emit the box and voice before `finish_turn()` at game.cpp:828/1050.
6. **漲價卡/查封卡 street overlay** (map 26 f0–3). Hint: expose `temporary` in `PropertyState` and blit `0x1a` frame `(temporary&1)+(facility?2:0)` over the property.
7. **Small board sounds**: god leaves 54, gift pickups 35/36, time-bomb tick 32 (loop), 工程車 swing 15, turn-around 56, robot-doll loop 38. Hint: emit them from the matching GameEvents or effects via `audio_->play_sound`/`play_loop`.
8. **Square/actor movies**: item/coupon marker 98/99, dog 85/93, UFO 84, airplane 96, news 86–89. Hint: add new `EffectKind`s using the Data entries listed in effects.md §6.
9. **Monthly screens**: lottery draw (57/58 + voices 17–36 + MIDI09), bank report (27/28/60 + voices 92–122 + MIDI10), dividend (61). Hint: core events carrying the tables, plus overlays.
10. **Magic house**: voices 37–41/46–57 and the exit movie 59 (Panel 20). Hint: add voice ids to the items.cpp magic texts and play Effect 59 on leaving.
11. **Lottery kiosk voices 11–16** and booth Effect 31. Hint: market.cpp lottery grid.
12. **Auction voices 131–148** and Effects 29/63. Hint: `draw_auction` plays 131, 132, 135, then 136+c or 148.
13. **End of game**: event slots 24/25/26 and Effect 101 (Data 556). Hint: on `player_bankrupt`/`game_finished`, voice 25 for the loser, voice 24 for the winner, then the 556 movie.
14. **Christmas holiday** (Data 554 + Effect 114 + MIDI14-1), New Year fireworks (90 + MIDI14-2). Hint: drive both from the `holidays` table rows with flag 4.
15. **Mini-games** (Effects 11–24, MIDI11–13; feature not ported). Rare: nurse 127–130, NPC thanks 123–126, god-of-death prompts 42–45, setup sound 5.
