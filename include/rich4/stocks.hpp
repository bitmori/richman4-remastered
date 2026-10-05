// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace rich4 {
class World;

namespace stocks {

struct SaleResult {
    std::int32_t proceeds{};
    bool commercial_owner_changed{};
};

// Original 0x428ec5: percentage movement, price-band tick rounding, and bounds
// [1,9999]. Prices and percentages must be finite; movement is in [-10,10].
[[nodiscard]] float adjusted_price(float previous_price, float percent);

// Original 0x428e23. Player and stock indices are zero based; amount must be
// nonnegative and no greater than the holding. true credits the BANK account,
// false credits the public/liquidation fund (original 0x499080).
SaleResult sell_stock(World& world, std::size_t player, std::size_t stock,
                      std::int32_t amount, bool credit_bank);

// Original 0x429040: apply news/card limit-up or limit-down flags. Zero selects
// all twelve stocks; 1..12 selects one. Uses previous price, overwrites the last
// history sample, and does not advance the history cursor or consume randomness.
// This is NOT the daily random market update at 0x4291d6.
std::size_t update_stocks(World& world, unsigned one_based_or_zero_all);

// Original 0x4239b9: cash + bank - loan, stock value with the original per-stock
// binary32 accumulator conversion, and owned land/facility acquisition values.
// Does not multiply property value by the price index.
[[nodiscard]] std::int32_t player_wealth(const World& world, std::size_t player);

} // namespace stocks
} // namespace rich4
