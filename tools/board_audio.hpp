// SPDX-License-Identifier: GPL-3.0-or-later
// AUDIO: which original loops and context music belong to what the app is showing.
// Presentation only: reads the session/world, never changes a rule or draws randomness.
//  - walk_loop_clip: the walk/engine loop of a moving slot (rich4_player_utils.asm fcn_0040dd1f
//    and _rich4_update_player_sprite): board sound preset 0x48234a slot 0xb + (traffic & 3), slot
//    0xf on water, special actors 0xb/0xf, the robot doll (slot 8) preset [9]; a time-bomb carrier
//    also loops preset 0x482362 (Effect 32) while it walks.
//  - MusicDirector: keeps Audio's context stack equal to the wanted list of contexts (outermost
//    first) with push/pop/replace, exactly one pop per push.
#pragma once

#include "native_audio.hpp"
#include "rich4/calendar.hpp"
#include "rich4/game.hpp"
#include "rich4/holidays.hpp"
#include "rich4/world.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rich4::native {

// Effect.mkf loop for the slot whose step is being animated (0..3 players, 4..7 special actors,
// 8 robot doll), or nullopt.
inline std::optional<std::size_t> walk_loop_clip(const World &world, std::size_t slot) {
    const auto on_water = [&world](std::uint16_t node) {
        return node < world.nodes.size() && (world.nodes[node].flags & 0x80000000U) != 0;
    };
    if (slot < 4) {
        if (slot >= world.players.size())
            return std::nullopt;
        const auto &state = world.players[slot].state;
        if (on_water(state.node_id))
            return 47;
        constexpr std::array<std::size_t, 4> by_traffic{44, 45, 46, 53}; // foot, motorbike, car, 工程車
        return by_traffic[state.traffic_method & 3U];
    }
    if (slot == 8)
        return 38;
    if (slot - 4 < world.special_actors.size())
        return on_water(world.special_actors[slot - 4].node_id) ? 47 : 44;
    return std::nullopt;
}
// Time-bomb tick while a carrier walks (player_utils.asm:664/746/1058, players only).
inline std::optional<std::size_t> bomb_loop_clip(const World &world, std::size_t slot) {
    if (slot < 4 && slot < world.players.size() && world.players[slot].status.attached_bomb)
        return 32;
    return std::nullopt;
}

// Context music of a decision screen (rich4_media_music.asm callers).
inline std::optional<audio::Music> decision_music(const GameDecision &decision) {
    switch (decision.kind) {
    case DecisionKind::bank:
        return audio::Music::bank; // _rich4_ui_bank_entry ui_bank.asm:3557
    case DecisionKind::auction:
        return audio::Music::auction; // _rich4_ui_auction_entry ui_auction.asm:3139
    case DecisionKind::shop:
    case DecisionKind::lottery:
        return audio::Music::shop; // shop.asm:2196, ui_letou.asm:2938 (bar)
    case DecisionKind::magic_category:
    case DecisionKind::magic_effect:
        return audio::Music::magic_house; // magic_house.asm:2269
    case DecisionKind::rescue: // ui_prison.asm:917 / ui_hospital.asm:1529
        return decision.prompt.find("住院") != std::string::npos ? audio::Music::hospital : audio::Music::prison;
    default:
        return std::nullopt;
    }
}

// Holiday music rows of table 0x47ff4c (flag 4, fcn_00452444): Christmas 12/25 on the stage-0
// maps (MIDI14-1, one day) and Lunar New Year on Taiwan/China (MIDI14-2, held three days).
inline std::optional<audio::Music> holiday_music(const World &world) {
    const auto &header = world.header;
    if (header.stage_id != 0)
        return std::nullopt;
    const calendar::Date date{header.date.year, header.date.month, header.date.day};
    if (date.month == 12 && date.day == 25)
        return audio::Music::christmas;
    if (header.map_id <= 1) {
        const auto lunar = holidays::lunar_date(date);
        if (lunar && lunar->month == 1 && lunar->day >= 1 && lunar->day <= 3)
            return audio::Music::new_year;
    }
    return std::nullopt;
}

class MusicDirector {
  public:
    // A screen closing pops only after this long, so consecutive screens of one visit (a bank
    // answer followed by the next bank decision) keep the music running.
    static constexpr std::uint64_t pop_hold_ms = 300;

    // `wanted`: contexts outermost first ({} = board playlist). Call once per frame.
    void update(audio::Audio &audio, const std::vector<audio::Music> &wanted, std::uint64_t now_ms) {
        if (wanted == pushed_) {
            pop_pending_ = false;
            return;
        }
        std::size_t common = 0;
        while (common < wanted.size() && common < pushed_.size() && wanted[common] == pushed_[common])
            ++common;
        if (common == wanted.size()) { // only pops: hold briefly
            if (!pop_pending_) {
                pop_pending_ = true;
                pop_since_ = now_ms;
            }
            if (now_ms - pop_since_ < pop_hold_ms)
                return;
        }
        pop_pending_ = false;
        if (pushed_.size() == wanted.size() && common + 1 == wanted.size()) {
            audio.replace_music(wanted.back()); // e.g. menu -> setup (the original's 0x8001)
            pushed_.back() = wanted.back();
            return;
        }
        while (pushed_.size() > common) {
            audio.pop_music();
            pushed_.pop_back();
        }
        for (std::size_t i = common; i < wanted.size(); ++i) {
            audio.push_music(wanted[i]);
            pushed_.push_back(wanted[i]);
        }
    }
    // Audio's stack was cleared elsewhere (select_music): start over from the playlist.
    void forget() {
        pushed_.clear();
        pop_pending_ = false;
    }
    [[nodiscard]] const std::vector<audio::Music> &pushed() const noexcept { return pushed_; }

  private:
    std::vector<audio::Music> pushed_;
    bool pop_pending_ = false;
    std::uint64_t pop_since_ = 0;
};

// Walk and time-bomb loops: started when a step animation is shown, stopped shortly after the
// last one (the core resets the animation between two steps of the same walk).
class WalkSound {
  public:
    static constexpr std::uint64_t stop_after_ms = 150;

    void update(audio::Audio &audio, std::optional<std::size_t> walk, std::optional<std::size_t> bomb,
                std::uint64_t now_ms) {
        if (walk) {
            last_walk_ = now_ms;
            audio.play_loop(*walk, audio::Loop::walk);
            if (bomb)
                audio.play_loop(*bomb, audio::Loop::bomb);
            else
                audio.stop_loop(audio::Loop::bomb);
            return;
        }
        if (now_ms - last_walk_ >= stop_after_ms)
            stop(audio);
    }
    void stop(audio::Audio &audio) {
        audio.stop_loop(audio::Loop::walk);
        audio.stop_loop(audio::Loop::bomb);
    }

  private:
    std::uint64_t last_walk_ = 0;
};

} // namespace rich4::native
