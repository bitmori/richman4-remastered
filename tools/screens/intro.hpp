// SPDX-License-Identifier: GPL-3.0-or-later
// The original new-game intro: the players jump out of an airplane and parachute down
// (fcn_00415872, rich4.asm:7166, called from rich4_main.asm:376 right after _rich4_load_map;
// docs/ui/effects.md §7). It only runs for a NEW game; loading a save skips it
// (rich4_main.asm loc_00401d08).
//
// Original sequence, full screen 640x480, before the first turn:
//   0. _rich4_show_loading_scene: raw picture Data.mkf 601 ("Loading") while the map loads.
//   1. AIRPLANE.AVI / FLY<region>.AVI (fcn_00451677). The shipped AVIs are 5 KB stubs, so the
//      native port has nothing to play here.
//   2. Jump.mkf 45 sprite 0 (airplane side, door open) as the backdrop; Effect.mkf 25 starts;
//      FLC Jump 46 (door opening, 220x240) overlays it at (180,60), flags 3.
//   3. For each player in turn order: the NEXT player waits in the doorway (Jump 45 sprite
//      3+character darkened by brightness level -20, at (270,220), behind the door-frame
//      sprite 2 at (150,60)) while this player's FLC Jump 47+character (640x480, flags
//      0x14000003) plays: overlay, skippable, Effect.mkf 26 at frame 20.
//   4. Jump 45 sprite 1 (sky) as the backdrop; for each player FLC Jump 59+character
//      (parachute, flags 3).
// Every FLC is skippable by mouse-up or key-up (flags bit 1), and a skip ends the whole intro.
// No rule state is touched: this is presentation only. No SDL here: the caller supplies time.
#pragma once

#include "../native_frame.hpp"
#include "rich4/flc.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace rich4::native {

class IntroPlayer {
  public:
    explicit IntroPlayer(Artwork &artwork) : artwork_(artwork) {}

    // characters: the players' character ids (0..11) in turn order.
    void start(const std::vector<std::uint8_t> &characters);
    [[nodiscard]] bool active() const noexcept { return index_ < shots_.size(); }
    void skip() noexcept;

    using SoundFunction = std::function<void(std::size_t)>;
    // Advances by elapsed_ms (clamped) and paints the whole 640x480 canvas. No-op when idle.
    void draw(graphics::Image &canvas, unsigned elapsed_ms, const SoundFunction &play_sound);

  private:
    struct Shot {
        std::shared_ptr<const graphics::Image> still; // what the screen shows under the movie
        int movie = -1;                               // Jump.mkf FLC entry, -1 = none
        int x = 0, y = 0;                             // FLC top-left on screen
        int start_sound = -1;                         // Effect.mkf index played when the shot starts
        int frame_sound = -1, sound_frame = 0;        // Effect.mkf index at the start of a frame
        unsigned hold_ms = 0;                         // no movie: how long the still is shown
    };
    void begin_shot();

    Artwork &artwork_;
    std::vector<Shot> shots_;
    std::size_t index_ = 0;
    std::unique_ptr<flc::Decoder> movie_;
    graphics::Image frame_;
    unsigned elapsed_ = 0, delay_ = 100;
    bool started_ = false, sounded_ = false;
};

} // namespace rich4::native
