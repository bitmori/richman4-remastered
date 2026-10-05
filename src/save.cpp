// SPDX-License-Identifier: GPL-3.0-or-later
// Layout recovered from mytbk/rich4 asm/rich4_save_files.asm and
// asm/rich4_player_info.h (Iru Cai); see docs/migrations/002-save.md.
#include "rich4/save.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace rich4 {
namespace {

constexpr std::size_t kPlayerRecords = 0x10;
constexpr std::size_t kPlayerBytes = 0x68;
constexpr std::size_t kPlayerCards = 0x654;
constexpr std::size_t kPlayerTools = 0x690;
constexpr std::size_t kCurrentPlayer = 0x2676;
constexpr std::size_t kMapByteSize = 0x2747;
constexpr std::size_t kMapBlock = 0x274b;
constexpr std::size_t kSnapshotBytes = 0x2718;

void require_range(std::span<const std::uint8_t> bytes, std::size_t offset,
                   std::size_t size, const char* label) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        throw SaveError(std::string("Truncated save region: ") + label);
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(bytes, offset, 2, "16-bit field");
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(bytes, offset, 4, "32-bit field");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::int32_t read_i32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<std::int32_t>(read_u32(bytes, offset));
}

void write_i32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
}

MapSummary read_map_summary(std::span<const std::uint8_t> bytes) {
    const auto table = [&](std::size_t entry, std::uint32_t stride) {
        return MapTableSummary{read_u32(bytes, kMapBlock + entry * 8),
                               read_u32(bytes, kMapBlock + entry * 8 + 4), stride};
    };
    return {read_u32(bytes, kMapByteSize), table(0, 40), table(1, 52),
            table(2, 56), table(3, 52), table(4, 28)};
}

void validate_map(const MapSummary& map) {
    const std::array tables{map.nodes, map.lands, map.facilities,
                            map.commercials, map.raw_reserved_table_4};
    std::array<std::uint64_t, 5> ends{};
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto& table = tables[i];
        // Arithmetic is explicitly widened before count + 1, including on a
        // 32-bit build. Every table includes its original index-zero record.
        ends[i] = table.byte_offset +
                  (static_cast<std::uint64_t>(table.count) + 1) * table.record_bytes;
        if (table.byte_offset < 40 || ends[i] > map.byte_size) {
            throw SaveError("Map table exceeds the serialized map block");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (table.byte_offset < ends[j] && tables[j].byte_offset < ends[i]) {
                throw SaveError("Serialized map tables overlap");
            }
        }
    }
}

} // namespace

SaveFile::SaveFile(std::vector<std::uint8_t> bytes, std::vector<SaveRegion> regions)
    : bytes_(std::move(bytes)), regions_(std::move(regions)) {}

SaveFile SaveFile::decode(std::span<const std::uint8_t> bytes) {
    require_range(bytes, 0, kMapBlock, "fixed state");
    if (read_u32(bytes, 0) != 0x26) {
        throw SaveError("Unsupported save signature (expected 0x26)");
    }
    const auto players = read_u32(bytes, 0xc);
    if (players < 1 || players > 4) {
        throw SaveError("Player count must be between 1 and 4");
    }
    if (read_u32(bytes, 0x1b0) > players) {
        throw SaveError("Human player count exceeds total player count");
    }
    if (read_u32(bytes, kCurrentPlayer) >= players) {
        throw SaveError("Current player is outside the saved player slots");
    }
    const auto map_size = read_u32(bytes, kMapByteSize);
    if (map_size < 40) {
        throw SaveError("Serialized map has no complete table header");
    }
    require_range(bytes, kMapBlock, map_size, "main map");
    validate_map(read_map_summary(bytes));

    std::vector<SaveRegion> regions;
    std::size_t offset = 0;
    const auto add = [&](std::string name, std::size_t size, SaveRegionKind kind) {
        require_range(bytes, offset, size, name.c_str());
        regions.push_back({std::move(name), offset, size, kind});
        offset += size;
    };
    using enum SaveRegionKind;
    add("header", 0x10, decoded);
    add("player_records", 4 * kPlayerBytes, partially_decoded);
    add("human_player_count", 4, decoded);
    add("raw_reserved_special_players", 5 * 0x10, raw_reserved);
    add("raw_reserved_objects", 46 * 0x18, raw_reserved);
    add("player_cards", 60, decoded);
    add("player_tool_amounts", 60, decoded);
    add("raw_reserved_remaining_cards", 30, raw_reserved);
    add("raw_reserved_remaining_tools", 8, raw_reserved);
    add("raw_reserved_00499100", 4, raw_reserved);
    add("raw_reserved_00497328", 0x6c0 * 4, raw_reserved);
    add("raw_reserved_player_stocks", 48 * 8, raw_reserved);
    add("raw_reserved_stocks_on_map", 12 * 0x24, raw_reserved);
    add("raw_reserved_004967e0", 28 * 12, raw_reserved);
    add("current_player", 4, decoded);
    add("raw_reserved_00499118", 4, raw_reserved);
    add("raw_reserved_00499110", 4, raw_reserved);
    add("raw_reserved_0049911c", 4, raw_reserved);
    add("raw_reserved_00499108", 4, raw_reserved);
    add("initial_fund", 4, decoded);
    add("price_index", 4, decoded);
    add("raw_reserved_004990e4", 4, raw_reserved);
    add("raw_reserved_00499084", 4, raw_reserved);
    add("raw_reserved_004990dc", 4, raw_reserved);
    add("raw_reserved_0049907c", 4, raw_reserved);
    add("raw_reserved_00499078", 4, raw_reserved);
    add("raw_reserved_004990ec", 4, raw_reserved);
    add("raw_reserved_004990f0", 4, raw_reserved);
    add("raw_reserved_004990f4", 12, raw_reserved);
    add("raw_reserved_00499080", 4, raw_reserved);
    add("raw_reserved_004990b8", 36, raw_reserved);
    add("raw_reserved_00496b30", 8, raw_reserved);
    add("raw_reserved_00496b60", 8, raw_reserved);
    add("raw_reserved_004990e0", 4, raw_reserved);
    add("raw_reserved_004990b4", 4, raw_reserved);
    add("raw_reserved_00499090", 36, raw_reserved);
    add("raw_reserved_00496b38", 37, raw_reserved);
    add("raw_reserved_00499088", 4, raw_reserved);
    add("map_byte_size", 4, decoded);
    add("map_header", 40, decoded);
    add("raw_reserved_map_records", map_size - 40, raw_reserved);
    for (std::uint32_t i = 0; i < players; ++i) {
        add("raw_reserved_player_snapshot_" + std::to_string(i), kSnapshotBytes, raw_reserved);
        add("raw_reserved_player_map_snapshot_" + std::to_string(i), map_size, raw_reserved);
    }
    if (offset < bytes.size()) {
        add("raw_reserved_trailing_bytes", bytes.size() - offset, raw_reserved);
    }
    return SaveFile({bytes.begin(), bytes.end()}, std::move(regions));
}

SaveHeader SaveFile::header() const {
    return {read_u32(bytes_, 0), {bytes_[4], bytes_[5], read_u16(bytes_, 6)},
            read_u16(bytes_, 8), read_u16(bytes_, 10), read_u32(bytes_, 12),
            read_u32(bytes_, 0x1b0), read_u32(bytes_, kCurrentPlayer),
            read_i32(bytes_, 0x268a), read_i32(bytes_, 0x268e)};
}

std::size_t SaveFile::player_offset(std::size_t slot) const {
    if (slot >= read_u32(bytes_, 12)) {
        throw std::out_of_range("Player slot is outside the saved player count");
    }
    return kPlayerRecords + slot * kPlayerBytes;
}

PlayerState SaveFile::player(std::size_t slot) const {
    const auto base = player_offset(slot);
    PlayerState result{};
    result.slot = slot;
    result.color = read_u32(bytes_, base + 4);
    result.x = read_u16(bytes_, base + 8);
    result.y = read_u16(bytes_, base + 10);
    result.node_id = read_u16(bytes_, base + 12);
    result.last_node_id = read_u16(bytes_, base + 14);
    result.direction = bytes_[base + 16];
    result.traffic_method = bytes_[base + 17];
    result.dice_count = bytes_[base + 18];
    result.character = bytes_[base + 19];
    result.sex = bytes_[base + 20];
    result.controller_flags = bytes_[base + 21];
    result.cash = read_i32(bytes_, base + 28);
    result.bank_balance = read_i32(bytes_, base + 32);
    result.loan = read_i32(bytes_, base + 36);
    result.special_finance = read_i32(bytes_, base + 40);
    result.points = read_u16(bytes_, base + 48);
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(kPlayerCards + slot * 15),
                15, result.cards.begin());
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(kPlayerTools + slot * 15),
                15, result.tool_amounts.begin());
    return result;
}

MapSummary SaveFile::map_summary() const { return read_map_summary(bytes_); }

std::span<const SaveRegion> SaveFile::regions() const noexcept { return regions_; }

std::vector<std::uint8_t> SaveFile::encode() const { return bytes_; }

void SaveFile::set_cash(std::size_t slot, std::int32_t value) {
    write_i32(bytes_, player_offset(slot) + 28, value);
}

void SaveFile::set_bank_balance(std::size_t slot, std::int32_t value) {
    write_i32(bytes_, player_offset(slot) + 32, value);
}

} // namespace rich4
