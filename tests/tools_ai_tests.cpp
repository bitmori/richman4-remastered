// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/tools_ai.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace rich4;
namespace {
void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Independent fixture serialization is in run_tools_ai_oracle.py. These are
// small map graphs, not copies of the predicates under test.
World fixture(unsigned scenario) {
    World world;
    world.players.resize(4);
    world.nodes.resize(13);
    world.lands.resize(9);
    world.facilities.resize(3);
    world.header.price_index = 1 + int(scenario % 5);
    world.camera_rotation = scenario % 8;
    for (unsigned i = 1; i <= 12; ++i) {
        auto &node = world.nodes[i];
        node.id = i;
        node.x = static_cast<std::int16_t>(960 + i * 32);
        node.y = 1024;
        node.neighbors = {static_cast<std::uint16_t>(i == 12 ? 1 : i + 1),
                          static_cast<std::uint16_t>(i == 1 ? 12 : i - 1), 0, 0};
        if (scenario % 7 == 0 && i == 8) {
            node.neighbors[2] = 3;
        }
        node.type_and_index = static_cast<std::uint16_t>(i <= 8 ? 2000 + i : i <= 10 ? 4000 + i - 8 : 0);
        node.flags = i == 11 ? 4 : i == 12 ? 5 : 0;
        if (scenario % 11 == 0 && i == 9) {
            node.flags |= 0x40000000U;
        }
    }
    for (unsigned i = 0; i < 4; ++i) {
        auto &player = world.players[i];
        player.state.slot = i;
        player.state.controller_flags = 2;
        player.state.x = static_cast<std::uint16_t>(1184 + i * 160);
        player.state.y = 1024;
        player.state.node_id = 7;
        player.state.last_node_id = 6;
        player.state.traffic_method = static_cast<std::uint8_t>(scenario % 4);
        player.state.cash = int(8000 + scenario * 911);
        player.state.bank_balance = 5000;
        player.state.points = 300;
        player.status.rescue_strategy = static_cast<std::uint8_t>(scenario % 3);
        player.status.card_strategy = 2;
        player.status.money_fortune_modifier = scenario % 13 == 0 ? -1 : 0;
        player.status.days_sleeping = scenario % 17 == 0 ? 1 : 0;
        player.status.days_in_hospital = scenario % 19 == 0 ? 1 : 0;
        player.status.god_info = scenario % 23 == 0 ? 7 : 0;
        player.status.hostility[(i + 1) % 4] = scenario % 2 == 0 ? 5 : 0;
    }
    for (unsigned i = 1; i <= 8; ++i) {
        auto &land = world.lands[i];
        land.id = i;
        land.x = static_cast<std::uint16_t>(960 + i * 32 + (scenario % 9 == 0 ? 700 : 0));
        land.y = static_cast<std::uint16_t>(1024 + (scenario % 9 == 0 ? 700 : 32));
        land.economy.street_name = i < 5 ? "First" : "Second";
        land.economy.owner_code = static_cast<std::uint8_t>((i + scenario) % 4);
        land.economy.level = static_cast<std::uint8_t>((i + scenario) % 6);
        land.economy.chain_store = scenario % 10 == 0 && i == 5 ? 1 : 0;
        land.land_price = static_cast<std::uint16_t>(1000 + i * 800);
        land.price_per_level = static_cast<std::uint16_t>(500 + i * 200);
        for (unsigned j = 0; j < 6; ++j) {
            land.economy.toll_by_level[j] = static_cast<std::uint16_t>(500 + i * 100 + j * 1800);
        }
    }
    for (unsigned i = 1; i <= 2; ++i) {
        auto &facility = world.facilities[i];
        facility.id = i;
        facility.x = static_cast<std::uint16_t>(1248 + i * 32);
        facility.y = 1056;
        facility.facility_type = static_cast<std::uint8_t>((scenario + i) % 5);
        facility.owner_code = static_cast<std::uint8_t>((scenario + i) % 3);
        facility.level = static_cast<std::uint8_t>((scenario + i) % 6);
        facility.land_price = 2000;
        for (unsigned j = 0; j < 6; ++j) {
            facility.toll_by_level[j] = static_cast<std::uint16_t>(1000 + j * 1000);
        }
    }
    if (scenario % 3 == 0) {
        world.nodes[8].flags |= 1U << 16;
        constexpr std::array<unsigned, 7> kinds{5, 8, 11, 16, 17, 18, 10};
        world.objects[0].kind = static_cast<std::uint8_t>(kinds[(scenario / 3) % kinds.size()]);
    }
    if (scenario >= 128) {
        world.players[0].status.rescue_strategy = 2;
        for (std::size_t i = 1; i < world.lands.size(); ++i) {
            world.lands[i].x = static_cast<std::uint16_t>(1700 + i * 32);
            world.lands[i].y = 1724;
            world.lands[i].economy.owner_code = 2;
        }
    }
    world.prison_roster[0] = scenario % 5 == 0 ? 1 : 0;
    world.hospital_roster[1] = scenario % 6 == 0 ? 1 : 0;
    return world;
}

tools_ai::Callbacks callbacks() {
    tools_ai::Callbacks result;
    result.nearby_instances = [](std::size_t) {
        return std::vector<std::uint16_t>{2001, 2002, 2003, 2004,   2005,   2006,   2007,
                                          2008, 4001, 4002, 0x8001, 0x8002, 0x8004, 0x8008};
    };
    return result;
}

void visits() {
    auto world = fixture(1);
    world.players[0].state.tool_amounts.fill(0);
    world.players[0].state.tool_amounts[9] = 4;
    Random random(1);
    require(!tools_ai::choose(world, random, 0).decision && random.state() == 1,
            "AI excludes time machine without drawing RNG");
    world.players[0].state.tool_amounts[4] = 1;
    world.players[0].state.traffic_method = 0;
    world.players[0].status.rescue_strategy = 2;
    for (unsigned seed = 0; seed < 200; ++seed) {
        Random expected(seed), actual(seed);
        bool used = false;
        for (unsigned attempt = 0; attempt < 4 && !used; ++attempt) {
            used = expected.next() % 4 == 0;
        }
        const auto result = tools_ai::choose(world, actual, 0);
        require(result.supported && bool(result.decision) == used && actual.state() == expected.state(),
                "A single held vehicle gets up to four wrapped attempts");
    }
    world.players[0].state.tool_amounts.fill(0);
    for (const unsigned tool : {1U, 5U, 6U, 8U, 12U}) {
        world.players[0].state.tool_amounts[tool - 1] = 1;
    }
    world.players[0].state.traffic_method = 3;
    world.players[0].state.cash = 0;
    world.players[0].state.bank_balance = 0;
    world.nodes[8].flags = 0;
    Random expected_start(9);
    (void)expected_start.next();
    random.seed(9);
    require(!tools_ai::choose(world, random, 0).decision && random.state() == expected_start.state(),
            "Five tool types draw one starting-index value even when every predicate fails");
    world.players[0].state.controller_flags = 1;
    random.seed(5);
    require(!tools_ai::choose(world, random, 0).decision && random.state() == 5,
            "Human visits never draw AI randomness");
    world.players[0].state.controller_flags = 2;
    world.players[0].status.card_strategy = 0;
    require(!tools_ai::choose(world, random, 0).decision && random.state() == 5, "Profile bit disables AI");
    world.players[0].status.rescue_strategy = 2;
    const auto missing = tools_ai::evaluate(world, random, 0, 9);
    require(!missing.supported && !missing.decision, "Missing instance scan is explicit");
}

void golden(const char *path) {
    std::ifstream file(path);
    require(bool(file), "Cannot open tool AI oracle CSV");
    std::string line;
    std::getline(file, line);
    unsigned cases = 0;
    std::array<unsigned, 13> successes{};
    while (std::getline(file, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        unsigned scenario, tool, seed, used, target, final_seed;
        require(bool(row >> scenario >> tool >> seed >> used >> target >> final_seed),
                "Malformed oracle row");
        const auto world = fixture(scenario);
        Random random(seed);
        const auto result = tools_ai::evaluate(world, random, 0, tool, callbacks());
        const unsigned actual_target =
            result.decision ? (tool == 11 ? result.decision->destination : result.decision->target) : 0;
        if (!result.supported || bool(result.decision) != bool(used) || (used && actual_target != target) ||
            random.state() != final_seed) {
            throw std::runtime_error(
                "Original mismatch scenario=" + std::to_string(scenario) + " tool=" + std::to_string(tool) +
                " used=" + std::to_string(bool(result.decision)) + "/" + std::to_string(used) +
                " target=" + std::to_string(actual_target) + "/" + std::to_string(target) +
                " seed=" + std::to_string(random.state()) + "/" + std::to_string(final_seed));
        }
        successes[tool - 1] += used;
        ++cases;
    }
    require(cases >= 13, "Incomplete tool AI oracle");
    std::cout << cases << " original tool predicates: decision/target/final RNG matched; successes";
    for (auto count : successes) {
        std::cout << ' ' << count;
    }
    std::cout << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        visits();
        if (argc > 1) {
            golden(argv[1]);
        }
        std::cout << "Tool AI profile, wrapped visits and missing-selection tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
