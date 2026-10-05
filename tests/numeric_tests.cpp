// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/legacy_float.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using rich4::numeric::Extended;

void check(bool condition, const char *description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}

Extended expression(unsigned operation, double a, double b, double c) {
    switch (operation) {
    case 0:
        return (Extended::from(a) + Extended::from(b)) * Extended::from(c);
    case 1:
        return (Extended::from(a) - Extended::from(b)) / Extended::from(c);
    case 2:
        return (Extended::from(a) * Extended::from(b)) + Extended::from(c);
    case 3:
        return (Extended::from(a) / Extended::from(b)) - Extended::from(c);
    default:
        throw std::runtime_error("unknown numeric probe operation");
    }
}

void unit_tests() {
    const auto one = Extended::from(1);
    const auto large = Extended::from(0x1p53);
    check(((large + one) - large).integer() == 1, "preserve integer bit beyond binary64 precision");
    check((large + one).double_precision() == 0x1p53, "binary64 midpoint ties to even");
    check((large + Extended::from(3)).double_precision() == 0x1p53 + 4,
          "binary64 midpoint odd rounds upward");
    const auto huge = Extended::from(0x1p64);
    check(((huge + one) - huge).integer() == 0, "arithmetic rounds at x87 precision");
    check(((huge + Extended::from(3)) - huge).integer() == 4, "x87 midpoint ties to even");
    const auto midpoint = one + Extended::from(0x1p-24);
    const auto extra_bit = Extended::from(0x1p-63);
    check(std::bit_cast<std::uint32_t>((midpoint + extra_bit).single()) == 0x3f800001,
          "binary32 spill retains extended bit above midpoint");
    check(std::bit_cast<std::uint32_t>((midpoint - extra_bit).single()) == 0x3f800000,
          "binary32 spill below midpoint");
    const auto double_midpoint = one + Extended::from(0x1p-53);
    check(std::bit_cast<std::uint64_t>((double_midpoint + extra_bit).double_precision()) ==
              0x3ff0000000000001ULL,
          "binary64 spill retains extended bit above midpoint");
    check((-Extended::from(100.9)).integer() == -100, "negative integer conversion truncates toward zero");
    check(Extended::from(2147483648.0).integer() == INT32_MIN, "positive integer overflow is x87 indefinite");
    check(Extended::from(-2147483648.0).integer() == INT32_MIN, "int32 minimum is valid");
    check((one / Extended::from(3)).single() == 1.0F / 3.0F, "binary32 division spill");
    bool rejected = false;
    try {
        (void)(one / Extended::from(0));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "division by zero is a defined boundary");
}

void compare_probe(const char *path) {
    std::ifstream file(path);
    check(bool(file), "cannot open independent x87 probe results");
    std::string line;
    std::getline(file, line);
    std::size_t count = 0;
    while (std::getline(file, line)) {
        std::istringstream input(line);
        std::vector<std::uint64_t> row;
        for (std::string field; std::getline(input, field, ',');) {
            row.push_back(std::stoull(field));
        }
        check(row.size() == 7, "incomplete numeric probe record");
        const auto value = expression(static_cast<unsigned>(row[0]), std::bit_cast<double>(row[1]),
                                      std::bit_cast<double>(row[2]), std::bit_cast<double>(row[3]));
        if (std::bit_cast<std::uint32_t>(value.single()) != row[4] ||
            std::bit_cast<std::uint64_t>(value.double_precision()) != row[5] ||
            std::bit_cast<std::uint32_t>(value.integer()) != row[6]) {
            std::cerr << "numeric probe mismatch at record " << count << '\n';
            throw std::runtime_error("independent x87 arithmetic differs");
        }
        ++count;
    }
    check(count == 40000, "numeric probe is truncated");
    std::cout << "Independent x87 expressions: " << count << ", mismatches: 0\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        unit_tests();
        if (argc == 2) {
            compare_probe(argv[1]);
        } else if (argc != 1) {
            throw std::runtime_error("usage: numeric_tests [x87-probe.csv]");
        }
        std::cout << "Numeric tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
