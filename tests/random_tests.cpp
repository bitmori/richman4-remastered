// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/random.hpp"
#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::array<std::uint32_t, 4> parse_row(std::string_view line) {
    std::array<std::uint32_t, 4> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), values[i]);
        if (error != std::errc{}) throw std::runtime_error("invalid RNG golden number");
        line.remove_prefix(static_cast<std::size_t>(end - line.data()));
        if (i + 1 < values.size()) {
            if (line.empty() || line.front() != ',') throw std::runtime_error("invalid RNG golden delimiter");
            line.remove_prefix(1);
        }
    }
    if (line != "" && line != "\r") throw std::runtime_error("unexpected RNG golden data");
    return values;
}
void compare_original(const char* path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("missing original RNG vectors");
    std::string line;
    std::getline(input, line);
    if (line != "seed,index,return,state" && line != "seed,index,return,state\r")
        throw std::runtime_error("unexpected RNG golden header");
    rich4::Random random(0);
    std::uint32_t seed{}, step{};
    std::size_t cases{}, streams{};
    while (std::getline(input, line)) {
        const auto row = parse_row(line);
        if (row[1] == 0) { random.seed(row[0]); seed = row[0]; step = 0; ++streams; }
        check(row[0] == seed && row[1] == step, "nonsequential original RNG corpus");
        check(random.next() == row[2], "original RNG result differs");
        check(random.state() == row[3], "original RNG state differs");
        ++step; ++cases;
    }
    check(streams >= 8 && cases >= 8192, "incomplete original RNG corpus");
    std::cout << cases << " original random steps matched\n";
}
}

int main(int argc, char** argv) {
    try {
        rich4::Random random(1);
        for (const auto expected : {16838, 5758, 10113, 17515, 31051})
            check(random.next() == expected, "Watcom known sequence");
        rich4::Random zero(0);
        check(zero.next() == 0 && zero.state() == 12345, "seed zero must stay zero until first advance");
        rich4::Random first(0xffffffffU), second(0xffffffffU);
        for (int i = 0; i < 10000; ++i) {
            check(first.next() == second.next(), "game streams must be independent");
            check(first.state() == second.state(), "game stream state changed unexpectedly");
        }
        auto copy = first;
        check(copy.next() == first.next(), "copied state must resume the same stream");
        first.seed(1);
        check(first.next() == 16838, "reseed must reset the full state");
        if (argc == 2) compare_original(argv[1]);
        else if (argc != 1) throw std::runtime_error("usage: random_tests [random.csv]");
        std::cout << "Random tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "random: " << error.what() << '\n';
        return 1;
    }
}
