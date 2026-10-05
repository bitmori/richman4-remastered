// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/game.hpp"
#include "rich4/world.hpp"
#include "rich4/random.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
rich4::World world(unsigned count = 4) {
    std::vector<std::uint8_t> map(40);
    const std::array<unsigned, 5> counts{8, 1, 1, 0, 2}, strides{40, 52, 56, 52, 28};
    const auto put32 = [&](std::size_t offset, std::uint32_t integer) {
        for (unsigned i = 0; i < 4; ++i) map.at(offset + i) = static_cast<std::uint8_t>(integer >> (i * 8));
    };
    for (unsigned i = 0; i < 5; ++i) {
        const auto offset = map.size();
        put32(i * 8, counts[i]); put32(i * 8 + 4, static_cast<std::uint32_t>(offset));
        map.resize(offset + (counts[i] + 1) * strides[i]);
    }
    for (unsigned i = 1; i <= 8; ++i) {
        const auto offset = 40 + i * 40;
        put32(offset, (64 + i * 32) | (128U << 16));
        put32(offset + 24, (i == 8 ? 1U : i + 1) | ((i == 1 ? 8U : i - 1) << 16));
        put32(offset + 36, 1);
    }
    rich4::NewGameOptions options; options.players.resize(count);
    auto value = rich4::World::from_map_resource(map, options);
    value.header = {0x26, {5, 10, 2026}, 0, 0, count, 1, 0, 200000, 1};
    value.nodes.resize(9); value.lands.resize(2);
    for (unsigned i = 1; i < value.nodes.size(); ++i) {
        auto& node = value.nodes[i];
        node.id = i; node.x = static_cast<std::int16_t>(64 + i * 32); node.y = 128;
        node.neighbors = {static_cast<std::uint16_t>(i == 8 ? 1 : i + 1),
                          static_cast<std::uint16_t>(i == 1 ? 8 : i - 1), 0, 0};
        node.flags = 1;
    }
    value.players.resize(count);
    for (unsigned i = 0; i < count; ++i) {
        auto& p = value.players[i].state;
        p.slot = i; p.node_id = static_cast<std::uint16_t>(i + 1);
        p.last_node_id = static_cast<std::uint16_t>(i ? i : 8);
        p.x = static_cast<std::uint16_t>(value.nodes[p.node_id].x); p.y = 128;
        p.controller_flags = static_cast<std::uint8_t>(i ? 2 : 1);
        p.cash = 100000; p.bank_balance = 10000; p.dice_count = 1;
        value.nodes[p.node_id].flags |= 0x100U << i;
    }
    value.lands[1].id = 1;
    value.lands[1].land_price = 1000;
    value.lands[1].price_per_level = 400;
    value.lands[1].economy.street_name = "street";
    value.lands[1].economy.toll_by_level = {150, 300, 600, 1200, 2400, 4800};
    return rich4::World::from_save(rich4::SaveFile::decode(value.export_save()));
}
void property_nodes(rich4::World& value) {
    for (std::size_t i = 1; i < value.nodes.size(); ++i) {
        value.nodes[i].flags &= ~0xffU; value.nodes[i].type_and_index = 2001;
    }
}
void until(rich4::GameSession& game, const std::function<bool()>& done) {
    for (unsigned i = 0; i < 100000 && !done(); ++i) game.tick(1);
    require(done(), "state machine failed to reach expected state");
}
void first_landing(rich4::GameSession& game) {
    require(game.roll_dice(), "human roll rejected");
    until(game, [&] { return game.phase() == rich4::GamePhase::awaiting_decision ||
        game.phase() == rich4::GamePhase::advancing_turn || game.phase() == rich4::GamePhase::finished; });
}
void dice_and_paths() {
    rich4::GameSession game(world(), 1);
    rich4::Random expected(1);
    const unsigned face = expected.next() % 6 + 1;
    first_landing(game);
    for (unsigned i = 0; i < face; ++i) (void)expected.next();
    require(game.dice().size() == 1 && game.dice()[0] == face, "original die modulo differs");
    require(game.world().players[0].state.node_id == 1 + face, "walking distance differs");
    require(game.random_state() == expected.state(), "single path must consume one random call per step");
    auto value = world();
    value.players[0].state.last_node_id = 2;
    value.nodes[1].neighbors = {2, 3, 4, 0};
    value.nodes[1].flags |= 0x20000000U;
    rich4::GameSession blocked(std::move(value), 0);
    first_landing(blocked);
    require(blocked.world().players[0].state.node_id == 4, "last node or blocked edge was selected");
    value = world(); value.players[0].state.last_node_id = 2;
    value.nodes[1].neighbors = {2, 0, 0, 0};
    rich4::GameSession fallback(std::move(value), 0);
    first_landing(fallback);
    rich4::Random once(0); (void)once.next();
    require(fallback.world().players[0].state.node_id == 2 && fallback.random_state() == once.state(),
            "fallback must return to last node without consuming randomness");
}
void property_and_rent() {
    auto value = world(); property_nodes(value);
    value.header.price_index = 2; value.lands[1].economy.level = 2;
    rich4::GameSession buy(std::move(value), 0);
    first_landing(buy);
    require(buy.pending_decision()->amount == 3600, "purchase base and per-level fields reversed");
    require(buy.respond(0), "buy decision failed");
    require(buy.world().lands[1].economy.owner_code == 1 && buy.world().players[0].state.cash == 96400,
            "purchase did not update owner/cash");
    value = world(); property_nodes(value); value.lands[1].economy.owner_code = 1;
    rich4::GameSession upgrade(std::move(value), 0); first_landing(upgrade);
    require(upgrade.pending_decision()->kind == rich4::DecisionKind::upgrade_land &&
            upgrade.pending_decision()->amount == 400, "upgrade price differs");
    require(upgrade.respond(0) && upgrade.world().lands[1].economy.level == 1, "upgrade did not advance one level");
    value = world(); property_nodes(value); value.lands[1].economy.owner_code = 2;
    value.players[0].state.cash = 100; value.players[0].state.bank_balance = 1000;
    rich4::GameSession rent(std::move(value), 0); first_landing(rent);
    require(rent.world().players[0].state.cash == 0 && rent.world().players[0].state.bank_balance == 950,
            "rent must spend cash before bank");
    require(rent.world().players[1].state.bank_balance == 10150 && rent.world().players[1].state.cash == 100000,
            "rent must credit owner's bank, not cash");
    require(rent.world().players[0].status.cumulative_spending == 150 &&
            rent.world().players[1].status.cumulative_income == 150, "rent accounting differs");
    require(rent.world().lands[1].removed_building == 150, "last requested toll was not preserved");
    value = world(); property_nodes(value); value.lands[1].economy.owner_code = 2;
    value.players[1].status.god_info = 15;
    rich4::GameSession immune(std::move(value), 0); first_landing(immune);
    require(immune.world().players[0].state.cash == 100000, "owner god immunity ignored");
}
void bank_and_failure() {
    auto value = world(); for (auto& node : value.nodes) node.flags = (node.flags & ~0xffU) | 14;
    rich4::GameSession bank(std::move(value), 0); first_landing(bank);
    require(bank.pending_decision()->kind == rich4::DecisionKind::bank, "bank decision missing");
    require(!bank.respond(0, -10) && !bank.respond(0, 100001), "invalid bank amount accepted");
    require(bank.respond(0, 1234), "integer deposit rejected");
    require(bank.world().players[0].state.cash == 98766 && bank.world().players[0].state.bank_balance == 11234,
            "deposit changed total funds");
    require(bank.respond(1, 234) && bank.world().players[0].state.cash == 99000, "withdrawal failed");
    require(bank.respond(2), "bank exit failed");
    value = world(); property_nodes(value); value.lands[1].economy.owner_code = 2;
    value.players[0].state.cash = 40; value.players[0].state.bank_balance = 20;
    rich4::GameSession bankrupt(std::move(value), 0); first_landing(bankrupt);
    require(bankrupt.phase() == rich4::GamePhase::finished && !bankrupt.world().players[0].state.controller_flags,
            "sole human bankruptcy must end game");
    require(bankrupt.world().players[1].state.bank_balance == 10060 &&
            bankrupt.world().players[0].status.cumulative_spending == 60, "bankruptcy must transfer actual liquid funds");
}
void thirty_days_and_timing() {
    rich4::GameSession small(world(), 7), large(world(), 7);
    auto run = [](rich4::GameSession& game, unsigned delta) {
        unsigned bound = 0;
        while (game.completed_days() < 30 && ++bound < 100000) {
            if (game.phase() == rich4::GamePhase::human_ready) require(game.roll_dice(), "autoplay roll failed");
            require(!game.pending_decision(), "plain roads unexpectedly need a decision");
            game.tick(delta);
        }
        require(game.completed_days() == 30 && game.can_save(), "thirty days did not return to human input");
    };
    run(small, 17); run(large, 100000);
    require(small.random_state() == large.random_state(), "animation timing changed RNG sequence");
    for (std::size_t i = 0; i < 4; ++i) {
        require(small.world().players[i].state.node_id == large.world().players[i].state.node_id,
                "animation timing changed final nodes");
        require(small.world().players[i].state.bank_balance == 11000, "month boundary bank interest differs");
    }
    require(small.world().header.date.day == 4 && small.world().header.date.month == 11,
            "date must advance once per complete round");
}
void save_reload_and_deferred_spawn() {
    auto value = world();
    for (auto& entry : value.players) {
        entry.state.x = entry.state.y = entry.state.node_id = entry.state.last_node_id = 0;
    }
    for (auto& node : value.nodes) node.flags &= ~0x0f00U;
    rich4::GameSession spawn(std::move(value), 11);
    rich4::Random expected(11); (void)expected.next(); (void)expected.next();
    require(spawn.world().players[0].state.node_id != 0 && spawn.world().players[1].state.node_id == 0,
            "only the player's first turn should spawn it");
    require(spawn.random_state() == expected.state(), "spawn must consume node and neighbor random calls");
    auto saved = spawn.export_save();
    auto reloaded = rich4::World::from_save(saved);
    require(reloaded.export_save() == saved.encode(), "native settled save lost original fields");
    rich4::GameSession resume(std::move(reloaded), spawn.random_state());
    require(resume.world().players[0].state.node_id == spawn.world().players[0].state.node_id &&
            resume.random_state() == spawn.random_state(), "reload must not respawn the current player");
    require(spawn.roll_dice() && resume.roll_dice(), "reloaded human cannot roll");
    until(spawn, [&] { return spawn.completed_days() == 1; });
    until(resume, [&] { return resume.completed_days() == 1; });
    require(spawn.export_save().encode() == resume.export_save().encode(),
            "explicit RNG seed plus settled save must continue identically");
}
void facilities_events_and_rescue() {
    auto value = world();
    for (auto& node : value.nodes) { node.flags &= ~0xffU; node.type_and_index = 4001; }
    value.facilities[1].land_price = 4000;
    value.facilities[1].toll_by_level = {400,800,1600,3200,6400,12800};
    rich4::GameSession purchase(value,0); first_landing(purchase);
    require(purchase.pending_decision()->kind == rich4::DecisionKind::buy_facility && purchase.pending_decision()->amount == 4000,
            "unowned facility price/decision differs");
    require(purchase.respond(0) && purchase.world().facilities[1].owner_code == 1 && purchase.world().players[0].state.cash == 96000,
            "facility purchase failed");
    value.facilities[1].owner_code = 1;
    rich4::GameSession construct(value,0); first_landing(construct);
    require(construct.pending_decision()->kind == rich4::DecisionKind::construct_facility, "facility construction choice missing");
    require(construct.respond(1) && construct.world().facilities[1].facility_type == 2 && construct.world().facilities[1].level == 1,
            "selected department-store construction failed");
    value.facilities[1].owner_code = 2; value.facilities[1].facility_type = 2; value.facilities[1].level = 1;
    rich4::GameSession shopping(value,0); first_landing(shopping);
    require(shopping.pending_decision()->kind == rich4::DecisionKind::roulette, "department roulette missing");
    const auto prior = shopping.random_state();
    shopping.tick(350); require(shopping.respond(0), "roulette stop rejected");
    require(shopping.world().players[0].state.cash < 100000 && shopping.world().players[1].state.bank_balance > 10000,
            "roulette did not charge the selected original multiplier");
    require(prior == shopping.random_state(), "low-fee roulette animation must not consume random calls");

    value = world();
    for (auto& node : value.nodes) node.flags = (node.flags & ~0xffU) | 3;
    value.fortune_deck.fill(14); value.fortune_cursor = 0;
    rich4::GameSession expense(value,0); first_landing(expense);
    require(expense.world().players[0].state.cash == 97000 && expense.world().fortune_cursor == 1,
            "fortune deck expense was not applied exactly once");
    value.fortune_deck.fill(5); value.players[1].state.cards[0] = 3;
    rich4::GameSession card(value,0); first_landing(card);
    require(card.pending_decision()->kind == rich4::DecisionKind::choose_card && card.world().fortune_cursor == 0,
            "nested card choice should suspend without committing the event");
    require(card.respond(0) && card.world().players[0].state.cards[0] == 3 && card.world().players[1].state.cards[0] == 0 &&
            card.world().fortune_cursor == 1, "choice replay duplicated or lost card transfer");

    value = world();
    for (auto& node : value.nodes) node.flags = (node.flags & ~0xffU) | 4;
    value.prison_roster.fill(0); value.prison_roster[1] = 1;
    value.players[1].status.days_in_prison = 5; value.players[0].state.points = 50;
    rich4::GameSession rescue(value,0); first_landing(rescue);
    require(rescue.pending_decision()->kind == rich4::DecisionKind::rescue && rescue.respond(0), "prison rescue input failed");
    require(rescue.world().players[0].state.points == 20 && rescue.world().players[1].status.days_in_prison == 0x80 &&
            rescue.world().prison_roster[1] == 0, "prison rescue must spend points and defer original release");
}
void explicit_unsupported_and_save_guard() {
    auto value = world(); for (auto& node : value.nodes) node.flags = (node.flags & ~0xffU) | 255;
    rich4::GameSession game(std::move(value), 0);
    require(game.roll_dice(), "roll failed");
    bool rejected = false;
    try { (void)game.export_save(); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "saving an in-flight move must not lose queued state");
    until(game, [&] { return game.pending_decision().has_value(); });
    bool explicit_gap = false;
    for (const auto& event : game.drain_events()) if (event.kind == rich4::GameEventKind::unsupported_event) explicit_gap = true;
    require(explicit_gap, "unported compulsory event must never silently pass");
}
}
int main() {
    try {
        dice_and_paths(); property_and_rent(); bank_and_failure();
        thirty_days_and_timing(); save_reload_and_deferred_spawn(); facilities_events_and_rescue(); explicit_unsupported_and_save_guard();
        std::cout << "native game rule tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "native game tests: " << error.what() << '\n'; return 1;
    }
}
