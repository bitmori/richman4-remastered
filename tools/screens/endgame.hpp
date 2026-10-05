// SPDX-License-Identifier: GPL-3.0-or-later
// Original end-of-game screens (rich4_new_game.asm):
//  * fcn_00407842 "PLAY AGAIN ?" (Panel 112): frame 0 at (320,120), balloon digit 9..0 at
//    (320,350) counting down once a second. A click answers yes: the player's game-over line
//    (event slot 26), the "倒閉" movie Data 556 at (0,40) with Effect 101, then the load-game
//    screen (exit code 4). Timeout or right-click: main menu (exit code 1). Shown when the only
//    human goes bankrupt (rich4_player_bankrupt.asm:247).
//  * fcn_004075c1 + window proc fcn_004060e9, the stage-clear screen: the stage*4+map backdrop
//    (jump.mkf 0..7) scrolls left, the winner (Panel 100+character) chases 財神 (Panel 93) across
//    it at a random height. With one human, the map panel (jump 8:15 / 8:20, cleared maps starred)
//    drops in and picking an uncleared map continues the campaign on it (exit code 2). Otherwise
//    "恭喜勝利過關！！" and a click returns to the main menu (exit code 3).
// Presentation only: no game state is read or changed and no game random number is drawn (the
// original's rand() height uses a local generator here).
#pragma once

#include "draw.hpp"
#include "rich4/flc.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>

namespace rich4::native::screens {

namespace ids {
inline constexpr int endgame_play_again = 1100, // full-screen tap answering PLAY AGAIN
    endgame_plate = 1101,                       // + map 0..3 on the stage-clear map panel
    endgame_continue = 1105;                    // full-screen tap closing the multi-human stage clear
} // namespace ids

struct EndgameActions {
    std::function<void()> agree;                   // PLAY AGAIN answered yes: queue the slot-26 line
    std::function<void(bool)> play_again_done;     // true: open Load (code 4); false: main menu (code 1)
    std::function<void(unsigned map)> next_map;    // stage clear: campaign continues on this map
    std::function<void()> to_menu;                 // stage clear without a map choice (code 3)
};

struct StageClear {
    unsigned stage = 0, map = 0; // the map just won
    std::uint8_t character = 0;  // the winner
    std::uint8_t cleared = 0;    // bit per map of this stage, including `map`
    bool single_human = true;    // _num_human_players == 1: the map panel instead of the text
    std::uint32_t seed = 1;      // local generator for the runners' heights
};

class EndgameScreen {
  public:
    enum class Kind { none, play_again, stage_clear };
    void open_play_again();
    void open_stage_clear(const StageClear &info);
    void close();
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool active() const noexcept { return kind_ != Kind::none; }
    // `hold`: a speech bubble is still up (the original blocks on _rich4_player_say).
    void draw(Host &host, bool hold, const EndgameActions &actions);
    // Replay helper: the control that leaves the current screen, or -1 while none is offered.
    [[nodiscard]] int exit_button() const noexcept;

  private:
    void draw_play_again(Host &host, unsigned elapsed, bool hold, const EndgameActions &actions);
    void draw_stage_clear(Host &host, unsigned elapsed, bool hold, const EndgameActions &actions);
    void stage_tick(); // one 50 ms timer tick of fcn_004060e9

    Kind kind_ = Kind::none;
    std::uint64_t last_ticks_ = 0;
    bool started_ = false;
    // PLAY AGAIN.
    enum class Step { countdown, line, movie, done } step_ = Step::countdown;
    unsigned countdown_ms_ = 0;
    std::unique_ptr<flc::Decoder> movie_;
    unsigned movie_ms_ = 0;
    graphics::Image movie_frame_;
    // Stage clear.
    StageClear info_;
    std::minstd_rand random_;
    unsigned tick_ms_ = 0;
    int scroll_ = 0;                  // 0x48a415: backdrop offset in pixels
    int run_x_ = 0, run_y_ = 0;       // 0x48a419 / 0x48a41d (0 = pick a new run)
    bool run_left_ = false;           // 0x48a436
    unsigned winner_step_ = 0;        // 0x48a437
    unsigned god_step_ = 0;           // 0x48a438
    int panel_y_ = -80, panel_speed_ = 30; // 0x48a421 / 0x48a425
    int picked_ = -1;                 // 0x48a439 - 1 once a map is chosen
    unsigned picked_ticks_ = 0;       // 0x48a43b countdown before leaving
    bool leave_ = false;
    std::optional<graphics::Image> backdrop_, panel_;
    std::array<graphics::Image, 4> plates_;
};

} // namespace rich4::native::screens
