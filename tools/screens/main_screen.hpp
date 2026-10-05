// SPDX-License-Identifier: GPL-3.0-or-later
// Original in-game screen chrome (docs/ui/main-screen.md): icon toolbar, 4-tab info panel,
// minimap with rotate buttons, season/calendar panel, floating GO button and the FLC dice roll.
// The Application owns the board render and dispatches the registered hit areas by id.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/game.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace rich4::native {

// Hit ids. The replay tests click the pre-existing ids, so they keep their actions.
namespace main_ids {
inline constexpr int save = 71, load = 72, rotate_left = 73, rotate_right = 74, query = 75, options = 76,
                     cards = 77, tools = 78, stocks = 79, trading = 80, help = 81, ai = 82, full_map = 83,
                     full_map_close = 84, tab_first = 85, sun = 89, moon = 90, minimap = 91, roll = 100,
                     dice_count = 101, go_frame = 102;
}

struct MainScreenActions {
    std::function<void()> help, options, ai, load, save, query, tools, cards, trading, stocks;
    std::function<void(unsigned)> rotate;     // +7 turns left, +1 turns right
    std::function<void()> roll;               // GO
    std::function<void(unsigned)> dice_count; // requested count 1..3
    std::function<void(unsigned)> sound;      // original sound effect id
    bool can_save = false, can_load = false;
};

class MainScreen {
  public:
    MainScreen() = default;
    // Development preset "view=N,tab=N,calendar=N,map=1" for screenshots of every state.
    explicit MainScreen(std::string_view preset);
    // Board camera centre: the minimap free-scroll centre when detached, else `follow`.
    [[nodiscard]] board::Point camera_center(board::Point follow) const {
        return free_scroll_.value_or(follow);
    }
    void follow_player() {
        free_scroll_.reset();
    }
    // Starts the dice animation on a roll and re-centres the view when a turn or roll starts.
    void on_event(const GameEvent &event, const GameSession &session, Artwork &artwork);
    // The original roll animation is synchronous: while busy the application holds the game clock.
    [[nodiscard]] bool dice_busy() const;
    void advance(unsigned elapsed_ms);
    void cycle_view_mode() {
        view_mode_ = (view_mode_ + 1) % 3;
    }
    [[nodiscard]] unsigned view_mode() const {
        return view_mode_;
    }
    // Raw pointer input beyond the click dispatch: drags (GO button, minimap) and button releases.
    void pointer_motion(int x, int y, bool left_down);
    void pointer_up(int x, int y, bool right_button);

    // Dice and GO button over the board, then toolbar and right-hand panels.
    void draw(Frame &frame, const GameSession &session, unsigned rotation, const MainScreenActions &actions);

  private:
    void draw_toolbar(Frame &frame, const GameSession &session, const MainScreenActions &actions);
    void draw_info_panel(Frame &frame, const World &world, const MainScreenActions &actions);
    void draw_header(Frame &frame, const World &world);
    void draw_minimap(Frame &frame, const World &world, int top, const MainScreenActions &actions);
    void draw_calendar(Frame &frame, const World &world, const MainScreenActions &actions);
    void draw_go(Frame &frame, const GameSession &session, const MainScreenActions &actions);
    void draw_dice(Frame &frame, const GameSession &session, unsigned rotation);
    void draw_full_map(Frame &frame, const World &world);
    std::uint8_t go_mask(Frame &frame, int x, int y);

    unsigned view_mode_ = 1, calendar_mode_ = 0;
    std::array<std::uint8_t, 9> tab_{};
    int hover_ = -1;
    int go_x_ = 180, go_y_ = 120;
    std::optional<std::pair<int, int>> go_drag_; // pointer offset inside the GO button
    // Touch: a tap anywhere on GO rolls (the original's 35x41 roll hotspot is too small for a
    // finger); it becomes a drag only once the pointer moves. Set on press, fired on release.
    std::function<void()> pending_roll_;
    std::pair<int, int> press_point_{};
    std::optional<board::Point> free_scroll_;
    bool minimap_drag_ = false, full_map_ = false;
    int minimap_top_ = 280;
    std::vector<std::uint8_t> go_mask_;
    // Dice roll animation (Panel 4-6 FLC, then Panel 3 faces).
    std::map<std::size_t, std::vector<graphics::Image>> dice_animations_;
    std::vector<std::uint8_t> dice_faces_;
    std::uint8_t dice_direction_ = 0;
    bool dice_active_ = false;
    unsigned dice_elapsed_ = 0, dice_frames_ = 0;
};

} // namespace rich4::native
