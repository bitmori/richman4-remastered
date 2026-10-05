// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/auction.hpp"
#include "rich4/calendar.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
rich4::World world(std::size_t count = 3) {
    rich4::World value;
    value.header.price_index = 1;
    value.header.date = {31, 1, 2000};
    value.players.resize(count);
    value.lands.resize(2);
    value.facilities.resize(2);
    value.lands[1].land_price = 1000;
    value.lands[1].economy.street_name = "road";
    value.facilities[1].land_price = 2000;
    for (std::size_t slot = 0; slot < count; ++slot) {
        auto &player = value.players[slot].state;
        player.slot = slot;
        player.controller_flags = 1;
        player.cash = 100000;
        player.bank_balance = 50000;
    }
    return value;
}
void start_price_and_eligibility() {
    auto value = world(4);
    value.lands[1].land_price = 1001;
    value.lands[1].economy.level = 3;
    value.header.price_index = 2;
    value.players[0].state.cash = 5004; // Equality is excluded despite a bank balance.
    value.players[1].state.cash = 5003;
    value.players[2].status.days_in_hospital = 0x80; // Release marker still excludes.
    rich4::Random random(47);
    rich4::AuctionSession session(value, random, 2001);
    require(session.initial_price() == 5004, "auction initial value must truncate before indexing");
    require(session.decision()->player == 3, "initial auction cash/status eligibility");
    require(random.state() == 47, "human-only setup consumes no economic random draws");
    require(session.bid(rich4::AuctionAction::raise_100), "single human can raise");
    require(session.finished() && session.result().winner == 3,
            "single eligible bidder wins after first raise");
    require(value.players[3].state.cash == 94896, "initial value plus increment debited");
    require(value.public_fund == 5104, "unowned auction payment credited to public fund");
}
void pass_raise_withdraw_and_settlement() {
    auto value = world();
    value.lands[1].economy.level = 2;
    value.lands[1].temporary_state = 0x12;
    rich4::Random random(9);
    rich4::AuctionSession session(value, random, 2001);
    require(session.decision()->player == 2, "original last eligible participant starts");
    require(session.bid(rich4::AuctionAction::pass), "temporary pass accepted");
    require(session.decision()->player == 0, "auction rotates in original record order");
    require(session.bid(rich4::AuctionAction::raise_100), "raise accepted");
    require(session.decision()->player == 1, "next participant after raise");
    require(session.bid(rich4::AuctionAction::withdraw), "permanent withdrawal accepted");
    require(!session.finished() && session.decision()->player == 2,
            "a raise must clear prior passes and let that player return");
    require(session.bid(rich4::AuctionAction::pass), "second pass accepted");
    require(session.finished() && session.result().winner == 0, "last bidder wins");
    require(value.players[0].state.cash == 97900 && value.players[0].state.bank_balance == 50000,
            "auction debits cash only");
    require(value.players[0].status.cumulative_spending == 2100 && value.public_fund == 2100,
            "auction accounting matches toll payment");
    require(value.lands[1].economy.owner_code == 1 && value.lands[1].economy.level == 2 &&
                value.lands[1].temporary_state == 0x12,
            "sale changes ownership without resetting property effects");
    require(!session.bid(rich4::AuctionAction::raise_100), "finished auction cannot pay twice");
}
void no_sale_and_invalid_actions() {
    auto value = world(2);
    value.players[0].state.cash = 1100;
    value.players[1].state.cash = 1100;
    value.lands[1].economy.owner_code = 2;
    rich4::Random random(13);
    rich4::AuctionSession session(value, random, 2001);
    require(!session.bid(rich4::AuctionAction::raise_500), "unaffordable bid must be rejected");
    require(!session.bid(static_cast<rich4::AuctionAction>(7)), "unknown action rejected");
    require(session.price() == 1000 && session.decision()->player == 1,
            "rejected bid must not advance turn or price");
    require(session.bid(rich4::AuctionAction::pass), "first pass");
    require(session.bid(rich4::AuctionAction::pass), "second pass");
    require(session.finished() && !session.result().winner, "all initial passes leave property unsold");
    require(value.lands[1].economy.owner_code == 2 && value.public_fund == 0,
            "unsold auction changes no owner/account");
    rich4::AuctionSession empty(value, random, 2001, 0);
    require(empty.bid(rich4::AuctionAction::raise_100), "bid using exactly all available cash is legal");
    require(value.players[1].state.cash == 0 && value.players[1].state.bank_balance == 50000,
            "exact cash bid does not borrow from savings");
    require(value.players[0].state.bank_balance == 51100 && value.players[0].status.cumulative_income == 1100,
            "seller receives bank income, not cash");
}
void leases_and_facilities() {
    auto value = world(1);
    value.lease_mode = 5;
    value.facilities[1].facility_type = 2;
    value.facilities[1].level = 3;
    rich4::Random random(1);
    rich4::AuctionSession session(value, random, 4001);
    require(session.initial_price() == 5000, "facility base/level used for auction price");
    require(session.bid(rich4::AuctionAction::raise_500), "facility bid accepted");
    require(value.facilities[1].expired_date == 0x07d0021fU,
            "lease uses original packed month addition, preserving February 31");
    require(value.facilities[1].level == 3 && value.facilities[1].facility_type == 2,
            "facility sale preserves construction");
    value.facilities[1].owner_code = 2;
    value.facilities[1].expired_date = 0x12345678;
    rich4::AuctionSession existing(value, random, 4001);
    require(existing.bid(rich4::AuctionAction::raise_100), "owned facility bid accepted");
    require(value.facilities[1].expired_date == 0x12345678, "resale does not restart lease");
}
void ai_draws_and_bid_limits() {
    auto value = world(1);
    value.players[0].state.controller_flags = 2;
    rich4::Random random(1), expected(1);
    const auto first = expected.next();
    (void)expected.next();
    const auto enthusiasm = static_cast<float>(static_cast<double>(first) / 32767.0 * 0.3 + 0.5);
    const auto expected_limit = static_cast<std::int32_t>(2000.0 * enthusiasm);
    rich4::AuctionSession session(value, random, 2001);
    require(random.state() == expected.state(), "eligible AI consumes exactly two valuation draws");
    require(session.ai_limit(0) == expected_limit, "all-unowned starting-land AI valuation");
    require(!session.decision(), "AI does not request a human decision");
    require(session.tick() && session.finished(), "single AI progresses without UI");
    require(session.result().winner == 0 && session.result().price == 1100,
            "single AI raises only 100 regardless of its maximum increment");
    require(random.state() == expected.state(), "economic bidding adds no random draws");

    // End-to-end adversarial streams: every run must terminate, conserve money,
    // and leave a winner who could afford the final price entirely in cash.
    for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
        auto battle = world(4);
        battle.header.price_index = static_cast<std::int32_t>(seed % 4 + 1);
        battle.lands[1].economy.level = static_cast<std::uint8_t>(seed % 6);
        for (std::size_t i = 0; i < battle.players.size(); ++i) {
            battle.players[i].state.controller_flags = 2;
            battle.players[i].state.cash = static_cast<std::int32_t>(2000 + seed * (i + 1));
        }
        std::int64_t before = 0;
        for (const auto &player : battle.players)
            before += player.state.cash;
        rich4::Random stream(seed);
        rich4::AuctionSession auction(battle, stream, 2001);
        unsigned steps = 0;
        while (!auction.finished() && steps++ < 1000)
            require(auction.tick(), "AI auction must progress");
        require(auction.finished(), "AI auction must terminate");
        std::int64_t after = battle.public_fund;
        for (const auto &player : battle.players) {
            after += player.state.cash;
            require(player.state.cash >= 0, "winner paid beyond available cash");
        }
        require(before == after, "auction must conserve cash plus public fund");
    }
}
} // namespace

int main() {
    try {
        start_price_and_eligibility();
        pass_raise_withdraw_and_settlement();
        no_sale_and_invalid_actions();
        leases_and_facilities();
        ai_draws_and_bid_limits();
        std::cout << "auction tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
