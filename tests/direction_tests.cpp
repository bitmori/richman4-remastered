// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/direction.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using rich4::direction::from_delta;
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void unit_tests() {
    check(from_delta(0, 0) == 2, "original zero-vector convention");
    constexpr std::array<std::array<int, 3>, 8> cardinals{
        {{0, 1, 0}, {1, 1, 1}, {1, 0, 2}, {1, -1, 3}, {0, -1, 4}, {-1, -1, 5}, {-1, 0, 6}, {-1, 1, 7}}};
    for (const auto &v : cardinals)
        check(from_delta(v[0], v[1]) == v[2], "cardinal direction");
    check(from_delta(2048, -848) == 2, "below positive rounding boundary");
    check(from_delta(2048, -849) == 3, "positive rounding tie");
    check(from_delta(2048, 849) == 2, "negative rounding tie");
    check(from_delta(2048, 850) == 1, "above negative rounding boundary");
    check(from_delta(2048000, -849000) == 3, "integer scale invariance");
    constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    check(from_delta(minimum, 0) == 6, "minimum x magnitude");
    check(from_delta(0, minimum) == 0, "original minimum y negation wrap");
    check(from_delta(minimum, minimum) == 7, "both minimum components");
    check(from_delta(maximum, minimum) == 1, "large-magnitude unsigned division");
    for (int y = -128; y <= 128; ++y) {
        for (int x = -128; x <= 128; ++x) {
            check(from_delta(x, y) < 8, "direction outside game range");
            check(from_delta(x, y) == from_delta(x * 10000, y * 10000), "exact scale invariance");
        }
    }
}

std::size_t check_original(const char *path) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open original direction CSV");
    std::string line;
    std::getline(input, line);
    std::size_t count = 0;
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::int32_t dx, dy;
        unsigned expected;
        if (!(row >> dx >> dy >> expected))
            throw std::runtime_error("Malformed direction golden row");
        if (from_delta(dx, dy) != expected)
            throw std::runtime_error("Direction oracle mismatch for (" + std::to_string(dx) + "," +
                                     std::to_string(dy) + ")");
        ++count;
    }
    check(count > 0, "Empty direction golden file");
    return count;
}
} // namespace

int main(int argc, char **argv) {
    try {
        unit_tests();
        std::cout << "Direction unit tests passed\n";
        if (argc > 1)
            std::cout << check_original(argv[1]) << " original direction rows matched\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Direction test failed: " << error.what() << '\n';
        return 1;
    }
}
