// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rich4::direction {
// Returns the game's eight-direction index from screen-coordinate deltas.
// Preserves the original integer angle rounding and (0,0) => 2 convention.
// All int32 inputs are supported, including original wrapping negation of dy.
[[nodiscard]] std::uint8_t from_delta(std::int32_t dx, std::int32_t dy) noexcept;
} // namespace rich4::direction
