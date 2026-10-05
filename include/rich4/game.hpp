// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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

// Original on-screen announcements (docs/ui/messages.md). Presentation only: emitting
// them never changes a rule outcome or consumes random numbers.
enum class Announcement : std::uint8_t {
    none,
    toll,              // Box before a toll: property, subject=owner, second=ally, amount, value=multiplier.
    cash_insufficient, // "您的現金不足！" box when a buy/upgrade costs more than cash.
    news,              // Full-screen news: id 0..35 plus body arguments.
    fortune,           // Full-screen fortune: id 0..48 plus body arguments.
    god_arrival,       // God attached: id = object kind (1..10, 12, 15).
    point_coupon,      // "得點券N點" box (1000 ms): amount 10/30/50.
    absent_status,     // Turn-start "%s坐牢中\n\n還剩%d天！" box: id 0 hotel, 1 away, 2 prison,
                       // 3 hospital, 4 hibernating; value = days left.
};

struct GameEvent {
    GameEventKind kind;
    std::size_t player;
    std::int32_t amount = 0;
    std::uint16_t node = 0;
    std::string message;
    std::optional<std::uint8_t> voice_category = std::nullopt;
    Announcement announcement = Announcement::none;
    std::uint8_t announcement_id = 0; // News/fortune handler, god kind or facility type (toll).
    std::uint16_t property = 0;       // Original property code: 2000+land, 4000+facility.
    std::int32_t subject = -1, second_subject = -1, company = -1, value = 0;
    std::uint8_t players = 0; // Affected player slots (portrait rows).
    std::array<std::int32_t, 4> player_amounts{};
};

// Presentation-only record of an original on-board animation (docs/ui/effects.md §9). The rule
// change has ALREADY happened when the effect is drained; the app replays the picture (FLC movie,
// sprite throw, dice) and may show the pre-mutation board until the movie's commit frame.
// Emitting effects never consumes randomness and never changes rule outcomes.
enum class EffectKind : std::uint8_t {
    dice_roll,         // Panel 4..6 + Panel 3 faces; `dice` holds the values already rolled
    top_floor_sparkle, // Data 523, a property reached level 5
    build_rise,        // Data 553, robot worker free build
    object_throw,      // roadblock/mine/time bomb flies from `from` to `target`; object_kind 16/17/18
    missile,           // Data 528
    nuclear_missile,   // Data 530
    explosion,         // Data 525, mine stepped on / time bomb exploded
    demolish,          // Data 529, demolish card
    monster,           // Data 557, monster card
    god_appear,        // Data 540.., object_kind = god kind 1..15
    demon_puff,        // Data 526, demon god downgraded the property at `target`
    bankrupt           // Data 555
};
struct EffectPoint {
    std::int32_t x = 0, y = 0; // Original world coordinates (same space as board::Point).
};
struct EffectEvent {
    EffectKind kind;
    std::size_t player = 0;                         // actor
    std::uint16_t node = 0;                         // actor's road node, or the throw's target node
    EffectPoint target{};                           // world point the camera centres on
    std::optional<EffectPoint> from = std::nullopt; // throws: actor world point
    std::uint8_t object_kind = 0;                   // throws: 16/17/18; god_appear: god kind
    std::vector<std::uint8_t> dice{};               // dice_roll values
    std::uint16_t property = 0;                     // target property code (2001.. / 4001..), if any
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
    // Additive presentation queue, in emission order (see EffectEvent).
    [[nodiscard]] std::vector<EffectEvent> drain_effects();

    // Saving during an animation/decision is rejected, so a reload never loses
    // an in-flight action. World handles all original byte-layout serialization.
    [[nodiscard]] bool can_save() const noexcept;
    [[nodiscard]] SaveFile export_save() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rich4
