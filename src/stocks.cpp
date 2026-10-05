// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Iru Cai's asm/rich4_stocks.asm, original entries 0x428e23,
// 0x428ec5, 0x429040 and 0x4294d5. No Windows ABI or host pointer fields.
#include "rich4/stocks.hpp"
#include "rich4/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace rich4::stocks {
namespace {

// Finite x87 arithmetic, rounded to its 64-bit significand after every
// operation. ARM long double is only binary64, so it cannot preserve the
// original halfway cases. All storage and rounding here use fixed-width
// integers; there is no host floating-point control word or assembler.
struct Extended {
    using Wide = __uint128_t;
    std::uint64_t significand{};
    int exponent{}; // value = signed significand * 2^exponent
    bool negative{};

    static Extended normalized(Wide value, int scale, bool sign = false) {
        if (value == 0) {
            return {};
        }
        unsigned length = 0;
        for (auto copy = value; copy; copy >>= 1) {
            ++length;
        }
        if (length > 64) {
            const auto shift = length - 64;
            const auto remainder = value & ((Wide{1} << shift) - 1);
            const auto halfway = Wide{1} << (shift - 1);
            value >>= shift;
            if (remainder > halfway || (remainder == halfway && (value & 1))) {
                ++value;
            }
            scale += static_cast<int>(shift);
            if (value == (Wide{1} << 64)) {
                value >>= 1;
                ++scale;
            }
        } else {
            value <<= 64 - length;
            scale -= static_cast<int>(64 - length);
        }
        return {static_cast<std::uint64_t>(value), scale, sign};
    }

    static Extended from(double value) {
        const auto raw = std::bit_cast<std::uint64_t>(value);
        const auto exponent_bits = (raw >> 52) & 2047U;
        if (exponent_bits == 2047) {
            throw std::invalid_argument("nonfinite original floating arithmetic");
        }
        const auto mantissa = (raw & 0xfffffffffffffULL) |
                              (exponent_bits == 0 ? 0 : 0x10000000000000ULL);
        return normalized(mantissa, exponent_bits == 0 ? -1074 : int(exponent_bits) - 1075,
                          (raw >> 63) != 0);
    }

    Extended operator-() const { return {significand, exponent, !negative}; }

    friend Extended operator+(Extended a, Extended b) {
        if (!a.significand) return b;
        if (!b.significand) return a;
        if (a.exponent < b.exponent || (a.exponent == b.exponent && a.significand < b.significand)) {
            std::swap(a, b);
        }
        const auto difference = static_cast<unsigned>(a.exponent - b.exponent);
        const Wide large = Wide{a.significand} << 3;
        Wide small;
        if (difference <= 3) {
            small = Wide{b.significand} << (3 - difference);
        } else if (difference >= 67) {
            small = 1; // Sticky bit: the discarded nonzero operand is retained.
        } else {
            const auto shift = difference - 3;
            small = b.significand >> shift;
            small |= (Wide{b.significand} & ((Wide{1} << shift) - 1)) != 0;
        }
        return normalized(a.negative == b.negative ? large + small : large - small,
                          a.exponent - 3, a.negative);
    }
    friend Extended operator-(Extended a, Extended b) { return a + -b; }
    friend Extended operator*(Extended a, Extended b) {
        return normalized(Wide{a.significand} * b.significand, a.exponent + b.exponent,
                          a.negative != b.negative);
    }
    friend Extended operator/(Extended a, Extended b) {
        if (!b.significand) throw std::invalid_argument("original floating division by zero");
        if (!a.significand) return {};
        const unsigned shift = a.significand >= b.significand ? 63 : 64;
        const Wide dividend = Wide{a.significand} << shift;
        Wide quotient = dividend / b.significand;
        const Wide doubled_remainder = (dividend % b.significand) * 2;
        if (doubled_remainder > b.significand ||
            (doubled_remainder == b.significand && (quotient & 1))) {
            ++quotient;
        }
        return normalized(quotient, a.exponent - b.exponent - static_cast<int>(shift),
                          a.negative != b.negative);
    }

    [[nodiscard]] float single() const {
        if (!significand) return 0;
        int power = exponent + 63;
        if (power < -126 || power > 127) {
            throw std::out_of_range("stock calculation outside normal binary32 range");
        }
        auto rounded = significand >> 40;
        const auto discarded = significand & ((std::uint64_t{1} << 40) - 1);
        const auto halfway = std::uint64_t{1} << 39;
        if (discarded > halfway || (discarded == halfway && (rounded & 1))) {
            ++rounded;
            if (rounded == 0x1000000) {
                rounded >>= 1;
                ++power;
            }
        }
        return std::bit_cast<float>((negative ? 0x80000000U : 0) |
                                    (static_cast<std::uint32_t>(power + 127) << 23) |
                                    static_cast<std::uint32_t>(rounded & 0x7fffffU));
    }

    [[nodiscard]] std::int32_t integer() const {
        if (!significand || exponent <= -64) return 0;
        if (exponent >= 0) return INT32_MIN;
        const auto magnitude = significand >> -exponent;
        if (magnitude > (negative ? 0x80000000U : 0x7fffffffU)) return INT32_MIN;
        return static_cast<std::int32_t>(negative ? -static_cast<std::int64_t>(magnitude)
                                                  : static_cast<std::int64_t>(magnitude));
    }
};

std::int32_t add_wrap(std::int32_t first, std::int32_t second) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(first) +
                                       std::bit_cast<std::uint32_t>(second));
}

// Exact positive binary32 price times signed32 nonnegative share count, then
// x87 FISTP's truncation to int32. Integer arithmetic avoids host FP precision
// differences. An overflowing conversion returns x87's integer-indefinite.
std::int32_t sale_proceeds(float quote, std::int32_t amount) {
    if (!std::isfinite(quote) || quote < 0 || amount < 0) {
        throw std::invalid_argument("stock sale requires a finite nonnegative quote and amount");
    }
    const auto raw = std::bit_cast<std::uint32_t>(quote);
    const auto exponent = (raw >> 23) & 255U;
    const std::uint64_t significand = (raw & 0x7fffffU) | (exponent == 0 ? 0 : 0x800000U);
    const auto product = significand * static_cast<std::uint32_t>(amount);
    const int shift = exponent == 0 ? -149 : int(exponent) - 150;
    std::uint64_t integer;
    if (product == 0) {
        return 0;
    }
    if (shift >= 0) {
        if (shift >= 31 || product > (std::uint64_t(INT32_MAX) >> shift)) {
            return INT32_MIN;
        }
        integer = product << shift;
    } else {
        integer = shift <= -64 ? 0 : product >> -shift;
    }
    return integer > INT32_MAX ? INT32_MIN : static_cast<std::int32_t>(integer);
}

Commercial* linked_commercial(World& world, std::size_t stock) {
    const auto id = world.stocks[stock].flags_4;
    if (id == 0) {
        return nullptr;
    }
    if (id >= world.commercials.size()) {
        throw std::invalid_argument("stock refers to an absent commercial");
    }
    auto& commercial = world.commercials[id];
    for (const auto owner : commercial.shareholder_order) {
        if (owner > world.players.size()) {
            throw std::invalid_argument("commercial shareholder rank refers to an absent player");
        }
    }
    return &commercial;
}

bool update_commercial_owner(World& world, Commercial* commercial,
                             std::size_t player, std::size_t stock) {
    if (!commercial) {
        return false;
    }
    auto& order = commercial->shareholder_order;
    const auto owner_code = static_cast<std::uint8_t>(player + 1);
    // Original memcpy moves at most three bytes forward within this four-byte
    // list. Spell out the overlap-safe operation rather than copy raw structs.
    if (const auto found = std::find(order.begin(), order.end(), owner_code); found != order.end()) {
        std::move(found + 1, order.end(), found);
        order.back() = 0;
    }
    const auto holding = world.players[player].stocks[stock].amount;
    if (holding != 0) {
        std::size_t insertion = 0;
        for (int index = 2; index >= 0; --index) {
            const auto position = static_cast<std::size_t>(index);
            const auto other = order[position];
            if (other == 0) {
                continue;
            }
            if (world.players[other - 1].stocks[stock].amount < holding) {
                order[position + 1] = other;
            } else {
                insertion = position + 1;
                break;
            }
        }
        order[insertion] = owner_code;
    }
    const bool changed = commercial->owner_code != order.front();
    commercial->owner_code = order.front();
    return changed; // The application refreshes its minimap; no UI call here.
}

} // namespace

float adjusted_price(float previous, float percent) {
    if (!std::isfinite(previous) || previous < 0 || previous > 10000 || !std::isfinite(percent) ||
        percent < -10 || percent > 10) {
        throw std::invalid_argument("stock price update is outside the finite original market domain");
    }
    // Explicit binary32 stores mirror the original stack slots. The tick is
    // selected using the unrounded candidate, and rounds the CHANGE relative
    // to the previous quote, not the absolute quote to a decimal grid.
    const float candidate = ((Extended::from(100) + Extended::from(percent)) /
                             Extended::from(100) * Extended::from(previous)).single();
    const float change = percent > 0 ? candidate - previous : previous - candidate;
    const double tick = candidate < 5 ? 0.01 : candidate < 15 ? 0.05 :
                        candidate < 50 ? 0.1 : candidate < 150 ? 0.5 : 1.0;
    const auto delta = Extended::from(change);
    const auto quantum = Extended::from(tick);
    const auto quotient = (delta / quantum).integer();
    const auto remainder = delta - quantum * Extended::from(quotient);
    const float rounded = (percent > 0 ? Extended::from(candidate) - remainder
                                      : Extended::from(candidate) + remainder).single();
    return std::clamp(rounded, 1.0F, 9999.0F);
}

SaleResult sell_stock(World& world, std::size_t player, std::size_t stock,
                      std::int32_t amount, bool credit_bank) {
    if (world.players.size() > 4 || player >= world.players.size() || stock >= world.stocks.size()) {
        throw std::out_of_range("stock sale player or stock index out of range");
    }
    auto& holding = world.players[player].stocks[stock];
    if (amount < 0 || amount > holding.amount) {
        throw std::invalid_argument("stock sale amount exceeds available holdings");
    }
    auto& company = world.stocks[stock];
    const auto proceeds = sale_proceeds(std::bit_cast<float>(company.float_bits[2]), amount);
    auto* commercial = linked_commercial(world, stock);
    holding.amount -= amount;
    if (holding.amount == 0) {
        holding.raw_cost_basis = 0;
    }
    // ADD word retains the original low 16 bits, independently of share count.
    company.shares_available = static_cast<std::uint16_t>(company.shares_available +
                                                         static_cast<std::uint32_t>(amount));
    company.flags_10 = static_cast<std::uint16_t>(company.flags_10 +
                                                 static_cast<std::uint32_t>(amount));
    if (credit_bank) {
        auto& bank = world.players[player].state.bank_balance;
        bank = add_wrap(bank, proceeds);
    } else {
        world.public_fund = add_wrap(world.public_fund, proceeds);
    }
    return {proceeds, update_commercial_owner(world, commercial, player, stock)};
}

std::size_t update_stocks(World& world, unsigned selection) {
    if (selection > world.stocks.size() || world.stock_history_cursor < 0 ||
        world.stock_history_cursor >= 144) {
        throw std::out_of_range("forced stock selection or history cursor out of range");
    }
    const auto previous_sample = world.stock_history_cursor == 0 ? 143 : world.stock_history_cursor - 1;
    const std::size_t first = selection == 0 ? 0 : selection - 1;
    const std::size_t end = selection == 0 ? world.stocks.size() : selection;
    std::size_t updated = 0;
    for (std::size_t index = first; index < end; ++index) {
        auto& stock = world.stocks[index];
        if (stock.news_movement == 0) {
            continue;
        }
        const float change = (stock.news_movement & 0xf0U) != 0 ? 10.0F : -10.0F;
        const auto quote = adjusted_price(std::bit_cast<float>(stock.float_bits[1]), change);
        stock.float_bits[4] = std::bit_cast<std::uint32_t>(change);
        stock.float_bits[2] = std::bit_cast<std::uint32_t>(quote);
        world.stock_history_bits[index][static_cast<std::size_t>(previous_sample)] = stock.float_bits[2];
        ++updated;
    }
    return updated;
}

std::int32_t player_wealth(const World& world, std::size_t player) {
    if (player >= world.players.size()) {
        throw std::out_of_range("wealth player index out of range");
    }
    const auto& account = world.players[player];
    auto total = add_wrap(account.state.cash, account.state.bank_balance);
    total = std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(total) -
                                       std::bit_cast<std::uint32_t>(account.state.loan));
    for (std::size_t stock = 0; stock < world.stocks.size(); ++stock) {
        const auto quote = std::bit_cast<float>(world.stocks[stock].float_bits[2]);
        const auto amount = account.stocks[stock].amount;
        if (!std::isfinite(quote) || quote < 0 || quote > 10000 || amount < 0 ||
            (quote != 0 && quote < 1)) {
            throw std::invalid_argument("wealth requires valid market quotes and nonnegative holdings");
        }
        // In the original FILD total / FSTP float loses low integer bits before
        // each stock addition, even for a zero holding. Preserve that sequence.
        const auto rounded_total = static_cast<std::int64_t>(static_cast<float>(total));
        const auto raw = std::bit_cast<std::uint32_t>(quote);
        const auto exponent = (raw >> 23) & 255U;
        const auto significand = (raw & 0x7fffffU) | (exponent == 0 ? 0 : 0x800000U);
        const auto product = std::uint64_t(significand) * static_cast<std::uint32_t>(amount);
        // Market quotes [1,10000] have 10..23 fraction bits. The integer product
        // is exact; split it so cancellation with negative wealth is also exact.
        const unsigned fraction_bits = quote == 0 ? 23U : 150U - exponent;
        const auto integer = product >> fraction_bits;
        const bool fraction = (product & ((std::uint64_t{1} << fraction_bits) - 1)) != 0;
        auto sum = rounded_total + static_cast<std::int64_t>(integer);
        if (sum < 0 && fraction) {
            ++sum; // Truncate the negative fractional sum toward zero.
        }
        total = sum < INT32_MIN || sum > INT32_MAX ? INT32_MIN : static_cast<std::int32_t>(sum);
    }
    const auto owner = player + 1;
    for (std::size_t id = 1; id < world.lands.size(); ++id) {
        const auto& land = world.lands[id];
        if (land.economy.owner_code == owner) {
            const auto levels = land.economy.chain_store != 0 ? 1 : land.economy.level;
            total = add_wrap(total, land.land_price + land.price_per_level * levels);
        }
    }
    for (std::size_t id = 1; id < world.facilities.size(); ++id) {
        const auto& facility = world.facilities[id];
        if (facility.owner_code == owner) {
            total = add_wrap(total, facility.land_price + facility.price_per_level() * facility.level);
        }
    }
    return total;
}

} // namespace rich4::stocks
