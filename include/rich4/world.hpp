// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/economy.hpp"
#include "rich4/inventory.hpp"
#include "rich4/save.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rich4 {

// Runtime values, never overlays of the original packed 32-bit structures.
struct PlayerStatus {
    std::uint8_t init_cash_ratio = 50;
    std::uint8_t days_in_hotel = 0;
    std::uint8_t days_disappearing = 0;
    std::uint8_t days_in_prison = 0;
    std::uint8_t days_in_hospital = 0;
    std::uint8_t days_sleeping = 0;
    std::uint8_t days_sleep_walking = 0;
    std::uint8_t days_stopping = 0;
    std::uint8_t days_tortoise_walking = 0;
    std::uint8_t days_rejected_by_bank = 0;
    std::uint8_t days_bank_no_loans = 0;
    std::uint8_t allied_days = 0;
    std::uint8_t days_assurance = 0;
    std::uint8_t god_info = 0;
    std::uint8_t allied_player = 0; // One-based owner code, zero means none.
    std::uint8_t total_winter_sleep_days = 0;
    std::array<std::uint32_t, 4> hostility{};
    std::uint32_t cumulative_spending = 0;
    std::uint32_t cumulative_income = 0;
    std::uint8_t flags_100 = 0;
    std::uint8_t flags_101 = 0;
    std::uint8_t flags_102 = 0;
    std::uint8_t flags_103 = 0;
};

struct WorldPlayer {
    PlayerState state{};
    PlayerStatus status{};
};

struct MapNode {
    std::uint32_t id = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::array<std::uint16_t, 4> neighbors{}; // Zero is an unused link.
    std::uint16_t type_and_index = 0; // Original event/property code, not an enum guess.
    std::uint16_t ground_sprite = 0;
    std::uint32_t flags = 0;
};

struct Land {
    std::uint32_t id = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    LandEconomyRecord economy;
    std::uint8_t temporary_state = 0;
    std::uint8_t direction = 0;
    std::uint16_t price_per_level = 0;
    std::uint16_t land_price = 0;
    std::uint32_t removed_building = 0;
    std::uint32_t expired_date = 0;
};

struct Facility {
    std::uint32_t id = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::string name; // Original Big5 bytes, excluding NUL padding.
    std::uint8_t facility_type = 0;
    std::uint8_t owner_code = 0;
    std::uint8_t level = 0;
    std::uint8_t direction = 0;
    std::uint8_t temporary_state = 0;
    std::uint16_t land_price = 0;
    std::uint16_t price_per_level = 0;
    std::uint32_t removed_building = 0;
    std::uint32_t expired_date = 0;
};

struct Commercial {
    std::uint32_t id = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t owner_code = 0;
    std::uint8_t commerce_type = 0;
    std::uint8_t direction = 0;
    std::uint16_t sprite_index = 0;
    std::uint16_t toll_fee = 0;
};

// Fifth map table. Only rendering placement fields are interpreted; all other
// bytes remain in the backing map and are not assigned invented game semantics.
struct Landscape {
    std::uint32_t id = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint8_t direction = 0;
    std::uint16_t sprite_index = 0;
};

struct NewGamePlayer {
    std::uint8_t character = 10;
    std::uint8_t controller = 1; // 1 human, 2 computer.
    std::uint8_t traffic_method = 0;
    std::uint16_t start_node = 0; // Zero defers spawning to the player's first turn.
};

struct NewGameOptions {
    // The application supplies its current local date; the core reads no clock.
    SaveDate date{5, 10, 2026};
    std::uint16_t map_id = 0;
    std::uint16_t stage_id = 0;
    std::int32_t initial_fund = 200000;
    std::uint32_t random_seed = 1;
    std::vector<NewGamePlayer> players{{10, 1, 0, 0}, {3, 2, 0, 0},
                                       {0, 2, 0, 0}, {6, 2, 0, 0}};
};

class World {
public:
    static World from_save(const SaveFile& save);
    // Accepts the decoded odd MAP.MKF metadata entry, not its compressed bytes.
    static World from_map_resource(std::span<const std::uint8_t> map,
                                   const NewGameOptions& options = {});

    SaveHeader header{};
    std::vector<WorldPlayer> players;
    // Map vectors retain the original index-zero sentinel. IDs are array indices.
    std::vector<MapNode> nodes;
    std::vector<Land> lands;
    std::vector<Facility> facilities;
    std::vector<Commercial> commercials;
    std::vector<Landscape> landscapes;
    CardSupply card_supply{};
    ToolSupply tool_supply{};

    [[nodiscard]] WorldPlayer& active_player();
    [[nodiscard]] const WorldPlayer& active_player() const;
    // Original bankruptcy clears record bytes 28..103, including unknown fields.
    // It does not clear separate card/tool inventories or the controller byte.
    void clear_bankrupt_player_fields(std::size_t slot);
    [[nodiscard]] const MapSummary& map_summary() const noexcept { return map_; }
    // The initial spawning stream's final state, for a fresh session to continue.
    // An old SAVE does not persist Watcom's PRNG state. Zero is a valid seed.
    [[nodiscard]] std::uint32_t initial_random_state() const noexcept { return random_state_; }
    [[nodiscard]] bool has_initial_random_state() const noexcept { return random_state_available_; }
    // Known fields are patched into the retained original bytes. Unknown fields,
    // historical snapshots and trailing bytes are preserved. Resizing is rejected.
    [[nodiscard]] std::vector<std::uint8_t> export_save() const;

private:
    MapSummary map_{};
    std::vector<std::uint8_t> backing_;
    std::uint32_t random_state_ = 0;
    bool random_state_available_ = false;
};

} // namespace rich4
