// SPDX-License-Identifier: GPL-3.0-or-later
// Original on-board animations (docs/ui/effects.md): missiles, explosions, god appearances,
// demolition, throws, ... replayed from the session's EffectEvent queue. The dice roll is drawn
// by the main screen itself, so dice_roll effects are ignored here.
//
// Model (§9 of the spec): the session has ALREADY applied the rule when an effect is drained.
// The player keeps a scene snapshot of the board from the last idle frame and shows it until
// the movie's board-commit frame, then the live world. While anything is queued or playing,
// the app must not tick the session (the original blocks its game loop) and must ignore board
// input. No SDL here: time and sound are supplied by the caller, so tools can render frames.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/flc.hpp"
#include "rich4/game.hpp"
#include "rich4/world.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace rich4::native {

// Owned copy of the sprite lists that board::Renderer draws (a board::Scene view of them).
struct SceneData {
    std::vector<board::PlayerSprite> players;
    std::vector<board::ObjectSprite> objects;
    std::vector<board::PropertyState> lands, facilities, commercials;
    std::vector<board::SpecialPlayerSprite> special_players;
    [[nodiscard]] board::Scene view() const {
        return {players, objects, lands, facilities, special_players, commercials};
    }
};
// Same sprite construction as the main board (no walking interpolation).
SceneData make_scene(const World &world, std::uint64_t ticks);

// Per-kind playback table (values from docs/ui/effects.md §1-§6).
struct EffectSpec {
    char archive = 'd';        // 'd' Data.mkf, 'p' Panel.mkf
    int entry = -1;            // FLC entry; -1 = no movie (throw)
    int commit_frame = 0;      // board shows the pre-mutation snapshot before this frame
    int sound = -1;            // Effect.mkf index, -1 none
    int sound_frame = 0;       // frame whose start plays `sound`
    unsigned hold_ms = 0;      // after the last frame (fcn_0045285e)
    bool camera_on_target = true; // false: camera stays on the actor (demon puff, throws)
};
EffectSpec effect_spec(const EffectEvent &effect);

class EffectPlayer {
  public:
    explicit EffectPlayer(Artwork &artwork) : artwork_(artwork) {}

    // Headless/replay mode: effects are dropped on enqueue (zero duration).
    void set_skip(bool skip) noexcept { skip_ = skip; }

    // Call once per frame before any session mutation can happen (start of draw). Only
    // records while idle; `owner` identifies the session so a new game never reuses it.
    void remember(const World &world, const void *owner, std::uint64_t ticks);
    void enqueue(std::vector<EffectEvent> effects, const void *owner);
    [[nodiscard]] bool active() const noexcept { return current_.has_value() || !queue_.empty(); }
    void clear();

    using SoundFunction = std::function<void(std::size_t)>;
    // Advances by elapsed_ms (clamped) and paints the board viewport [0,40,440,480) of canvas:
    // the board (snapshot or live, camera per effect) plus the movie/sprites. No-op when idle.
    void draw(graphics::Image &canvas, board::Renderer &board, const World &world, std::uint8_t rotation,
              unsigned elapsed_ms, std::uint64_t ticks, const SoundFunction &play_sound);

  private:
    struct Playing {
        EffectEvent event;
        EffectSpec spec;
        std::unique_ptr<flc::Decoder> movie;
        graphics::Image frame;  // last decoded movie frame
        unsigned elapsed = 0;   // ms since start
        unsigned delay = 100;   // ms per frame
        bool sounded = false;
        std::vector<board::Point> path; // throws: screen points
    };
    void start(EffectEvent event);
    std::vector<std::uint8_t> &movie_bytes(char archive, int entry);

    Artwork &artwork_;
    bool skip_ = false;
    std::deque<EffectEvent> queue_;
    std::optional<Playing> current_;
    std::optional<SceneData> snapshot_;
    const void *snapshot_owner_ = nullptr;
    std::map<std::pair<char, int>, std::vector<std::uint8_t>> movies_;
};

} // namespace rich4::native
