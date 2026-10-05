// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/game.hpp"
#include "rich4/world.hpp"
#include "rich4/calendar.hpp"
#include "rich4/direction.hpp"
#include "rich4/economy.hpp"
#include "rich4/events.hpp"
#include "rich4/inventory.hpp"
#include "rich4/random.hpp"
#include "rich4/stocks.hpp"
#include "rich4/services.hpp"
#include "rich4/auction.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <functional>
#include <stdexcept>
#include <utility>

namespace rich4 {
namespace {
std::int32_t add32(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::int32_t subtract32(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}
std::int32_t multiply32(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
std::string player_name(std::size_t slot) { return "玩家" + std::to_string(slot + 1); }
constexpr unsigned roll_duration = 350;
constexpr unsigned step_duration = 160;
struct NeedChoice { GameDecision decision; std::size_t key; };
struct UnsupportedRule { std::string message; };
}

struct GameSession::Impl {
    World world;
    Random random;
    GamePhase phase = GamePhase::human_ready;
    std::optional<GameDecision> decision;
    std::optional<MoveAnimation> animation;
    std::vector<std::uint8_t> faces;
    std::vector<GameEvent> events;
    unsigned phase_elapsed = 0;
    int remaining = 0;
    std::uint16_t next_node = 0;
    std::uint64_t days = 0;
    bool resume_movement = false;
    std::map<std::size_t, int> event_choices;
    std::function<void()> pending_continuation;
    std::size_t pending_choice_key = 0;
    unsigned roulette_cursor = 0, roulette_elapsed = 0;
    std::size_t roulette_facility = 0;
    bool rescue_hospital = false;
    std::size_t choice_sequence = 0;
    std::optional<services::Shop> shop;
    std::uint16_t shop_property = 0;
    unsigned magic_category = 0;

    explicit Impl(World value, std::uint32_t seed) : world(std::move(value)),
        random(world.has_initial_random_state() ? world.initial_random_state() : seed) {
        if (world.players.empty() || world.players.size() > 4 ||
            world.header.current_player >= world.players.size())
            throw std::invalid_argument("invalid native game player table");
        if (!calendar::is_valid({world.header.date.year, world.header.date.month, world.header.date.day}))
            throw std::invalid_argument("invalid native game date");
        for (const auto& player : world.players)
            if (player.state.node_id >= world.nodes.size() || player.state.last_node_id >= world.nodes.size())
                throw std::invalid_argument("player node outside native world");
        start_turn(false);
    }

    std::size_t actor() const { return world.header.current_player; }
    PlayerState& player() { return world.players[actor()].state; }
    const PlayerState& player() const { return world.players[actor()].state; }
    bool human() const { return player().controller_flags == 1; }
    PlayerStatus& status() { return world.players[actor()].status; }
    const PlayerStatus& status() const { return world.players[actor()].status; }
    void emit(GameEventKind kind, std::string message, std::int32_t amount = 0) {
        events.push_back({kind, actor(), amount, player().node_id, std::move(message)});
    }
    void voice(std::size_t slot, std::uint8_t category) {
        events.push_back({GameEventKind::notice, slot, 0, world.players.at(slot).state.node_id, {}, category});
    }
    void amount_voice(std::size_t slot, std::int32_t amount, unsigned category) {
        if (amount <= 0) return;
        if (amount >= multiply32(9000, world.header.price_index)) voice(slot, static_cast<std::uint8_t>(category));
        else if (amount >= multiply32(5000, world.header.price_index))
            voice(slot, static_cast<std::uint8_t>(category + (random.next() & 1U)));
        else if (category != 6 || amount >= multiply32(2000, world.header.price_index))
            voice(slot, static_cast<std::uint8_t>(category + 2));
    }
    void absence_voice(std::size_t slot, int days_left) {
        if (days_left > 6) voice(slot, 3);
        else if (days_left > 3) voice(slot, static_cast<std::uint8_t>(3 + (random.next() & 1U)));
        else if (days_left > 0) voice(slot, 5);
    }
    void set_phase(GamePhase next) { phase = next; phase_elapsed = 0; }
    void finish_turn() { decision.reset(); animation.reset(); set_phase(GamePhase::advancing_turn); }
    void unsupported(std::string description) {
        emit(GameEventKind::unsupported_event, description);
        if (human()) {
            decision = GameDecision{DecisionKind::notice, actor(), 0, 0,
                std::move(description), {{"继续", 0}}};
            set_phase(GamePhase::awaiting_decision);
        } else finish_turn();
    }
    void spawn_if_needed() {
        auto& state = player();
        if (state.x != 0) return;
        std::vector<std::uint16_t> candidates;
        for (std::size_t i = 1; i < world.nodes.size(); ++i) {
            const auto& node = world.nodes[i];
            if (!(node.flags & 0x80ffff00U) &&
                std::any_of(node.neighbors.begin(), node.neighbors.end(), [](auto link) { return link != 0; })) {
                if (i > 255) throw std::runtime_error("original spawn candidate exceeds byte node ID");
                candidates.push_back(static_cast<std::uint16_t>(i));
            }
        }
        if (candidates.empty()) throw std::runtime_error("world has no unoccupied spawn node");
        state.node_id = candidates[random.next() % candidates.size()];
        const auto& node = world.nodes[state.node_id];
        std::vector<std::uint16_t> previous;
        for (auto neighbor : node.neighbors) if (neighbor) {
            if (neighbor >= world.nodes.size()) throw std::runtime_error("spawn neighbor outside table");
            previous.push_back(neighbor);
        }
        state.last_node_id = previous[random.next() % previous.size()];
        state.x = static_cast<std::uint16_t>(node.x);
        state.y = static_cast<std::uint16_t>(node.y);
        const auto& last = world.nodes[state.last_node_id];
        state.direction = direction::from_delta(node.x - last.x, node.y - last.y);
        world.nodes[state.node_id].flags |= 0x100U << actor();
    }
    static void age_status(std::uint8_t& value) {
        value &= 0x7fU;
        if (value && --value == 0) value = 0x80;
    }
    void update_status() {
        auto& value = status();
        bool released = false;
        for (auto* days_left : {&value.days_in_hotel, &value.days_disappearing,
                               &value.days_in_prison, &value.days_in_hospital}) {
            if (*days_left & 0x80U) { *days_left = 0; released = true; }
            else if (*days_left) {
                --*days_left;
                const auto mask = days_left == &value.days_disappearing ? 0x3fU : 0x7fU;
                if ((*days_left & mask) == 0) *days_left = 0x80;
            }
        }
        if (released) {
            if (!value.days_in_prison) world.prison_roster[actor()] = 0;
            if (!value.days_in_hospital) world.hospital_roster[actor()] = 0;
            auto& state = player(); const auto& node = world.nodes.at(state.node_id);
            state.controller_flags &= 0x0fU;
            state.direction = value.saved_direction < 8 ? value.saved_direction :
                direction::from_delta(node.x - state.x, node.y - state.y);
            state.x = static_cast<std::uint16_t>(node.x); state.y = static_cast<std::uint16_t>(node.y);
            world.nodes[state.node_id].flags |= 0x100U << actor();
        }
        if (!(value.days_in_hotel | value.days_disappearing | value.days_in_prison | value.days_in_hospital)) {
            age_status(value.days_sleeping); age_status(value.days_sleep_walking);
            age_status(value.days_tortoise_walking);
        }
        if ((value.allied_days & 0x80U) != 0) value.allied_player = 0;
        age_status(value.days_stopping); age_status(value.days_rejected_by_bank);
        age_status(value.days_bank_no_loans); age_status(value.allied_days); age_status(value.days_assurance);
    }
    void start_turn(bool update = true) {
        spawn_if_needed();
        decision.reset(); animation.reset(); remaining = 0;
        if (update) update_status();
        emit(GameEventKind::turn_started, player_name(actor()) + "的回合");
        const auto& value = status();
        if (value.days_in_hotel || value.days_disappearing || value.days_in_prison ||
            value.days_in_hospital || value.days_sleeping) {
            emit(GameEventKind::notice, player_name(actor()) + "本回合休息"); finish_turn();
        } else if (value.days_stopping) {
            faces.clear(); remaining = 0; land();
        } else if (human() && !value.days_sleep_walking) set_phase(GamePhase::human_ready);
        else begin_roll();
    }
    void begin_roll() {
        faces.clear(); remaining = 0;
        if (status().days_tortoise_walking) remaining = 1;
        else {
            // Original fcn_00419572: exactly one rand per die, modulo 6 plus one.
            for (unsigned i = 0; i < player().dice_count; ++i) {
                auto face = static_cast<std::uint8_t>(random.next() % 6 + 1);
                faces.push_back(face); remaining += face;
            }
        }
        emit(GameEventKind::dice_rolled, player_name(actor()) + "前进" + std::to_string(remaining) + "步", remaining);
        set_phase(GamePhase::rolling);
    }
    void start_step() {
        auto& state = player();
        const auto& node = world.nodes.at(state.node_id);
        std::vector<std::uint16_t> choices;
        for (unsigned i = 0; i < node.neighbors.size(); ++i) {
            const auto candidate = node.neighbors[i];
            if (candidate && candidate != state.last_node_id && !(node.flags & (0x40000000U >> i))) {
                if (candidate >= world.nodes.size()) throw std::runtime_error("map link outside node table");
                choices.push_back(candidate);
            }
        }
        // Original fcn_0040c05c consumes rand even for a single candidate.
        next_node = choices.empty() ? state.last_node_id : choices[random.next() % choices.size()];
        if (next_node >= world.nodes.size()) throw std::runtime_error("fallback node outside table");
        const auto& next = world.nodes[next_node];
        animation = MoveAnimation{actor(), state.x, state.y, next.x, next.y, 0, step_duration};
        set_phase(GamePhase::moving);
    }
    void complete_step() {
        auto& state = player();
        const auto old_node = state.node_id;
        const auto& next = world.nodes.at(next_node);
        world.nodes.at(old_node).flags &= ~(0x100U << actor());
        world.nodes.at(next_node).flags |= 0x100U << actor();
        state.last_node_id = old_node;
        state.node_id = next_node;
        state.direction = direction::from_delta(static_cast<std::int32_t>(next.x) - state.x,
                                                static_cast<std::int32_t>(next.y) - state.y);
        state.x = static_cast<std::uint16_t>(next.x);
        state.y = static_cast<std::uint16_t>(next.y);
        --remaining; animation.reset();
        emit(GameEventKind::moved, player_name(actor()) + "到达道路" + std::to_string(next_node));
        if (remaining > 0) {
            if ((next.flags & 0xffU) == 14 && !status().days_sleep_walking) open_bank(true);
            else start_step();
        } else land();
    }
    std::int32_t purchase_price(std::size_t land_id) const {
        const auto& land = world.lands.at(land_id);
        // Source offsets, rather than the misleading historical C field names:
        // +0x1c is base price; +0x1e is per building level.
        return multiply32(world.header.price_index,
                          add32(land.land_price, multiply32(land.economy.level, land.price_per_level)));
    }
    std::int32_t upgrade_price(std::size_t land_id) const {
        return multiply32(world.header.price_index, world.lands.at(land_id).price_per_level);
    }
    void buy(std::size_t land_id, bool upgrade) {
        auto& land = world.lands.at(land_id);
        const auto cost = upgrade ? upgrade_price(land_id) : purchase_price(land_id);
        if (cost < 0 || player().cash < cost) { finish_turn(); return; }
        player().cash = subtract32(player().cash, cost);
        if (upgrade) ++land.economy.level;
        else land.economy.owner_code = static_cast<std::uint8_t>(actor() + 1);
        emit(upgrade ? GameEventKind::land_upgraded : GameEventKind::land_purchased,
             player_name(actor()) + (upgrade ? "升级房产" : "买下房产"), cost);
        if (upgrade && land.economy.level == 5) voice(actor(), 15);
        else {
            unsigned matching = 0;
            for (const auto& other : world.lands)
                if (other.economy.owner_code == actor() + 1 && other.economy.street_name == land.economy.street_name) ++matching;
            if (matching >= 3 && (!upgrade || random.next() % 3 == 0)) voice(actor(), upgrade ? 17 : 16);
        }
        finish_turn();
    }
    void property(std::size_t land_id) {
        if (!land_id || land_id >= world.lands.size()) { unsupported("房产记录无效，暂时无法结算"); return; }
        auto& land = world.lands[land_id];
        auto& economy = land.economy;
        if (!economy.owner_code || economy.owner_code == actor() + 1) {
            if (status().days_sleep_walking || status().god_info == 12) { finish_turn(); return; }
            if (status().god_info == 7 || status().god_info == 8 || status().god_info == 15) {
                emit(GameEventKind::notice, "神明阻止这次房产交易"); finish_turn(); return;
            }
            const bool upgrade = economy.owner_code != 0;
            if (upgrade && (economy.chain_store || economy.level >= 5)) { finish_turn(); return; }
            const auto cost = upgrade ? upgrade_price(land_id) : purchase_price(land_id);
            if (cost < 0 || player().cash < cost) { finish_turn(); return; }
            if (human()) {
                decision = GameDecision{upgrade ? DecisionKind::upgrade_land : DecisionKind::buy_land,
                    actor(), static_cast<std::uint16_t>(land_id), cost,
                    upgrade ? "升级这块房产？" : "购买这块房产？",
                    {{upgrade ? "升级" : "购买", 1}, {"放弃", 0}}};
                set_phase(GamePhase::awaiting_decision);
            } else {
                const auto reserve = multiply32(static_cast<std::int32_t>(std::min(world.header.initial_fund * 0.05, 7000.0)),
                                                 world.header.price_index);
                if (upgrade || static_cast<std::int64_t>(player().cash) + player().bank_balance - cost > reserve)
                    buy(land_id, upgrade);
                else finish_turn();
            }
            return;
        }
        const auto owner = static_cast<std::size_t>(economy.owner_code - 1);
        if (owner >= world.players.size()) { unsupported("房产所有者无效，暂时无法结算"); return; }
        const auto& owner_status = world.players[owner].status;
        if (owner_status.allied_player == actor() + 1 || owner_status.god_info == 15 ||
            owner_status.days_in_hotel || owner_status.days_disappearing || owner_status.days_in_prison ||
            owner_status.days_in_hospital || owner_status.days_sleeping || owner_status.days_sleep_walking ||
            ((land.temporary_state & 0xf0U) && (land.temporary_state & 0x0fU))) { finish_turn(); return; }
        std::vector<LandEconomyRecord> records;
        for (std::size_t i = 1; i < world.lands.size(); ++i) records.push_back(world.lands[i].economy);
        std::int32_t toll = land_toll(records, economy.owner_code, world.header.price_index,
            economy.chain_store ? std::nullopt : std::optional<std::string_view>(economy.street_name));
        if (land.temporary_state) toll = multiply32(toll, 2);
        if (status().god_info == 1) toll >>= 1;
        else if (status().god_info == 2) toll = 0;
        else if (status().god_info == 5) toll = add32(toll, toll >> 1);
        else if (status().god_info == 6) toll = multiply32(toll, 2);
        land.removed_building = static_cast<std::uint32_t>(toll);
        pay_toll(owner, toll);
        if (phase != GamePhase::finished && phase != GamePhase::awaiting_decision) finish_turn();
    }
    bool transaction_blocked() const {
        return status().god_info == 7 || status().god_info == 8 || status().god_info == 15;
    }
    bool ai_can_purchase(std::int32_t cost) const {
        const auto reserve = multiply32(static_cast<std::int32_t>(std::min(world.header.initial_fund * 0.05, 7000.0)),
                                        world.header.price_index);
        return static_cast<std::int64_t>(player().cash) + player().bank_balance - cost > reserve;
    }
    void buy_facility(std::size_t id, int operation, unsigned type = 0) {
        auto& facility = world.facilities.at(id);
        const auto cost = multiply32(operation == 2 ? facility.price_per_level() : facility.land_price,
                                     world.header.price_index);
        // The original assigns the selected construction type before the god
        // veto, but leaves level and balance unchanged if the purchase is blocked.
        if (operation == 1) facility.facility_type = static_cast<std::uint8_t>(type);
        if (cost < 0 || player().cash < cost || transaction_blocked()) { finish_turn(); return; }
        player().cash = subtract32(player().cash, cost);
        if (operation == 0) facility.owner_code = static_cast<std::uint8_t>(actor() + 1);
        else ++facility.level;
        emit(operation ? GameEventKind::land_upgraded : GameEventKind::land_purchased,
             operation == 0 ? "购买设施用地" : operation == 1 ? "建设设施" : "升级设施", cost);
        if (facility.level == 5) voice(actor(), 15);
        finish_turn();
    }
    static int roulette_value(unsigned kind, unsigned cursor) {
        static constexpr int rows[4][12] = {
            {1,-1,0,-1,1,-1,2,-1,3,-1,2,-1},
            {-1,-1,1,-1,-1,4,-1,3,-1,-1,2,-1},
            {1,-1,6,-1,5,-1,4,-1,3,-1,2,-1},
            {5,-1,3,-1,30,-1,20,-1,15,-1,10,-1}};
        while (rows[kind][cursor % 12] < 0) ++cursor;
        return rows[kind][cursor % 12];
    }
    void charge_facility(std::size_t id, int multiplier) {
        auto& facility = world.facilities.at(id);
        const auto owner = static_cast<std::size_t>(facility.owner_code - 1);
        std::int32_t toll = 0;
        if (facility.facility_type == 3) {
            const auto traffic = player().traffic_method & 3U;
            if (traffic == 0) { finish_turn(); return; }
            unsigned dice_total = 0;
            for (auto face : faces) dice_total += face;
            if (faces.empty() && status().days_tortoise_walking) dice_total = 1;
            toll = multiply32(multiply32(static_cast<std::int32_t>(dice_total), traffic == 1 ? 500 : 1000), world.header.price_index);
        } else {
            toll = multiply32(multiply32(facility.toll_by_level.at(facility.level), world.header.price_index), multiplier);
            if (facility.temporary_state) toll = multiply32(toll, 2);
        }
        if (status().god_info == 1) toll >>= 1;
        else if (status().god_info == 2) toll = 0;
        else if (status().god_info == 5) toll = add32(toll, toll >> 1);
        else if (status().god_info == 6) toll = multiply32(toll, 2);
        facility.removed_building = static_cast<std::uint32_t>(toll);
        pay_toll(owner, toll);
        if (phase == GamePhase::finished) return;
        if (facility.facility_type == 1 && player().controller_flags) {
            clear_absences(actor());
            status().days_in_hotel = static_cast<std::uint8_t>(multiplier - 1);
            if (!status().days_in_hotel) status().days_in_hotel = 0x80;
            status().total_winter_sleep_days = static_cast<std::uint8_t>(status().total_winter_sleep_days + multiplier);
            status().facility_id = static_cast<std::uint16_t>(id);
            status().saved_direction = player().direction;
            world.nodes.at(player().node_id).flags &= ~(0x100U << actor());
            player().x = facility.x; player().y = facility.y;
            absence_voice(actor(), multiplier);
            emit(GameEventKind::notice, "在旅馆住宿" + std::to_string(multiplier) + "天", multiplier);
        }
        finish_turn();
    }
    void facility(std::size_t id) {
        if (!id || id >= world.facilities.size()) { unsupported("设施记录无效"); return; }
        auto& entry = world.facilities[id];
        if (!entry.owner_code || entry.owner_code == actor() + 1) {
            if (status().days_sleep_walking || (!entry.owner_code && status().god_info == 12)) { finish_turn(); return; }
            const int operation = !entry.owner_code ? 0 : entry.level == 0 ? 1 : 2;
            static constexpr unsigned maximum[5]{1,5,5,1,5};
            if (entry.facility_type >= 5 || (operation == 2 && entry.level >= maximum[entry.facility_type])) { finish_turn(); return; }
            const auto cost = multiply32(operation == 2 ? entry.price_per_level() : entry.land_price, world.header.price_index);
            if (cost < 0 || player().cash < cost) { finish_turn(); return; }
            if (human()) {
                std::vector<DecisionOption> options;
                if (operation == 1) options = {{"旅馆",1},{"百货公司",2},{"加油站",3},{"研究所",4}};
                else options = {{operation == 0 ? "购买" : "升级",1},{"放弃",0}};
                decision = GameDecision{operation == 0 ? DecisionKind::buy_facility : operation == 1 ? DecisionKind::construct_facility : DecisionKind::upgrade_facility,
                    actor(), static_cast<std::uint16_t>(id), cost, operation == 0 ? "购买设施用地？" : operation == 1 ? "选择要建设的设施" : "升级设施？", std::move(options)};
                set_phase(GamePhase::awaiting_decision);
            } else if (operation != 0 || ai_can_purchase(cost)) {
                const unsigned type = operation == 1 ? static_cast<unsigned>(random.next() % 4 + 1) : 0U;
                buy_facility(id, operation, type);
            } else finish_turn();
            return;
        }
        if (entry.owner_code > world.players.size() || !entry.level || !entry.facility_type || entry.facility_type >= 4) { finish_turn(); return; }
        const auto& owner = world.players[entry.owner_code - 1].status;
        if (owner.allied_player == actor() + 1 || owner.god_info == 15 || owner.days_in_hotel || owner.days_disappearing ||
            owner.days_in_prison || owner.days_in_hospital || owner.days_sleeping || owner.days_sleep_walking ||
            ((entry.temporary_state & 0xf0U) && (entry.temporary_state & 0x0fU))) { finish_turn(); return; }
        if (entry.facility_type == 3) { charge_facility(id, 1); return; }
        roulette_facility = id; roulette_cursor = random.next() % 12; roulette_elapsed = 0;
        if (!human() || status().days_sleep_walking) {
            charge_facility(id, roulette_value(entry.facility_type, roulette_cursor + 51)); return;
        }
        decision = GameDecision{DecisionKind::roulette, actor(), static_cast<std::uint16_t>(id), 0,
            entry.facility_type == 1 ? "转盘决定住宿天数，按停止选定" : "转盘决定消费倍数，按停止选定", {{"停止转盘",0}}};
        set_phase(GamePhase::awaiting_decision);
    }
    void rescue(std::size_t target, bool hospital, bool by_human) {
        auto& roster = hospital ? world.hospital_roster : world.prison_roster;
        if (target >= roster.size() || !roster[target]) return;
        const auto cost = target < 4 ? 30U : 300U;
        if (player().points < cost) return;
        if (target < world.players.size()) {
            auto& other = world.players[target];
            auto& field = hospital ? other.status.days_in_hospital : other.status.days_in_prison;
            if (by_human) update_hostility(target, actor(), -multiply32(multiply32(static_cast<std::int32_t>(std::max<unsigned>(field & 0x7fU, 1U)),100),world.header.price_index));
            field = 0x80;
        } else if (target >= 4) {
            auto& special = world.special_actors.at(target - 4);
            const auto exit = std::find_if(world.nodes.begin() + 1, world.nodes.end(), [&](const auto& node) { return (node.flags & 0xffU) == (hospital ? 5U : 4U); });
            if (exit == world.nodes.end()) throw std::runtime_error("rescue has no exit node");
            special.owner = static_cast<std::uint8_t>(actor()); special.inactive_state = 0;
            special.node_id = static_cast<std::uint16_t>(exit - world.nodes.begin()); special.last_node_id = 0;
            special.x = static_cast<std::uint16_t>(exit->x); special.y = static_cast<std::uint16_t>(exit->y);
            special.flags = static_cast<std::uint8_t>(0x80 | (hospital ? 2 : 1));
        } else return;
        roster[target] = 0; player().points = static_cast<std::uint16_t>(player().points - cost);
        emit(GameEventKind::notice, target < 4 ? "救援" + player_name(target) : "释放特殊角色", static_cast<std::int32_t>(cost));
    }
    void visit_confinement(bool hospital) {
        rescue_hospital = hospital;
        const auto& roster = hospital ? world.hospital_roster : world.prison_roster;
        if (std::none_of(roster.begin(), roster.end(), [](auto value) { return value != 0; })) { finish_turn(); return; }
        if (human()) {
            GameDecision prompt{DecisionKind::rescue,actor(),0,0,hospital ? "探望住院角色" : "探望入狱角色",{}};
            for (std::size_t i = 0; i < roster.size(); ++i) if (roster[i] && (i >= 4 || i < world.players.size()))
                prompt.options.push_back({(i < 4 ? "救援" + player_name(i) : "释放特殊角色" + std::to_string(i - 3)) + (i < 4 ? "（30点）" : "（300点）"),static_cast<std::int32_t>(i)});
            prompt.options.push_back({"离开",-1}); decision = std::move(prompt); set_phase(GamePhase::awaiting_decision); return;
        }
        if ((random.next() & 1U) == 0) { finish_turn(); return; }
        const auto strategy = status().rescue_strategy;
        bool normal = strategy == 0 || strategy == 1;
        bool special = strategy == 2 || (strategy == 1 && random.next() % 3 == 0);
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < 8; ++i) if (roster[i] && ((i < world.players.size() && normal) || (i >= 4 && special))) candidates.push_back(i);
        if (!candidates.empty()) {
            const auto target = candidates[random.next() % candidates.size()];
            if ((target < 4 && player().points > 30) || (target >= 4 && player().points >= 700)) rescue(target,hospital,false);
        }
        finish_turn();
    }
    void receive_random_card() {
        unsigned count = 0;
        for (auto amount : world.card_supply) count += amount;
        if (count) {
            unsigned ticket = random.next() % count;
            std::size_t card = 0;
            while (ticket >= world.card_supply.at(card)) ticket -= world.card_supply[card++];
            receive_card(player().cards,world.card_supply,services::card_prices,static_cast<std::uint8_t>(card + 1));
            const auto price = services::card_prices[card];
            if (price > 100) voice(actor(),0);
            else if (price > 50) voice(actor(),static_cast<std::uint8_t>(random.next() & 1U));
            else if (price) voice(actor(),2);
            emit(GameEventKind::notice,"获得卡片" + std::to_string(card + 1));
        }
        finish_turn();
    }
    std::int32_t pay(int payer_code, int recipient_code, std::int32_t requested, unsigned flags) {
        if (requested <= 0) return 0;
        auto paid = requested;
        if (payer_code > 100) {
            auto& company = world.commercials.at(static_cast<std::size_t>(payer_code - 100));
            company.funds = subtract32(company.funds, requested);
            company.funds_change = subtract32(company.funds_change, requested);
        } else {
            const auto slot = static_cast<std::size_t>(payer_code);
            auto& payer = world.players.at(slot).state;
            const auto available = std::max<std::int64_t>(0, static_cast<std::int64_t>(payer.cash) + payer.bank_balance);
            paid = static_cast<std::int32_t>(std::min<std::int64_t>(requested, available));
            auto& first = flags & 4U ? payer.bank_balance : payer.cash;
            auto& second = flags & 4U ? payer.cash : payer.bank_balance;
            const auto first_payment = std::min(std::max(first, 0), paid);
            first = subtract32(first, first_payment);
            second = subtract32(second, paid - first_payment);
            if (paid < requested) bankrupt(slot);
            world.players[slot].status.cumulative_spending += static_cast<std::uint32_t>(paid);
        }
        if (recipient_code == -1) world.public_fund = add32(world.public_fund, paid);
        else if (recipient_code > 100) {
            auto& company = world.commercials.at(static_cast<std::size_t>(recipient_code - 100));
            company.funds = add32(company.funds, paid);
            company.funds_change = add32(company.funds_change, paid);
        } else {
            auto& recipient = world.players.at(static_cast<std::size_t>(recipient_code));
            auto& account = flags & 1U ? recipient.state.cash : recipient.state.bank_balance;
            account = add32(account, paid);
            recipient.status.cumulative_income += static_cast<std::uint32_t>(paid);
        }
        return paid;
    }
    void update_hostility(std::size_t from, std::size_t target, std::int32_t delta) {
        if (from == target) return;
        auto& value = world.players.at(from).status;
        const auto old = std::bit_cast<std::int32_t>(value.hostility.at(target));
        if (delta < 0 && old == 0) return;
        value.hostility[target] = static_cast<std::uint32_t>(std::max(add32(old, delta), 0));
        if (delta > 0 && value.allied_player == target + 1) { value.allied_player = 0; value.allied_days = 0; }
    }
    void pay_toll(std::size_t owner, std::int32_t requested) {
        update_hostility(actor(), owner, requested / 100);
        int most_hostile = -1;
        std::int32_t largest_hostility = 0;
        for (std::size_t i = 0; i < world.players.size(); ++i) {
            if (i == actor() || !world.players[i].state.controller_flags) continue;
            const auto hostility = std::bit_cast<std::int32_t>(status().hostility[i]);
            if (hostility > largest_hostility) { largest_hostility = hostility; most_hostile = static_cast<int>(i); }
        }
        bool hostile_speech = false;
        if (most_hostile == static_cast<int>(owner) && requested >= multiply32(5000, world.header.price_index)) {
            hostile_speech = (random.next() & 1U) != 0;
            if (hostile_speech) voice(actor(), 18);
        }
        if (!hostile_speech) amount_voice(actor(), requested, 9);
        amount_voice(owner, requested, 6);
        const auto paid = pay(static_cast<int>(actor()), static_cast<int>(owner), requested, 0);
        if (paid) emit(GameEventKind::rent_paid, player_name(actor()) + "支付租金", paid);
    }
    void bankrupt(std::size_t slot) {
        auto& state = world.players.at(slot).state;
        const bool was_human = (state.controller_flags & 1U) != 0;
        state.controller_flags = 0;
        const auto& node = world.nodes.at(state.node_id);
        state.x = static_cast<std::uint16_t>(node.x); state.y = static_cast<std::uint16_t>(node.y);
        world.nodes[state.node_id].flags |= 0x100U << slot;
        world.clear_bankrupt_player_fields(slot);
        world.prison_roster[slot] = world.hospital_roster[slot] = 0;
        for (auto& other : world.players) {
            other.status.hostility[slot] = 0;
            if (other.status.allied_player == slot + 1) {
                other.status.allied_player = 0; other.status.allied_days = 0;
            }
        }
        events.push_back({GameEventKind::player_bankrupt, slot, 0, state.node_id, player_name(slot) + "破产"});
        unsigned active = 0, humans = 0;
        for (const auto& other : world.players) {
            if (other.state.controller_flags) ++active;
            if (other.state.controller_flags & 1U) ++humans;
        }
        if (!humans || (was_human && world.header.human_player_count == 1) || active == 1) {
            emit(GameEventKind::game_finished, humans ? "游戏结束，最后一位玩家获胜" : "游戏结束");
            set_phase(GamePhase::finished); return;
        }
        // The original retains levels/buildings when clearing their owner.
        for (auto& land : world.lands) if (land.economy.owner_code == slot + 1) {
            land.economy.owner_code = 0; land.expired_date = 0;
        }
        for (auto& facility : world.facilities) if (facility.owner_code == slot + 1) {
            facility.owner_code = 0; facility.expired_date = 0;
        }
        for (auto& commerce : world.commercials) if (commerce.owner_code == slot + 1) commerce.owner_code = 0;
        for (auto card : state.cards) if (card > 0 && card <= world.card_supply.size()) ++world.card_supply[card - 1];
        state.cards.fill(0);
        for (std::size_t i = 0; i < world.tool_supply.size(); ++i)
            world.tool_supply[i] = static_cast<std::uint8_t>(world.tool_supply[i] + state.tool_amounts[i]);
        state.tool_amounts.fill(0);
        emit(GameEventKind::unsupported_event, "破产拍卖、股票及特殊角色清理尚未完成");
    }
    void insure(std::size_t slot, std::int32_t amount) {
        if (!world.players.at(slot).status.days_assurance) return;
        for (std::size_t i = 1; i < world.commercials.size(); ++i)
            if (world.commercials[i].commerce_type == 4) {
                pay(static_cast<int>(i + 100), static_cast<int>(slot), amount, 1); return;
            }
    }
    void clear_absences(std::size_t slot) {
        auto& value = world.players.at(slot).status;
        if (value.days_in_prison) world.prison_roster[slot] = 0;
        if (value.days_in_hospital) world.hospital_roster[slot] = 0;
        value.days_in_hotel = value.days_disappearing = value.days_in_prison = value.days_in_hospital = 0;
    }
    void injure(std::size_t slot) {
        if (slot >= world.players.size()) return;
        auto& entry = world.players.at(slot);
        auto& state = entry.state;
        const auto& value = entry.status;
        if (!state.controller_flags || value.days_in_hotel || value.days_disappearing ||
            value.days_in_prison || value.days_in_hospital) return;
        if (state.traffic_method) {
            if ((state.traffic_method & 3U) == 1) ++world.tool_supply[4];
            else if ((state.traffic_method & 3U) == 2) ++world.tool_supply[5];
            state.traffic_method = 0; state.dice_count = 1;
        }
        state.controller_flags |= 0x40U;
    }
    void add_confinement(std::size_t slot, int days_left, bool hospital) {
        if (slot >= 4 && slot < 8) {
            auto& special = world.special_actors.at(slot - 4);
            world.nodes.at(special.node_id).flags &= ~(0x100U << slot);
            special.inactive_state = hospital ? 2 : 1;
            special.flags = special.days_sleeping = special.days_sleep_walking = 0;
            special.days_stopping = special.days_tortoise_walking = 0;
            (hospital ? world.hospital_roster : world.prison_roster)[slot] = 1;
            return;
        }
        if (slot >= world.players.size()) throw std::runtime_error("invalid confinement player");
        auto& entry = world.players[slot];
        auto& state = entry.state;
        auto& value = entry.status;
        auto& days_field = hospital ? value.days_in_hospital : value.days_in_prison;
        if (!days_field) {
            clear_absences(slot);
            absence_voice(slot, days_left);
            state.controller_flags &= 0x0fU;
            world.nodes.at(state.node_id).flags &= ~(0x100U << slot);
            const auto kind = hospital ? 5U : 4U;
            const auto target = std::find_if(world.nodes.begin() + 1, world.nodes.end(),
                [&](const auto& candidate) { return (candidate.flags & 0xffU) == kind; });
            if (target == world.nodes.end()) throw std::runtime_error("map has no confinement exit node");
            state.node_id = static_cast<std::uint16_t>(target - world.nodes.begin());
            state.last_node_id = 0; value.saved_direction = 15;
            const auto& building = world.landscapes.at(hospital ? 1 : 2);
            state.x = building.x; state.y = building.y;
            days_field = static_cast<std::uint8_t>(days_left);
            (hospital ? world.hospital_roster : world.prison_roster)[slot] = 1;
        } else days_field = static_cast<std::uint8_t>((days_field + days_left) & 0x7f);
        voice(slot, hospital ? 20 : 19);
        insure(slot, multiply32(multiply32(days_left, 2000), world.header.price_index));
        value.total_winter_sleep_days = static_cast<std::uint8_t>(value.total_winter_sleep_days + days_left);
        events.push_back({GameEventKind::notice, slot, days_left, state.node_id,
                          player_name(slot) + (hospital ? "住院" : "入狱") + std::to_string(days_left) + "天"});
    }
    void disappear(std::size_t slot, int days_left, int mode) {
        auto& entry = world.players.at(slot);
        auto& value = entry.status;
        const auto encoded = static_cast<std::uint8_t>((mode << 6) | days_left);
        if (!value.days_disappearing) {
            clear_absences(slot);
            absence_voice(slot, days_left);
            insure(slot, multiply32(multiply32(days_left, 2000), world.header.price_index));
            value.total_winter_sleep_days = static_cast<std::uint8_t>(value.total_winter_sleep_days + days_left);
            value.days_disappearing = encoded;
            auto& node = world.nodes.at(entry.state.node_id);
            node.flags &= ~(0x100U << slot);
            entry.state.x = static_cast<std::uint16_t>(node.x); entry.state.y = static_cast<std::uint16_t>(node.y);
        } else value.days_disappearing = static_cast<std::uint8_t>((value.days_disappearing & 0x3fU) + encoded);
        events.push_back({GameEventKind::notice, slot, days_left, entry.state.node_id,
                          player_name(slot) + "暂时离开" + std::to_string(days_left) + "天"});
    }
    int try_defense_card(std::size_t slot) {
        auto& entry = world.players.at(slot);
        if (has_card(entry.state.cards, 21)) {
            (void)consume_card(entry.state.cards, world.card_supply, 21);
            events.push_back({GameEventKind::notice, slot, 0, entry.state.node_id, player_name(slot) + "使用免罪卡"});
            return -1;
        }
        if (!has_card(entry.state.cards, 19)) return static_cast<int>(slot);
        int target = -1;
        if (entry.state.controller_flags == 1) {
            const auto key = choice_sequence++;
            const auto found = event_choices.find(key);
            if (found != event_choices.end()) target = found->second;
            else {
                GameDecision prompt{DecisionKind::card_target, slot, 0, 0, "使用嫁祸卡？", {}};
                for (std::size_t i = 0; i < world.players.size(); ++i)
                    if (i != slot && world.players[i].state.controller_flags)
                        prompt.options.push_back({"嫁祸给" + player_name(i), static_cast<std::int32_t>(i)});
                prompt.options.push_back({"保留卡片", -1});
                throw NeedChoice{std::move(prompt), key};
            }
        } else {
            std::int32_t hostility = 0;
            for (std::size_t i = 0; i < world.players.size(); ++i) {
                if (i == slot || !world.players[i].state.controller_flags) continue;
                const auto amount = std::bit_cast<std::int32_t>(entry.status.hostility[i]);
                if (amount > hostility) { hostility = amount; target = static_cast<int>(i); }
            }
            if (target == -1) {
                std::vector<std::size_t> eligible;
                for (std::size_t i = 0; i < world.players.size(); ++i) {
                    const auto& other = world.players[i]; const auto& state = other.status;
                    if (i != slot && other.state.controller_flags && !state.days_in_hotel &&
                        !state.days_disappearing && !state.days_in_prison && !state.days_in_hospital) eligible.push_back(i);
                }
                if (!eligible.empty()) target = static_cast<int>(eligible[random.next() % eligible.size()]);
            }
        }
        if (target >= 0) {
            (void)consume_card(entry.state.cards, world.card_supply, 19);
            events.push_back({GameEventKind::notice, slot, 0, entry.state.node_id,
                              player_name(slot) + "使用嫁祸卡，目标是" + player_name(static_cast<std::size_t>(target))});
            return target;
        }
        return static_cast<int>(slot);
    }
    std::uint8_t choose_victim_card(std::size_t slot, std::size_t victim) {
        const auto key = choice_sequence++;
        const auto found = event_choices.find(key);
        if (found != event_choices.end()) return static_cast<std::uint8_t>(found->second);
        GameDecision prompt{DecisionKind::choose_card, slot, 0, 0, "选择一张卡片", {}};
        for (auto card : world.players.at(victim).state.cards) if (card)
            if (std::none_of(prompt.options.begin(), prompt.options.end(), [&](const auto& option) { return option.value == card; }))
                prompt.options.push_back({std::string(services::card_names.at(card - 1)), card});
        if (prompt.options.empty()) return 0;
        prompt.options.push_back({"放弃", 0});
        throw NeedChoice{std::move(prompt), key};
    }
    void release_hotel_guests() {
        // Original 0x40dffa deliberately releases ALL hotels' guests.
        for (auto& entry : world.players)
            if (entry.state.controller_flags && entry.status.days_in_hotel) entry.status.days_in_hotel = 0x80;
    }
    void change_building(std::uint16_t code, bool upgrade) {
        if (code > 2000 && code < 4000) {
            auto& land = world.lands.at(code - 2000).economy;
            if (upgrade) {
                if ((!land.chain_store && land.level < 5) || (land.chain_store == 1 && land.level < 1)) ++land.level;
            } else if (land.level) {
                --land.level;
                if (land.chain_store) { land.chain_store = 0; land.level = 0; }
            }
        } else if (code > 4000 && code < 6000) {
            auto& entry = world.facilities.at(code - 4000);
            if (upgrade) {
                if (!entry.level) {
                    if (player().controller_flags & 6U) {
                        entry.facility_type = entry.owner_code == actor() + 1 ? static_cast<std::uint8_t>(random.next() % 4 + 1) : 0;
                    } else {
                        const auto key = choice_sequence++;
                        const auto answer = event_choices.find(key);
                        if (answer == event_choices.end()) throw NeedChoice{GameDecision{DecisionKind::construct_facility,actor(),static_cast<std::uint16_t>(code - 4000),0,
                            "选择魔法建设的设施",{{"旅馆",1},{"百货公司",2},{"加油站",3},{"研究所",4}}},key};
                        entry.facility_type = static_cast<std::uint8_t>(answer->second);
                    }
                    ++entry.level;
                } else {
                    static constexpr unsigned limits[]{1,5,5,1,5};
                    if (entry.facility_type < 5 && entry.level < limits[entry.facility_type]) ++entry.level;
                }
            } else if (entry.level) {
                --entry.level;
                if (!entry.level) { entry.facility_type = 0; release_hotel_guests(); }
            }
        }
    }
    void turn_around(std::size_t slot) {
        auto& state = world.players.at(slot).state;
        state.direction = static_cast<std::uint8_t>((state.direction + 4) & 7U);
        const auto& node = world.nodes.at(state.node_id);
        std::vector<std::uint16_t> choices;
        for (unsigned i = 0; i < 4; ++i)
            if (node.neighbors[i] && node.neighbors[i] != state.last_node_id && !(node.flags & (0x40000000U >> i))) choices.push_back(node.neighbors[i]);
        state.last_node_id = choices.empty() ? 0 : choices[random.next() % choices.size()];
    }
    void auction(std::uint16_t property_code, int seller = -1) {
        AuctionSession auction_session(world,random,property_code,seller);
        static constexpr const char* labels[]{"不加价","加价100","加价500","加价1000","加价5000","加价10000","退出拍卖"};
        for (unsigned count = 0; !auction_session.finished(); ++count) {
            if (count >= 100000) throw std::runtime_error("auction action limit reached");
            if (const auto bidder = auction_session.decision()) {
                const auto key = choice_sequence++;
                const auto answer = event_choices.find(key);
                if (answer == event_choices.end()) {
                    GameDecision prompt{DecisionKind::auction,bidder->player,property_code,bidder->price,"地产拍卖：当前出价" + std::to_string(bidder->price),{}};
                    for (auto action : bidder->actions) prompt.options.push_back({labels[static_cast<unsigned>(action)],static_cast<std::int32_t>(action)});
                    throw NeedChoice{std::move(prompt),key};
                }
                if (!auction_session.bid(static_cast<AuctionAction>(answer->second))) throw std::logic_error("cached auction bid no longer legal");
            } else if (!auction_session.tick()) throw std::logic_error("auction made no progress");
        }
        if (auction_session.result().winner) emit(GameEventKind::notice,"拍卖成交",auction_session.result().price);
        else emit(GameEventKind::notice,"地产拍卖流拍");
    }
    EventCallbacks event_callbacks() {
        EventCallbacks callbacks;
        callbacks.voice = [this](auto slot, auto category) { voice(slot, category); };
        callbacks.pay = [this](auto payer, auto recipient, auto amount, auto flags) { (void)pay(payer, recipient, amount, flags); };
        callbacks.add_prison = [this](auto slot, auto days_left) { add_confinement(slot, days_left, false); };
        callbacks.add_hospital = [this](auto slot, auto days_left) { injure(slot); add_confinement(slot, days_left, true); };
        callbacks.disappear = [this](auto slot, auto days_left, auto mode) { disappear(slot, days_left, mode); };
        callbacks.try_mianzui_jiahuo = [this](auto slot) { return try_defense_card(slot); };
        callbacks.choose_card = [this](auto slot, auto victim) { return choose_victim_card(slot, victim); };
        callbacks.auction = [this](auto property) { auction(property); };
        callbacks.sell_stock = [this](auto slot, auto stock, auto amount, auto bank) {
            (void)stocks::sell_stock(world, slot, stock, amount, bank);
        };
        callbacks.update_stocks = [this](auto stock) { (void)stocks::update_stocks(world, stock); };
        return callbacks;
    }
    void run_rule(std::function<void()> action, bool first_attempt = true) {
        if (first_attempt) event_choices.clear();
        choice_sequence = 0;
        World checkpoint = world;
        const auto checkpoint_random = random.state();
        const auto event_count = events.size();
        const auto old_phase = phase;
        const auto restore = [&] {
            world = std::move(checkpoint); random.seed(checkpoint_random); events.resize(event_count); phase = old_phase;
        };
        try {
            action();
            pending_continuation = {}; event_choices.clear();
            if (phase != GamePhase::finished) finish_turn();
        } catch (const NeedChoice& choice) {
            // Nested fortune/card/auction choices replay a transaction with its
            // answers, never its mutations or already-consumed random draws.
            restore();
            decision = choice.decision; pending_choice_key = choice.key;
            pending_continuation = [this, action] { run_rule(action, false); };
            set_phase(GamePhase::awaiting_decision);
        } catch (const UnsupportedRule& missing) {
            restore(); pending_continuation = {}; event_choices.clear(); unsupported(missing.message);
        }
    }
    void resolve_event(bool news) {
        run_rule([this, news] {
            const auto result = news ? apply_news(world, random, event_callbacks()) :
                                       apply_fortune(world, random, actor(), event_callbacks());
            if (!result.supported) throw UnsupportedRule{result.message};
            emit(GameEventKind::notice, result.message);
        });
    }
    void shop_prompt() {
        GameDecision prompt{DecisionKind::shop,actor(),0,0,"商店：选择购买或出售的物品",{}};
        for (auto card : shop->cards) prompt.options.push_back({"买 " + std::string(services::card_names.at(card - 1)) + " " + std::to_string(services::card_prices.at(card - 1)) + "点",100 + card});
        for (auto tool : shop->tools) prompt.options.push_back({"买 " + std::string(services::tool_names.at(tool - 1)) + " " + std::to_string(services::tool_prices.at(tool - 1)) + "点",200 + tool});
        for (auto card : player().cards) if (card && std::none_of(prompt.options.begin(),prompt.options.end(),[&](const auto& option){return option.value == 300 + card;}))
            prompt.options.push_back({"卖 " + std::string(services::card_names.at(card - 1)),300 + card});
        for (std::size_t tool = 0; tool < services::tool_names.size(); ++tool) if (player().tool_amounts[tool])
            prompt.options.push_back({"卖 " + std::string(services::tool_names[tool]),static_cast<std::int32_t>(401 + tool)});
        prompt.options.push_back({"离开",0}); decision = std::move(prompt); set_phase(GamePhase::awaiting_decision);
    }
    void open_shop(std::uint16_t property_code) {
        shop_property = property_code;
        shop = services::open_shop(world,random,actor(),property_code);
        if (human()) shop_prompt();
        else { services::close_shop(world,*shop,property_code); shop.reset(); finish_turn(); }
    }
    bool shop_action(int action, unsigned quantity) {
        if (!shop) return false;
        if (!action) { services::close_shop(world,*shop,shop_property); shop.reset(); finish_turn(); return true; }
        bool accepted = false;
        if (action > 100 && action <= 130) accepted = services::buy_card(world,*shop,actor(),static_cast<std::uint8_t>(action - 100));
        else if (action > 200 && action <= 213) accepted = services::buy_tool(world,*shop,actor(),static_cast<std::uint8_t>(action - 200));
        else if (action > 300 && action <= 330) accepted = services::sell_card(world,*shop,actor(),static_cast<std::uint8_t>(action - 300));
        else if (action > 400 && action <= 413) accepted = services::sell_tool(world,*shop,actor(),static_cast<std::uint8_t>(action - 400),quantity);
        if (accepted) { emit(GameEventKind::notice,action < 300 ? "购买完成" : "出售完成"); shop_prompt(); }
        return accepted;
    }
    services::MagicCallbacks magic_callbacks() {
        services::MagicCallbacks callbacks;
        callbacks.fortune = [this](auto slot) {
            const auto result = apply_fortune(world,random,slot,event_callbacks());
            if (!result.supported) throw UnsupportedRule{result.message};
            emit(GameEventKind::notice,result.message);
        };
        callbacks.defense = [this](auto slot){return try_defense_card(slot);};
        callbacks.hostility = [this](auto from,auto target,auto delta){update_hostility(from,target,delta);};
        callbacks.prison = [this](auto slot,auto duration){add_confinement(slot,static_cast<int>(duration),false);};
        callbacks.hospital = [this](auto slot,auto duration){add_confinement(slot,static_cast<int>(duration),true);};
        callbacks.upgrade = [this](auto property){change_building(property,true);};
        callbacks.downgrade = [this](auto property){change_building(property,false);};
        callbacks.turn_around = [this](auto slot){turn_around(slot);};
        callbacks.auction = [this](auto property,auto seller){auction(property,static_cast<int>(seller));};
        return callbacks;
    }
    void magic_effect(unsigned effect) {
        const auto category = magic_category;
        run_rule([this,category,effect] {
            const auto targets = services::magic_targets(world,category);
            if (!services::apply_magic(world,random,actor(),targets,effect,magic_callbacks()))
                throw UnsupportedRule{"这项魔法的效果尚未完成"};
            emit(GameEventKind::notice,std::string(services::magic_effects.at(effect)));
        });
    }
    void magic_effect_prompt(unsigned category) {
        magic_category = category;
        GameDecision prompt{DecisionKind::magic_effect,actor(),0,0,"选择魔法效果",{}};
        for (std::size_t i = 0; i < services::magic_effects.size(); ++i)
            prompt.options.push_back({std::string(services::magic_effects[i]),static_cast<std::int32_t>(i)});
        prompt.options.push_back({"重新选择角色",-1}); decision = std::move(prompt); set_phase(GamePhase::awaiting_decision);
    }
    void open_magic() {
        if (!human()) {
            const auto selected = services::choose_magic_ai(world,random,actor());
            if (!selected) { finish_turn(); return; }
            magic_category = selected->category; magic_effect(selected->effect); return;
        }
        GameDecision prompt{DecisionKind::magic_category,actor(),0,0,"选择魔法作用的角色",{}};
        for (std::size_t i = 0; i < services::magic_categories.size(); ++i)
            if (!services::magic_targets(world,static_cast<unsigned>(i)).empty()) prompt.options.push_back({std::string(services::magic_categories[i]),static_cast<std::int32_t>(i)});
        if (prompt.options.empty()) { finish_turn(); return; }
        decision = std::move(prompt); set_phase(GamePhase::awaiting_decision);
    }
    void open_bank(bool passing) {
        resume_movement = passing;
        if (status().days_rejected_by_bank) {
            emit(GameEventKind::notice, "银行暂停办理这位玩家的业务"); leave_bank(); return;
        }
        if (human()) {
            std::vector<DecisionOption> options{{"存款", 1}};
            if (!status().days_bank_no_loans) options.push_back({"提款", 2});
            options.push_back({"离开", 0});
            decision = GameDecision{DecisionKind::bank, actor(), 0, 0, "办理存款或提款", std::move(options)};
            set_phase(GamePhase::awaiting_decision);
        } else {
            const auto total = add32(player().cash, player().bank_balance);
            if (total > 0) {
                const float ratio = static_cast<float>(player().cash) / static_cast<float>(total);
                float target = static_cast<float>(status().init_cash_ratio) / 100.0F;
                if (world.header.date.day <= 7) target = static_cast<float>(static_cast<double>(target) * 1.5);
                else if (world.header.date.day >= 26) target = static_cast<float>(static_cast<double>(target) * 0.5);
                if (target >= 1.0F) target = 0.9F;
                if (target <= 0.0F) target = 0.1F;
                const float difference = ratio - target;
                if (difference >= 0.25F || difference <= -0.25F || player().cash == 0) {
                    player().cash = static_cast<std::int32_t>(static_cast<double>(total) * target);
                    player().bank_balance = subtract32(total, player().cash);
                    emit(GameEventKind::bank_transfer, "电脑玩家调整现金与存款");
                }
            }
            if (!passing) emit(GameEventKind::unsupported_event, "电脑贷款与特殊融资规则尚未完成");
            leave_bank();
        }
    }
    void leave_bank() {
        decision.reset();
        if (resume_movement && remaining > 0) { resume_movement = false; start_step(); }
        else { resume_movement = false; finish_turn(); }
    }
    void land() {
        const auto& node = world.nodes.at(player().node_id);
        const unsigned kind = node.flags & 0xffU;
        if (kind != 0 && status().days_sleep_walking) { finish_turn(); return; }
        if (kind == 0) {
            if (node.type_and_index > 2000 && node.type_and_index < 4000) property(node.type_and_index - 2000);
            else if (node.type_and_index > 4000 && node.type_and_index < 6000) facility(node.type_and_index - 4000);
            else if (node.type_and_index == 0) finish_turn();
            else unsupported("这个设施的规则尚未完成");
        } else if (kind == 1) finish_turn();
        else if (kind == 2 || kind == 3) resolve_event(kind == 2);
        else if (kind == 4 || kind == 5) visit_confinement(kind == 5);
        else if (kind == 13) receive_random_card();
        else if (kind == 15) open_shop(node.type_and_index);
        else if (kind == 16) open_magic();
        else if (kind >= 6 && kind <= 8) {
            // Original RICH4.CFG[1] == 0 mode: built-in auto result for each
            // minigame, including humans. Keep both reward and speech RNG draws.
            const auto reward = static_cast<std::uint16_t>(random.next() % 20 + 50);
            const auto category = static_cast<std::uint8_t>(random.next() & 1U);
            player().points = static_cast<std::uint16_t>(player().points + reward);
            emit(GameEventKind::points_received, "小游戏获得" + std::to_string(reward) + "点", reward);
            voice(actor(), category);
            finish_turn();
        }
        else if (kind >= 10 && kind <= 12) {
            const auto amount = kind == 10 ? 50 : kind == 11 ? 30 : 10;
            player().points = static_cast<std::uint16_t>(player().points + amount);
            if (kind == 10) voice(actor(), static_cast<std::uint8_t>(random.next() & 1U));
            else if (kind == 11) voice(actor(), 2);
            emit(GameEventKind::points_received, "获得" + std::to_string(amount) + "点", amount);
            finish_turn();
        } else if (kind == 14) open_bank(false);
        else {
            const char* name = kind == 2 ? "新闻" : kind == 3 ? "命运" : kind == 4 ? "监狱" :
                kind == 5 ? "医院" : kind == 9 ? "彩票" : kind == 13 ? "卡片" :
                kind == 15 ? "商店" : kind == 16 ? "魔法屋" : "特殊地点";
            unsupported(std::string(name) + "规则尚未完成；继续将略过本次事件");
        }
    }
    std::int32_t wealth(std::size_t slot) const { return stocks::player_wealth(world,slot); }
    void update_price_index() {
        std::int32_t total = 0, active = 0;
        for (std::size_t i = 0; i < world.players.size(); ++i) if (world.players[i].state.controller_flags) {
            total = add32(total, wealth(i)); ++active;
        }
        if (active && world.header.initial_fund > 0)
            world.header.price_index = std::max(world.header.price_index, (total / active) / world.header.initial_fund);
    }
    void close_month() {
        for (auto& entry : world.players) {
            auto& state = entry.state;
            if (state.controller_flags && state.loan == 0) {
                const auto value = static_cast<double>(state.bank_balance) * 1.1;
                // Real games stay in this range; malformed/overflowing states
                // are rejected instead of invoking a C++ float-to-int UB.
                if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max())
                    throw std::runtime_error("monthly interest exceeds signed cash range");
                state.bank_balance = static_cast<std::int32_t>(value);
            }
            entry.status.total_winter_sleep_days = 0;
            entry.status.cumulative_spending = entry.status.cumulative_income = 0;
        }
        emit(GameEventKind::bank_transfer, "月结完成，无贷款玩家获得存款利息");
    }
    void next_turn() {
        const auto previous = actor();
        std::size_t next = previous;
        bool wrapped = false;
        for (std::size_t count = 0; count < world.players.size(); ++count) {
            ++next;
            if (next == world.players.size()) { next = 0; wrapped = true; }
            if (world.players[next].state.controller_flags != 0) break;
        }
        if (world.players[next].state.controller_flags == 0) {
            emit(GameEventKind::game_finished, "游戏结束"); set_phase(GamePhase::finished); return;
        }
        world.header.current_player = static_cast<std::uint32_t>(next);
        if (wrapped) {
            const auto advance = calendar::next_day({world.header.date.year, world.header.date.month, world.header.date.day});
            const auto date = advance.date;
            world.header.date = {date.day, date.month, date.year};
            ++days;
            update_price_index();
            if (advance.month_changed) close_month();
            emit(GameEventKind::date_changed, std::to_string(date.year) + "年" + std::to_string(date.month) + "月" + std::to_string(date.day) + "日");
        }
        start_turn();
    }
};

GameSession::GameSession(World world, std::uint32_t seed) : impl_(std::make_unique<Impl>(std::move(world), seed)) {}
GameSession::~GameSession() = default;
GameSession::GameSession(GameSession&&) noexcept = default;
GameSession& GameSession::operator=(GameSession&&) noexcept = default;
const World& GameSession::world() const noexcept { return impl_->world; }
GamePhase GameSession::phase() const noexcept { return impl_->phase; }
const std::optional<GameDecision>& GameSession::pending_decision() const noexcept { return impl_->decision; }
const std::optional<MoveAnimation>& GameSession::animation() const noexcept { return impl_->animation; }
const std::vector<std::uint8_t>& GameSession::dice() const noexcept { return impl_->faces; }
std::uint32_t GameSession::random_state() const noexcept { return impl_->random.state(); }
int GameSession::steps_remaining() const noexcept { return impl_->remaining; }
std::uint64_t GameSession::completed_days() const noexcept { return impl_->days; }

bool GameSession::roll_dice() {
    if (impl_->phase != GamePhase::human_ready) return false;
    impl_->begin_roll(); return true;
}
bool GameSession::respond(std::size_t option, std::int32_t amount) {
    if (impl_->phase != GamePhase::awaiting_decision || !impl_->decision || option >= impl_->decision->options.size()) return false;
    const auto decision = *impl_->decision;
    const auto value = decision.options[option].value;
    if (impl_->pending_continuation) {
        impl_->event_choices[impl_->pending_choice_key] = value;
        impl_->decision.reset();
        auto continuation = std::move(impl_->pending_continuation);
        if (!continuation) throw std::logic_error("event choice lost its continuation");
        continuation(); return true;
    }
    if (decision.kind == DecisionKind::shop) return impl_->shop_action(value,amount > 0 ? static_cast<unsigned>(amount) : 1U);
    if (decision.kind == DecisionKind::magic_category) { impl_->magic_effect_prompt(static_cast<unsigned>(value)); return true; }
    if (decision.kind == DecisionKind::magic_effect) {
        if (value < 0) impl_->open_magic(); else impl_->magic_effect(static_cast<unsigned>(value));
        return true;
    }
    if (decision.kind == DecisionKind::bank) {
        auto& player = impl_->player();
        if (value == 0) { impl_->leave_bank(); return true; }
        if (amount <= 0) return false;
        if (value == 1 && amount <= player.cash) {
            player.cash = subtract32(player.cash, amount); player.bank_balance = add32(player.bank_balance, amount);
        } else if (value == 2 && amount <= player.bank_balance) {
            player.bank_balance = subtract32(player.bank_balance, amount); player.cash = add32(player.cash, amount);
        } else return false;
        impl_->emit(GameEventKind::bank_transfer, value == 1 ? "存款完成" : "提款完成", amount);
        return true;
    }
    impl_->decision.reset();
    if (decision.kind == DecisionKind::rescue) {
        if (value >= 0) {
            const auto needed = value < 4 ? 30 : 300;
            if (impl_->player().points < needed) { impl_->decision = decision; return false; }
            impl_->rescue(static_cast<std::size_t>(value),impl_->rescue_hospital,true);
        }
        impl_->finish_turn();
    } else if (decision.kind == DecisionKind::roulette) {
        const auto kind = impl_->world.facilities.at(decision.land_id).facility_type;
        impl_->charge_facility(decision.land_id,Impl::roulette_value(kind,impl_->roulette_cursor + 12));
    } else if (decision.kind == DecisionKind::buy_facility || decision.kind == DecisionKind::construct_facility || decision.kind == DecisionKind::upgrade_facility) {
        if (value == 0) impl_->finish_turn();
        else impl_->buy_facility(decision.land_id,decision.kind == DecisionKind::buy_facility ? 0 : decision.kind == DecisionKind::construct_facility ? 1 : 2,static_cast<unsigned>(value));
    } else if (decision.kind == DecisionKind::buy_land || decision.kind == DecisionKind::upgrade_land) {
        if (value == 1) impl_->buy(decision.land_id, decision.kind == DecisionKind::upgrade_land);
        else impl_->finish_turn();
    } else impl_->finish_turn();
    return true;
}
void GameSession::tick(unsigned elapsed_ms) {
    if (impl_->phase == GamePhase::awaiting_decision && impl_->decision && impl_->decision->kind == DecisionKind::roulette) {
        impl_->roulette_elapsed += elapsed_ms;
        impl_->roulette_cursor = (impl_->roulette_cursor + impl_->roulette_elapsed / 100) % 12;
        impl_->roulette_elapsed %= 100;
        const auto kind = impl_->world.facilities.at(impl_->roulette_facility).facility_type;
        impl_->decision->amount = Impl::roulette_value(kind,impl_->roulette_cursor);
        return;
    }
    // Consume only as much elapsed time as the current animation needs. The
    // bound prevents malformed worlds from spinning forever in one UI frame.
    for (unsigned transitions = 0; transitions < 4096; ++transitions) {
        auto& game = *impl_;
        if (game.phase == GamePhase::human_ready || game.phase == GamePhase::awaiting_decision || game.phase == GamePhase::finished) return;
        if (game.phase == GamePhase::advancing_turn) { game.next_turn(); continue; }
        const unsigned duration = game.phase == GamePhase::rolling ? roll_duration : step_duration;
        const unsigned needed = duration - game.phase_elapsed;
        const unsigned consumed = std::min(elapsed_ms, needed);
        game.phase_elapsed += consumed; elapsed_ms -= consumed;
        if (game.animation) game.animation->elapsed_ms = game.phase_elapsed;
        if (game.phase_elapsed < duration) return;
        if (game.phase == GamePhase::rolling) {
            if (game.remaining > 0) game.start_step(); else game.land();
        } else game.complete_step();
        if (!elapsed_ms) return;
    }
    throw std::runtime_error("native game exceeded one tick's transition limit");
}
std::vector<GameEvent> GameSession::drain_events() { auto output = std::move(impl_->events); impl_->events.clear(); return output; }
bool GameSession::can_save() const noexcept { return impl_->phase == GamePhase::human_ready || impl_->phase == GamePhase::finished; }
SaveFile GameSession::export_save() const {
    if (!can_save()) throw std::logic_error("save requires a settled native game turn");
    return SaveFile::decode(impl_->world.export_save());
}

} // namespace rich4
