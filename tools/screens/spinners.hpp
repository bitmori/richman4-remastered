// SPDX-License-Identifier: GPL-3.0-or-later
// Original number-roll UIs (docs/ui/messages.md §2.3 "Spinners"):
//  - fcn_00440706 / fcn_0043f23e: the 大富翁 slot machine (Panel.mkf 67) used by 小財神, 大財神,
//    小窮神 and 大窮神 (god kinds 1/2/5/6).
//  - fcn_0044090e / fcn_0043f7c6: the cupid wheels (Panel.mkf 68..71) used by travel agencies,
//    hotels, department stores and insurance companies.
// The rules live in the core: a human spinner is a pending GameDecision whose `spinner` mirrors
// the core's live state (GameSession::tick advances it); computer/sleep-walking spins and the
// wheel's slow-down after STOP arrive as EffectKind::money_spinner / roulette effects whose result
// is already applied. This file only draws that state, plays the original sounds and maps taps.
#pragma once

#include "../native_frame.hpp"
#include "rich4/game.hpp"
#include "rich4/gods.hpp"
#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>

namespace rich4::native::spinners {

inline constexpr int skip_button = 901; // Full-screen tap ending a human-stopped wheel's result hold.

struct Sound {
    std::function<void(std::size_t)> play; // Effect.mkf one-shot
    std::function<void(std::size_t)> loop; // Effect.mkf loop (replaces the previous loop)
    std::function<void()> stop_loop;
};

// Decisions/effects this module draws (instead of the generic dialog / effect player).
[[nodiscard]] bool handles(const GameDecision &decision);
[[nodiscard]] bool handles(const EffectEvent &effect);

class Spinners {
  public:
    explicit Spinners(Sound sound) : sound_(std::move(sound)) {}

    // Headless replay: spinner effects are dropped on enqueue (zero duration), like EffectPlayer.
    void set_skip(bool skip) noexcept {
        skip_ = skip;
    }

    // Human spinner decision: draws the live state and registers `stop_id` over the whole screen
    // (the original stops on any mouse/key release; on a phone any tap is the STOP target).
    void draw_decision(Frame &frame, const World &world, const GameDecision &decision, int stop_id,
                       const std::function<void()> &stop);

    void enqueue(EffectEvent effect);
    [[nodiscard]] bool active() const noexcept {
        return current_.has_value() || !queue_.empty();
    }
    void clear();
    // Advances and draws the current spinner effect; registers `skip_id` while the human-stopped
    // wheel holds its result (fcn_0043f7c6 state 5 accepts a click).
    void draw_effect(Frame &frame, const World &world, unsigned elapsed_ms, int skip_id);

    // Call once per application frame after drawing: starts/stops the looping rattle so it only
    // plays while a reel or wheel is visibly spinning.
    void finish_frame();

  private:
    struct Playing {
        EffectEvent event;
        SpinnerState state;
        unsigned elapsed = 0;  // ms not yet consumed by frames
        unsigned frames = 0;   // frames since the current wheel state began (original ebp)
        unsigned wheel_state = 1, period = 1, count = 3, last = 0;
        std::optional<gods::MoneySpinner> machine;
        Random random{0};
        bool skip = false;
    };
    bool step_wheel(Playing &playing);

    Sound sound_;
    bool skip_ = false;
    std::deque<EffectEvent> queue_;
    std::optional<Playing> current_;
    std::size_t wanted_loop_ = 0, playing_loop_ = 0; // 0 = none
    unsigned decision_phase_ = 0;                    // last drawn decision phase (stop click sound)
};

} // namespace rich4::native::spinners
