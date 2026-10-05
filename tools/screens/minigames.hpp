// SPDX-License-Identifier: GPL-3.0-or-later
// The original interactive mini-games 企鵝挖寶 / 七彩氣球 / 喜從天降 (docs/ui/mini-games.md).
// Rules and randomness live in the portable core (rich4/minigames.hpp); this file paints what the
// original drew each tick (Panel 78..111, Data 526), plays its sounds and maps touch to the
// original mouse input. A human mini-game is a pending DecisionKind::mini_game: when the game's
// window closes, the app calls GameSession::submit_mini_game(score, draws).
#pragma once

#include "../native_frame.hpp"
#include "rich4/flc.hpp"
#include "rich4/game.hpp"
#include "rich4/minigames.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace rich4::native::minigames {

namespace core = rich4::minigames;

inline constexpr int tap_button = 902; // Full-screen tap target while a mini-game runs.

struct Sound {
    std::function<void(std::size_t)> play; // Effect.mkf one-shot
    std::function<void(std::size_t)> loop; // Effect.mkf loop (replaces the previous loop)
    std::function<void()> stop_loop;
};

struct Result {
    std::int32_t score = 0;
    std::uint32_t draws = 0;
};

[[nodiscard]] bool handles(const GameDecision &decision);

class MiniGames {
  public:
    explicit MiniGames(Sound sound);
    ~MiniGames();
    MiniGames(const MiniGames &) = delete;
    MiniGames &operator=(const MiniGames &) = delete;

    // Human mini-game decision: plays it full screen. Returns the result once the game's window
    // has closed (the caller submits it); std::nullopt while it is still running.
    std::optional<Result> draw_decision(Frame &frame, const GameDecision &decision);

    // Dev (--minigame): start a standalone game. autoplay: a simple bot plays (headless capture);
    // capture_directory: write start / READY / mid-play / result frames there.
    void start(core::Game game, std::uint32_t seed, std::uint8_t character, Frame &frame, bool autoplay = false,
               std::filesystem::path capture_directory = {});
    // Dev: advance and draw the standalone game; returns its result once it has finished.
    std::optional<Result> draw(Frame &frame);
    // Fixed frame step for headless runs (0 = wall clock from Frame::ticks).
    void set_fixed_step(unsigned ms) noexcept {
        fixed_step_ = ms;
    }

    [[nodiscard]] bool active() const noexcept {
        return playing_ != nullptr;
    }
    void clear();

  private:
    struct Playing;
    void begin(core::Game game, std::uint32_t seed, std::uint8_t character, Frame &frame);
    std::optional<Result> run(Frame &frame);
    void paint(Frame &frame);
    void input(Frame &frame);
    void autoplay(Frame &frame);
    void capture(Frame &frame, const std::string &tag);

    Sound sound_;
    std::unique_ptr<Playing> playing_;
    unsigned fixed_step_ = 0;
};

} // namespace rich4::native::minigames
