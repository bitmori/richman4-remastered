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

// Presentation state of the original number-roll UIs (docs/ui/messages.md §2.3), mirrored by the
// core while a spinner decision is pending (GameDecision::spinner) or carried by a spinner effect
// (EffectEvent::spinner) for computer/sleep-walking spins and the wheel's slow-down after STOP.
// Read-only for the app: it never feeds back into a rule or draws randomness.
struct SpinnerState {
    // money_spinner: god kind 1/2/5/6 (fcn_00440706, slot machine Panel.mkf 67).
    // roulette: wheel 0 travel, 1 hotel, 2 mall, 3 insurance (fcn_0044090e, cupid wheel Panel.mkf 68+type).
    std::uint8_t type = 0;
    // Slot machine reel positions 0..19 (digit * 2; odd = between two digits), as MoneySpinner.
    std::array<std::uint8_t, 4> digits{};
    // Slot machine: MoneySpinner phase (0 = not started yet, 1 spinning, 2..8 stopping, 9 result
    // hold, 10 done). Wheel effect: 1 = computer spin (free-runs 39 frames first), 2 = stopped by STOP.
    std::uint8_t phase = 0;
    std::uint8_t cursor = 0;   // Wheel sector 0..11 (Panel frame 2 + cursor).
    std::int32_t subject = -1; // Wheel: hotel/mall owner slot, or commercial id (travel/insurance).
    std::uint32_t seed = 0;    // Money effect: Random state the computer spin starts from.
    std::int32_t result = 0;   // Effect: the amount / wheel value the rule already applied.
    bool operator==(const SpinnerState &) const = default;
};

struct GameDecision {
    DecisionKind kind;
    std::size_t player;
    std::uint16_t land_id = 0;
    std::int32_t amount = 0;
    std::string prompt;
    std::vector<DecisionOption> options;
    std::int32_t max_amount = 0;
    std::optional<SpinnerState> spinner = std::nullopt; // money_spinner / roulette only
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

// Which original per-character speech table a bubble line comes from (docs/ui/messages.md §1.4).
// event: _rich4_event_strings (voice_category); card: _rich4_card_strings, slot = row*30 + card-1
// with row 0 the user, 1 the user targeting itself, 2 the victim; tool: _tool_strings, slot =
// tool-1 for tools 1..13, 14 roadblock hit, 15 mine hit, 16 time bomb picked up.
enum class SpeechTable : std::uint8_t { event, card, tool };

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
    // Card id (1..30) when this event reports a card being used, for the card-art popup.
    std::optional<std::uint8_t> card = std::nullopt;
    // Card/tool speech bubble (presentation only, never draws randomness): `player` speaks
    // table[character][speech_slot] with portrait mood `speech_mood` (the original call-site
    // argument). Event-table speech keeps using voice_category.
    SpeechTable speech_table = SpeechTable::event;
    std::uint8_t speech_slot = 0, speech_mood = 0;
    bool operator==(const GameEvent &) const = default; // pre-choice presentation replay matching
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
    bankrupt,          // Data 555
    money_spinner,     // computer/sleep-walking god slot machine (fcn_00440706); `spinner` set
    roulette,          // cupid wheel: computer spin, or the slow-down after a human STOP; `spinner` set
    // ---- Board movies (docs/audit/data-400-601.md). `from` = the actor's old world point when the
    // movie shows the actor leaving (drawn there until the commit frame), else unset. ----
    ambulance,         // Data 524, hospitalised (rich4_hospital_utils.asm:92)
    police,            // Data 538, jailed (rich4_prison_utils.asm:92)
    dog_vehicle,       // Data 552, rode into the dog (core_actions:2919)
    dog_fight,         // Data 532, met the dog on foot (core_actions:2945)
    ufo,               // Data 533, abducted (object_kind 0) or returned (object_kind 1)
    airplane,          // Data 558, left on a trip (object_kind 0) or returned (object_kind 1)
    parachute,         // Data 559 + character, player placed on the board (fcn_0040829d/fcn_00418c55)
    news_movie,        // Data 531/539/527/534/535 for news object_kind 4/5/15/20/21 at `target`
    card_throw,        // Data 415 card flies `from` -> `target` (AI targeted cards, animate_object(0))
    item_box,          // Data 536 spinning "?" before the random card square (core_actions:2507)
    coupon,            // Data 537 spinning ticket before a point-coupon box (core_actions:2394)
    truck_demolish,    // 工程車 pose 12 swing + Data 526 puff (player_utils state 3); object_kind =
                       // facing direction, previous_level = building level before the demolition
    holiday_fireworks, // Data 523 on New Year / national day (holiday table 0x47ff4c)
    christmas          // Data 554 on Dec 25 (holiday table 0x47ff4c)
};
struct EffectPoint {
    std::int32_t x = 0, y = 0; // Original world coordinates (same space as board::Point).
    bool operator==(const EffectPoint &) const = default;
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
    // Presentation order: how many GameEvents of the same drain batch were emitted before this
    // effect, so the app can interleave drain_effects() with drain_events() in true order.
    std::size_t after_events = 0;
    std::optional<SpinnerState> spinner = std::nullopt; // money_spinner / roulette
    // truck_demolish: `property` level and type (land chain flag / facility type) shown until the
    // commit frame, i.e. the building before the rule demolished it.
    std::uint8_t previous_level = 0, previous_type = 0;
    bool operator==(const EffectEvent &) const = default;
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
    // Presentation only: 19 (hospital gown) or 20 (prison stripes) while a player pulled out of
    // hospital/prison early still wears it (fcn_0040d761 -> fcn_0040bf93), else 0.
    [[nodiscard]] std::uint8_t player_costume(std::size_t slot) const noexcept;

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
