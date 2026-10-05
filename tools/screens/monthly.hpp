// SPDX-License-Identifier: GPL-3.0-or-later
// The original monthly-cycle screens (docs/ui/finance-menus.md):
//  - §4 "Monthly dividend screen": 上市公司分紅 table, Panel 76 (fcn_0042ba97), every 15th.
//  - §7.2 "Monthly draw": the 樂透開獎 show, Panel 15..17 (_rich4_ui_letou_kaijiang_entry), every
//    15th when at least one ticket is sold, right after the dividend table.
//  - §14 "Month-end report": the bank interest / ranking report, Panel 25 and the champion's Data
//    416+2c reaction (fcn_00439bfa / fcn_00437e61), at every month change.
// The rules live in the core: each screen arrives as an EffectKind::dividend / lottery_draw /
// month_report effect whose `monthly` figures the rule has already applied. This file only draws
// them full screen, plays the original sounds/voices and maps taps. No SDL: the caller supplies time.
#pragma once

#include "../native_frame.hpp"
#include "rich4/flc.hpp"
#include "rich4/game.hpp"
#include "rich4/world.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rich4::native::monthly {

inline constexpr int continue_button = 911; // full-screen tap: next step / close

struct Sound {
    std::function<void(std::size_t)> play;            // Effect.mkf one-shot
    std::function<std::uint32_t(std::size_t)> speech; // Speaking.mkf clip; returns its length in ms
};

[[nodiscard]] bool handles(const EffectEvent &effect);

class MonthlyScreens {
  public:
    explicit MonthlyScreens(Sound sound) : sound_(std::move(sound)) {}

    // Headless replay: every screen is shown for exactly one frame, then closes by itself.
    void set_skip(bool skip) noexcept {
        skip_ = skip;
    }
    void enqueue(EffectEvent effect);
    [[nodiscard]] bool active() const noexcept {
        return current_.has_value() || !queue_.empty();
    }
    void clear();
    // Advances the current screen by elapsed_ms and paints the whole 640x480 canvas; registers
    // continue_button over the screen while a tap can advance it. No-op when idle.
    void draw(Frame &frame, const World &world, unsigned elapsed_ms);
    // Short name of the current screen for screenshots ("dividend", "lottery", "report").
    [[nodiscard]] const char *shot() const noexcept;

    // Dev gallery (--monthly-gallery): plays sample screens for `world`'s players (a dividend
    // table, a won and a lost draw, a month report with a 悲情人物 and a champion) in 50 ms steps,
    // calling `background` before and `write(name)` after every sampled frame.
    void gallery(Frame &frame, const World &world, const std::function<void()> &background,
                 const std::function<void(const std::string &)> &write);

  private:
    // The generic speech bubble of fcn_0044ec30 / fcn_0044ecb6 / fcn_0044ee18 (rich4.asm:23272..):
    // a frame at (x,y) with text (font 20, bold, align 4) centred on it + (dx,dy); it stays up for
    // at least 2000 ms and until its voice ends.
    struct Bubble {
        std::size_t frame = 0; // Panel frame of the bubble (15:22.. in the draw, 25:1.. in the report)
        int x = 0, y = 0, dx = 0, dy = 0;
        std::string text;
        unsigned remaining_ms = 0;
        bool up = false;
    };
    // An FLC playing over the scene (Panel 16/17 in the draw, Data 416+2c/417+2c in the report).
    struct Movie {
        std::unique_ptr<flc::Decoder> decoder;
        graphics::Image still; // last decoded frame; stays on screen after the movie
        int x = 0, y = 0;
        unsigned plays = 1, elapsed = 0;
        bool running = false, overlay = true;
    };
    struct Playing {
        EffectEvent event;
        MonthlyFigures figures;
        int state = 0;          // the original window proc's state byte
        int counter = 0;        // its tick counter (c425 / cnt)
        unsigned tick_ms = 100; // timer period
        unsigned elapsed = 0;   // ms not yet consumed by ticks
        bool tapped = false;    // a tap since the last frame
        bool skip_wait = false; // report c42e: a click also skips the next wait
        bool shown = false;     // at least one frame painted (headless replays close after it)
        Bubble bubble;
        Movie movie, second;    // lottery: cage + winner; report: reaction (movie only)
        // Scene (redrawn every frame from these instead of restoring back-buffer rectangles).
        int layout = 0;          // screen-specific arrangement
        int lady_mode = 0;       // report: Panel 25 lady pose (c42d), -1 = not drawn
        int spotlight = -1, hidden = -1, award = 0;
        bool cards = false, lineup = false, happy = false, lined = false;
        bool balls = false, green = false, plate = false;
        int eye_patch = -1, mouth_patch = -1, left_patch = -1; // face animation frames
        int blink = -1, blink_step = 0, mouth_hold = 0;
        std::uint32_t seed = 1; // cosmetic rand() stand-in (never the gameplay stream)
        bool closing = false;
    };
    void start(Playing &playing);
    bool draw_dividend(Frame &frame, const World &world, Playing &playing, unsigned elapsed_ms);
    bool draw_lottery(Frame &frame, Playing &playing, unsigned elapsed_ms);
    bool draw_report(Frame &frame, Playing &playing, unsigned elapsed_ms);
    void say(Playing &playing, std::size_t entry, int x, int y, int dx, int dy, std::size_t voice,
             std::string_view text);
    void lottery_tick(Frame &frame, Playing &playing);
    void report_tick(Frame &frame, Playing &playing);

    Sound sound_;
    bool skip_ = false;
    std::deque<EffectEvent> queue_;
    std::optional<Playing> current_;
};

} // namespace rich4::native::monthly
