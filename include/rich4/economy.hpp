// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rich4 {

// Only fields read by the migrated land-toll routines. Name bytes retain their
// original encoding (usually Big5); no Unicode normalization or locale compare.
struct LandEconomyRecord {
    std::string street_name;
    std::uint8_t chain_store = 0; // Any nonzero value is a chain store.
    std::uint8_t owner_code = 0;  // Original value: player slot + 1; zero is unowned.
    std::uint8_t level = 0;
    std::array<std::uint16_t, 6> toll_by_level{};
};

// Read the relevant fields from one original 52-byte land record. This is not a
// full land serializer; SaveFile preserves fields outside this economy subset.
[[nodiscard]] LandEconomyRecord decode_land_economy_record(std::span<const std::uint8_t> bytes);

// The span contains real records only: exclude the original index-zero sentinel.
// Owner is already the original one-based value; no implicit +1 is performed.
[[nodiscard]] std::int32_t chain_store_count(std::span<const LandEconomyRecord> lands,
                                             std::int32_t owner_code) noexcept;

// nullopt selects chain stores (2000 each); a present street selects normal
// lands of exactly that byte name, summed at each land's current level.
// Empty string is distinct from nullopt. Embedded NUL ends the byte name, as in
// original strcmp. Multiplication/addition retain the original low 32 bits.
[[nodiscard]] std::int32_t land_toll(std::span<const LandEconomyRecord> lands, std::int32_t owner_code,
                                     std::int32_t price_index,
                                     std::optional<std::string_view> street = std::nullopt);

} // namespace rich4
