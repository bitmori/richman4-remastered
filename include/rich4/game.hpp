// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rich4/cards.hpp"
#include "rich4/save.hpp"

namespace rich4 {

class World;
namespace trading {
struct Listing;
}

enum class GamePhase { human_ready, rolling, moving, awaiting_decision, advancing_turn, finished };
enum class DecisionKind {
    buy_land,
    upgrade_land,
    bank,
    card_target,
    choose_card,
    rescue,
    buy_facility,
    construct_facility,
    upgrade_facility,
    roulette,
    shop,
    magic_category,
    magic_effect,
    auction,
    lottery,
    primary_stock,
    property_target,
    action_target,
    notice,
    money_spinner
};
enum class GameEventKind {
    turn_started,
    dice_rolled,
    moved,
    land_purchased,
    land_upgraded,
    rent_paid,
    bank_transfer,
    date_changed,
    points_received,
    player_bankrupt,
    game_finished,
    unsupported_event,
    notice
};

struct DecisionLocation {
    std::int32_t x, y;
};

struct DecisionOption {
    std::string label;
    std::int32_t value;
    std::optional<DecisionLocation> location = std::nullopt;
};

struct GameDecision {
    DecisionKind kind;
    std::size_t player;
    std::uint16_t land_id = 0;
    std::int32_t amount = 0;
    std::string prompt;
    std::vector<DecisionOption> options;
    std::int32_t max_amount = 0;
};

struct GameEvent {
    GameEventKind kind;
    std::size_t player;
    std::int32_t amount = 0;
    std::uint16_t node = 0;
    std::string message;
    std::optional<std::uint8_t> voice_category = std::nullopt;
    // Card id (1..30) when this event reports a card being used, for the card-art popup.
    std::optional<std::uint8_t> card = std::nullopt;
};

struct MoveAnimation {
    std::size_t player;
    std::int32_t from_x, from_y, to_x, to_y;
    unsigned elapsed_ms, duration_ms;
};

// Owns a portable World and a Watcom random stream. Window/audio code never
// participates in the rule implementation. tick() changes animation timing;
// rolling and path selection consume randomness only once per rule action.
class GameSession {
  public:
    explicit GameSession(World world, std::uint32_t seed = 1);
    ~GameSession();
    GameSession(GameSession &&) noexcept;
    GameSession &operator=(GameSession &&) noexcept;
    GameSession(const GameSession &) = delete;
    GameSession &operator=(const GameSession &) = delete;

    [[nodiscard]] const World &world() const noexcept;
    [[nodiscard]] GamePhase phase() const noexcept;
    [[nodiscard]] const std::optional<GameDecision> &pending_decision() const noexcept;
    [[nodiscard]] const std::optional<MoveAnimation> &animation() const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t> &dice() const noexcept;
    [[nodiscard]] std::uint32_t random_state() const noexcept;
    [[nodiscard]] int steps_remaining() const noexcept;
    [[nodiscard]] std::uint64_t completed_days() const noexcept;

    // Returns false for input that does not apply to the current phase. option
    // is an index into pending_decision().options. Bank amount must be positive.
    bool roll_dice();
    bool set_dice_count(unsigned count);
    [[nodiscard]] std::optional<std::size_t> winning_player() const noexcept;
    void set_camera_rotation(std::uint8_t rotation);
    [[nodiscard]] std::vector<CardTarget> card_targets(std::uint8_t card) const;
    bool use_card(const CardUse &card);
    bool use_tool(unsigned id, std::uint16_t target = 0, std::uint16_t destination = 0);
    bool dismount();
    bool buy_stock(std::size_t stock, std::int32_t quantity);
    bool sell_stock(std::size_t stock, std::int32_t quantity);
    bool close_stock_market();
    bool open_trading();
    bool post_offer(const trading::Listing &offer);
    bool cancel_offer(std::size_t index);
    bool buy_offer(std::size_t seller, std::size_t index);
    bool close_trading();
    bool respond(std::size_t option, std::int32_t amount = 0);
    void tick(unsigned elapsed_ms);
    [[nodiscard]] std::vector<GameEvent> drain_events();

    // Saving during an animation/decision is rejected, so a reload never loses
    // an in-flight action. World handles all original byte-layout serialization.
    [[nodiscard]] bool can_save() const noexcept;
    [[nodiscard]] SaveFile export_save() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rich4
