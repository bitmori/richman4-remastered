// SPDX-License-Identifier: GPL-3.0-or-later
// Reconstructed from rich4_player_core_actions.asm; see migration 006.
#include "rich4/economy.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace rich4 {
namespace {

std::string_view nul_prefix(std::string_view bytes) noexcept {
    const auto nul = bytes.find('\0');
    return bytes.substr(0, nul);
}

} // namespace

LandEconomyRecord decode_land_economy_record(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 52) throw std::invalid_argument("A land record must contain exactly 52 bytes");
    const auto name_bytes = bytes.subspan(4, 16);
    const auto end = std::find(name_bytes.begin(), name_bytes.end(), 0);
    if (end == name_bytes.end()) throw std::invalid_argument("Land name is missing its NUL terminator");
    LandEconomyRecord result;
    for (auto current = name_bytes.begin(); current != end; ++current) {
        result.street_name.push_back(std::bit_cast<char>(*current));
    }
    result.chain_store = bytes[0x18];
    result.owner_code = bytes[0x19];
    result.level = bytes[0x1a];
    for (std::size_t i = 0; i < result.toll_by_level.size(); ++i) {
        const auto offset = 0x20 + i * 2;
        result.toll_by_level[i] = static_cast<std::uint16_t>(bytes[offset]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
    }
    return result;
}

std::int32_t chain_store_count(std::span<const LandEconomyRecord> lands,
                               std::int32_t owner_code) noexcept {
    std::uint32_t result = 0;
    for (const auto& land : lands) {
        if (land.chain_store != 0 && land.owner_code == owner_code) ++result;
    }
    return std::bit_cast<std::int32_t>(result);
}

std::int32_t land_toll(std::span<const LandEconomyRecord> lands, std::int32_t owner_code,
                       std::int32_t price_index, std::optional<std::string_view> street) {
    std::uint32_t total = 0;
    for (const auto& land : lands) {
        if (land.owner_code != owner_code) continue;
        if (!street) {
            if (land.chain_store != 0) total += 2000;
        } else if (land.chain_store == 0 && nul_prefix(land.street_name) == nul_prefix(*street)) {
            if (land.level >= land.toll_by_level.size()) {
                throw std::invalid_argument("A matched normal land has a level outside 0..5");
            }
            total += land.toll_by_level[land.level];
        }
    }
    // Unsigned arithmetic is defined modulo 2^32, matching x86 add/imul.
    return std::bit_cast<std::int32_t>(total * std::bit_cast<std::uint32_t>(price_index));
}

} // namespace rich4
