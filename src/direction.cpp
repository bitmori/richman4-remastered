// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from mytbk/rich4 asm/rich4_calculate_direction.asm; see migration 004.
#include "rich4/direction.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace rich4::direction {
namespace {
constexpr std::array<std::uint8_t, 8> original_direction_order{2,3,4,5,6,7,0,1};

std::uint32_t magnitude(std::uint32_t bits) {
    return (bits & 0x80000000U) != 0 ? 0U - bits : bits;
}
} // namespace

std::uint8_t from_delta(std::int32_t dx, std::int32_t dy) noexcept {
    const auto x_bits = static_cast<std::uint32_t>(dx);
    // This intentionally retains x86 NEG's wrap at INT32_MIN. Signed -dy or
    // std::abs(INT32_MIN) would have undefined behavior in portable C++.
    const auto y_bits = 0U - static_cast<std::uint32_t>(dy);
    if ((x_bits | y_bits) == 0) {
        return 2;
    }
    const auto x = magnitude(x_bits);
    const auto y = magnitude(y_bits);
    const auto ratio = (static_cast<std::uint64_t>(std::min(x, y)) * 2048U) / std::max(x, y);

    // Only the final eight-way direction is observable. The original monotonic
    // 2049-entry angle table crosses 22.5 degrees at exactly index 849:
    // table[848]=1023, [849]=1024, [850]=1025 (quarter-angle units).
    // These representative angles preserve below/equal/above boundary results
    // in every quadrant, including the asymmetric tie from integer rounding.
    std::uint32_t angle = ratio < 849 ? 0U : ratio == 849 ? 0x1000U : 0x2000U;
    if (x < y) {
        angle = 0x4000U - angle;
    }
    const auto x_negative = (x_bits & 0x80000000U) != 0;
    const auto y_negative = (y_bits & 0x80000000U) != 0;
    if (x_negative) {
        angle = y_negative ? 0x8000U + angle : 0x8000U - angle;
    } else if (y_negative) {
        angle = 0U - angle;
    }
    angle &= 0xffffU; // Original quadrant operations affect AX only.
    const auto octant = (((angle >> 12U) + 1U) >> 1U) & 7U;
    return original_direction_order[octant];
}
} // namespace rich4::direction
