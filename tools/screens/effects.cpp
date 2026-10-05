// SPDX-License-Identifier: GPL-3.0-or-later
// See effects.hpp. Asset/timing/sound values cite docs/ui/effects.md and the upstream asm.
#include "effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace rich4::native {

SceneData make_scene(const World &world, std::uint64_t ticks) {
    SceneData scene;
    const auto frame = static_cast<std::uint8_t>((ticks / 120) % 6);
    const auto active_slot = world.header.current_player;
    for (const auto &player : world.players) {
        const auto &state = player.state;
        if (!state.controller_flags || (!state.x && !state.y))
            continue;
        scene.players.push_back(
            {{state.x, state.y}, state.character, state.direction, frame,
             static_cast<std::uint8_t>(state.traffic_method & 3U), static_cast<std::uint16_t>(state.color),
             state.slot, state.slot == active_slot, false,
             state.node_id < world.nodes.size() && (world.nodes[state.node_id].flags & 0x80000000U) != 0,
             player.status.days_sleep_walking != 0, (state.controller_flags & 0x40U) != 0});
    }
    for (std::size_t index = 1; index < world.lands.size(); ++index) {
        const auto &land = world.lands[index];
        scene.lands.push_back({land.id, land.economy.owner_code, land.economy.level, land.economy.chain_store});
    }
    for (std::size_t index = 1; index < world.facilities.size(); ++index) {
        const auto &facility = world.facilities[index];
        scene.facilities.push_back({facility.id, facility.owner_code, facility.level, facility.facility_type});
    }
    for (const auto &object : world.objects) {
        std::optional<board::Point> position;
        if (object.attached_player) {
            for (const auto &player : scene.players)
                if (player.slot + 1 == object.attached_player)
                    position = player.position;
        } else if (object.node_id && object.node_id < world.nodes.size()) {
            const auto &node = world.nodes[object.node_id];
            position = board::Point{node.x, node.y};
        }
        if (position)
            scene.objects.push_back({*position, object.kind, object.direction, frame});
    }
    for (std::size_t index = 0; index < world.special_actors.size(); ++index) {
        const auto &actor = world.special_actors[index];
        if (actor.inactive_state || !actor.node_id || actor.node_id >= world.nodes.size())
            continue;
        scene.special_players.push_back({{actor.x, actor.y}, static_cast<std::uint8_t>(index + 4), actor.direction,
                                         frame, false, false, (world.nodes[actor.node_id].flags & 0x80000000U) != 0,
                                         actor.days_sleep_walking != 0});
    }
    for (const auto &company : world.commercials)
        if (company.id)
            scene.commercials.push_back({company.id, company.owner_code, 0, 0});
    return scene;
}

EffectSpec effect_spec(const EffectEvent &effect) {
    switch (effect.kind) {
    case EffectKind::dice_roll: // §1: drawn by the main screen's own dice animation, not here
        return {'p', -1, 0, -1, 0, 0, false};
    case EffectKind::top_floor_sparkle: // §2 fcn_0040b0cd
        return {'d', 523, 0, 90, 0, 0, true};
    case EffectKind::build_rise: // §2 robot worker, flags 0x2C0001
        return {'d', 553, 44, 91, 0, 0, true};
    case EffectKind::object_throw: // §0 _rich4_animate_object, hold 100; sound after landing
        return {'d', -1, 0, effect.object_kind == 16 ? 33 : effect.object_kind == 17 ? 34 : 10, 0, 100, false};
    case EffectKind::missile: // §4 flags 0x90001
        return {'d', 528, 9, 81, 0, 0, true};
    case EffectKind::nuclear_missile: // §4 flags 0x80090001 (whiten mode not reproduced)
        return {'d', 530, 9, 83, 0, 0, true};
    case EffectKind::explosion: // §4 mine / time bomb, flags 0x30001
        return {'d', 525, 3, 82, 0, 0, true};
    case EffectKind::demolish: // §5 flags 0x260001, then 500 ms
        return {'d', 529, 38, 97, 0, 500, true};
    case EffectKind::monster: // §5 flags 0x100001, then 500 ms
        return {'d', 557, 16, 80, 0, 500, true};
    case EffectKind::god_appear: { // §6 _rich4_attach_god jump table, flags 1
        struct God {
            std::uint8_t kind;
            int entry, sound;
        };
        static constexpr std::array<God, 12> gods{{{1, 540, 102}, {2, 541, 103}, {3, 542, 104}, {4, 543, 105},
                                                   {5, 544, 106}, {6, 545, 107}, {7, 546, 108}, {8, 547, 109},
                                                   {9, 548, 110}, {10, 549, 112}, {12, 550, 111}, {15, 551, 113}}};
        for (const auto &god : gods)
            if (god.kind == effect.object_kind)
                return {'d', god.entry, 0, god.sound, 0, 0, true};
        return {'d', -1, 0, -1, 0, 0, true}; // 11/13/14: no appearance movie
    }
    case EffectKind::demon_puff: // §6 Data 526 at building anchor - (55,55), flags 0x30001
        return {'d', 526, 3, 95, 0, 0, false};
    case EffectKind::bankrupt: // §6 rich4_player_bankrupt.asm, flags 1
        return {'d', 555, 0, 100, 0, 0, true};
    case EffectKind::money_spinner: // drawn by screens/spinners.cpp, never queued here
    case EffectKind::roulette:
    case EffectKind::dividend: // MONTHLY: full-screen, drawn by screens/monthly.cpp
    case EffectKind::lottery_draw:
    case EffectKind::month_report:
        return {'p', -1, 0, -1, 0, 0, false};
    }
    return {};
}

void EffectPlayer::remember(const World &world, const void *owner, std::uint64_t ticks) {
    if (active())
        return;
    snapshot_ = make_scene(world, ticks);
    snapshot_owner_ = owner;
}

void EffectPlayer::enqueue(std::vector<EffectEvent> effects, const void *owner) {
    if (effects.empty())
        return;
    if (owner != snapshot_owner_) { // A different session: nothing cached belongs to it.
        clear();
        snapshot_owner_ = owner;
    }
    if (skip_)
        return;
    for (auto &effect : effects)
        if (effect.kind == EffectKind::object_throw || effect_spec(effect).entry >= 0)
            queue_.push_back(std::move(effect));
}

void EffectPlayer::clear() {
    queue_.clear();
    current_.reset();
    snapshot_.reset();
}

std::vector<std::uint8_t> &EffectPlayer::movie_bytes(char archive, int entry) {
    auto &bytes = movies_[{archive, entry}];
    if (bytes.empty())
        bytes = artwork_.archive(archive).decode(static_cast<std::size_t>(entry));
    return bytes;
}

void EffectPlayer::start(EffectEvent event) {
    Playing playing{std::move(event), {}, nullptr, {}, 0, 100, false, {}};
    playing.spec = effect_spec(playing.event);
    if (playing.spec.entry >= 0) {
        playing.movie = std::make_unique<flc::Decoder>(movie_bytes(playing.spec.archive, playing.spec.entry));
        playing.delay = playing.movie->delay_ms() ? playing.movie->delay_ms() : 100;
    }
    current_ = std::move(playing);
}

void EffectPlayer::draw(graphics::Image &canvas, board::Renderer &board, const World &world, std::uint8_t rotation,
                        unsigned elapsed_ms, std::uint64_t ticks, const SoundFunction &play_sound) {
    try {
        if (!current_) {
            if (queue_.empty())
                return;
            auto next = std::move(queue_.front());
            queue_.pop_front();
            start(std::move(next));
            elapsed_ms = 0; // the first frame is shown in full
        }
        auto &play = *current_;
        play.elapsed += std::min(elapsed_ms, 100U);
        const auto &event = play.event;
        const auto sound = [&](int index) {
            if (index >= 0 && play_sound)
                play_sound(static_cast<std::size_t>(index));
        };
        // Camera (fcn_0041d476(x,y,0)): the target for targeted movies, else the actor.
        board::Point actor{event.target.x, event.target.y};
        if (event.from)
            actor = {event.from->x, event.from->y};
        if (event.kind == EffectKind::demon_puff && event.player < world.players.size())
            actor = {world.players[event.player].state.x, world.players[event.player].state.y};
        const board::Camera camera{play.spec.camera_on_target ? board::Point{event.target.x, event.target.y} : actor,
                                   static_cast<std::uint8_t>(rotation & 7U)};
        const graphics::Rect viewport{0, 40, 440, 480};

        // ---- timeline ----
        int frame_index = 0;
        bool finished = false, committed = true;
        if (play.movie) {
            const auto count = static_cast<int>(std::max<std::size_t>(1, play.movie->frame_count()));
            frame_index = std::min(static_cast<int>(play.elapsed / play.delay), count - 1);
            finished = play.elapsed >= static_cast<unsigned>(count) * play.delay + play.spec.hold_ms;
            while (static_cast<int>(play.movie->decoded_frames()) <= frame_index && play.movie->next())
                play.frame = play.movie->image();
            if (!play.sounded && frame_index >= play.spec.sound_frame) {
                sound(play.spec.sound);
                play.sounded = true;
            }
            committed = frame_index >= play.spec.commit_frame;
        } else { // object throw: one step per 8 screen px, >= 24 ms per step
            if (play.path.empty()) {
                const auto from = board::project(actor, camera);
                const auto to = board::project({event.target.x, event.target.y}, camera);
                if (from && to) {
                    const double dx = to->x - from->x, dy = to->y - from->y;
                    const int steps = static_cast<int>(std::sqrt(dx * dx + dy * dy) * 0.125 + 1);
                    for (int step = 1; step <= steps; ++step)
                        play.path.push_back({from->x + static_cast<int>(dx * step / steps),
                                             from->y + static_cast<int>(dy * step / steps)});
                } else
                    play.path.push_back({-1000, -1000}); // off screen: nothing to show
            }
            const auto steps = static_cast<unsigned>(play.path.size());
            frame_index = static_cast<int>(std::min(play.elapsed / 24U, steps - 1));
            if (!play.sounded && play.elapsed >= steps * 24U) {
                sound(play.spec.sound);
                play.sounded = true;
            }
            finished = play.elapsed >= steps * 24U + play.spec.hold_ms;
            committed = false;
        }
        if (finished) {
            current_.reset();
            return; // draw_board already painted the live world this frame
        }

        // ---- board: pre-mutation snapshot until the commit frame, then the live world ----
        if (committed || !snapshot_) {
            if (committed)
                snapshot_.reset(); // later effects of the same batch use the live world (spec §9.4)
            const auto live = make_scene(world, ticks);
            board.render(canvas, live.view(), camera);
        } else
            board.render(canvas, snapshot_->view(), camera);

        // ---- picture ----
        if (!play.movie) {
            const auto &point = play.path[static_cast<std::size_t>(frame_index)];
            std::uint8_t direction = 0;
            for (const auto &object : world.objects)
                if (object.node_id == event.node && object.kind == event.object_kind)
                    direction = object.direction;
            const auto &sprite = artwork_.frame('d', 0x18c + event.object_kind - 1U,
                                                static_cast<std::size_t>((8 - rotation + direction) & 7U));
            graphics::blit(canvas, sprite, point.x, point.y, viewport);
            return;
        }
        int x = 0, y = 40; // 440x440 movies cover the viewport exactly (= project(target) - (220,220))
        if (event.kind == EffectKind::demon_puff) {
            // Building anchor approximated by the projected plot point (spec §9.7, UNVERIFIED).
            const auto anchor = board::project({event.target.x, event.target.y}, camera);
            if (!anchor)
                return;
            x = anchor->x - play.movie->width() / 2;
            y = anchor->y - play.movie->height() / 2;
        }
        graphics::blit(canvas, play.frame, x, y, viewport);
    } catch (const std::exception &error) { // A broken asset must never stop the game.
        std::cerr << "Effect skipped: " << error.what() << '\n';
        current_.reset();
    }
}

} // namespace rich4::native
