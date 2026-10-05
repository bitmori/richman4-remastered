// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/events.hpp"
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace rich4 {

enum class CardTargetKind { actor, property, stock, object, card, tool };
struct CardTarget {
    CardTargetKind kind = CardTargetKind::actor;
    // Actors/stocks are zero-based; properties retain 2001../4001.. map codes;
    // objects/cards/tools are one-based original IDs.
    std::uint16_t value = 0;
    bool operator==(const CardTarget &) const = default;
};
struct CardUse {
    std::uint8_t card_id = 0;
    std::optional<CardTarget> target;
    std::optional<CardTarget> choice; // Robbery inventory choice, or facility type in value.
};
enum class CardUseStatus { used, cancelled, invalid, missing_callback };
struct CardResult {
    CardUseStatus status = CardUseStatus::invalid;
    std::string message;
    [[nodiscard]] bool used() const noexcept {
        return status == CardUseStatus::used;
    }
};
struct CardLookahead {
    std::vector<std::uint16_t> nodes;
    bool crossed_junction = false; // 0x40b221 return value, used by tortoise AI.
};
struct CardAiMemory {
    std::uint32_t parameter0 = 0, parameter1 = 0, parameter2 = 0;
};

struct CardCallbacks : EventCallbacks {
    std::function<void(std::size_t, std::size_t, std::int32_t)> hostility;
    std::function<void(std::size_t)> turn_around;
    std::function<void(std::uint16_t, bool)> change_building; // true: upgrade, false: downgrade.
    std::function<void(std::uint16_t)> release_hotel;
    std::function<bool(std::uint16_t, std::int32_t)> auction_property;
    std::function<std::optional<CardTarget>(std::uint8_t, std::span<const CardTarget>)> choose_target;
    std::function<std::optional<CardTarget>(std::size_t, std::size_t)> choose_inventory;
    std::function<std::optional<std::uint8_t>(std::size_t)> choose_facility_type;
    std::function<bool(std::size_t, std::size_t, std::int32_t)> try_free;
    std::function<std::int32_t(std::size_t, std::uint8_t)> try_frame;
    std::function<void(std::uint16_t)> remove_object;
    std::function<void(std::size_t)> detach_god;
    std::function<void(std::size_t, std::uint16_t)> attach_god;
    std::function<bool()> market_closed; // 0x428d01: news closure or calendar holiday.
    std::function<CardLookahead(std::size_t, std::size_t)> lookahead_nodes;
    // Original 0x40a45c scan order and packed instance codes. Used by nearest-god
    // selection and computer predicates; presentation must not draw gameplay RNG.
    std::function<std::vector<std::uint16_t>(std::size_t)> nearby_instances;
};

[[nodiscard]] bool active_card(std::uint8_t id) noexcept;
// Logical filter from 0x445e4d, plus the original viewport scan when supplied.
// A UI must supply nearby_instances: card selectors do not enable map panning.
[[nodiscard]] std::vector<CardTarget> legal_card_targets(const World &, std::size_t actor,
                                                         std::uint8_t card_id, const CardCallbacks & = {});
[[nodiscard]] CardResult apply_card(World &, Random &, std::size_t actor, const CardUse &,
                                    const CardCallbacks & = {});
// Original UI visit: scans at most eight cards; at most one is played. This is
// separate from apply_card so manual actions never draw AI-only random values.
[[nodiscard]] std::optional<CardUse> choose_ai_card(const World &, Random &, std::size_t actor,
                                                    const CardCallbacks & = {}, CardAiMemory * = nullptr);
} // namespace rich4
