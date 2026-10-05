// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rich4/random.hpp"
#include "rich4/world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace rich4 {

// Values match the original auction callback's action numbers.
enum class AuctionAction : std::uint8_t {
    pass = 0, raise_100 = 1, raise_500 = 2, raise_1000 = 3,
    raise_5000 = 4, raise_10000 = 5, withdraw = 6
};

struct AuctionDecision {
    std::size_t player = 0;
    std::int32_t price = 0;
    std::vector<AuctionAction> actions;
};

struct AuctionResult {
    std::optional<std::size_t> winner;
    std::int32_t price = 0;
    std::uint16_t property = 0;
};

struct AuctionOptions {
    // Override original 0x4751f0[World::lease_mode] for explicit replay. An
    // absent override reads the persisted mode; zero means permanent ownership.
    std::optional<std::uint32_t> lease_interval;
};

// A synchronous, resumable port of 0x43bde5/0x439f0d/0x43a2dd. The World and
// Random must outlive this object and must not be changed during a pending
// auction. Construction consumes the two valuation draws per eligible AI;
// tick/bid consume no randomness. Win32 timer/announcer animation draws are
// deliberately outside this game-state module (see migration 016).
class AuctionSession {
public:
    AuctionSession(World& world, Random& random, std::uint16_t property,
                   std::int32_t seller = -1, AuctionOptions options = {});

    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const AuctionResult& result() const noexcept { return result_; }
    [[nodiscard]] std::int32_t price() const noexcept { return price_; }
    [[nodiscard]] std::int32_t initial_price() const noexcept { return initial_price_; }
    [[nodiscard]] std::optional<AuctionDecision> decision() const;
    // Execute one AI decision, or return false while waiting for a human.
    bool tick();
    // Accept a legal action for the current bidder. Rejected actions mutate
    // neither the auction nor the world. Exposed for UI and deterministic hooks.
    bool bid(AuctionAction action);
    [[nodiscard]] std::optional<std::int32_t> ai_limit(std::size_t player) const noexcept;

private:
    struct Bidder {
        std::size_t player = 0;
        std::int32_t limit = 0;
        bool present = false;
        bool passed = false;
        bool human = false;
    };
    [[nodiscard]] AuctionAction ai_action() const;
    [[nodiscard]] bool legal(AuctionAction action) const;
    void after_action();
    void settle();
    World* world_;
    std::uint16_t property_;
    std::int32_t seller_;
    AuctionOptions options_;
    std::array<Bidder, 4> bidders_{};
    std::size_t current_ = 0;
    std::size_t original_eligible_count_ = 0;
    std::optional<std::size_t> last_bidder_;
    std::int32_t initial_price_ = 0;
    std::int32_t price_ = 0;
    bool finished_ = false;
    AuctionResult result_{};
};

} // namespace rich4
