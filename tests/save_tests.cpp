// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/save.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kMapOffset = 0x274b;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

void put_u16(Bytes &bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put_u32(Bytes &bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

// All fixture bytes are generated here; no commercial game data is embedded.
Bytes fixture(std::uint32_t players = 4) {
    constexpr std::array<std::uint32_t, 5> counts{7, 3, 1, 2, 4};
    constexpr std::array<std::uint32_t, 5> strides{40, 52, 56, 52, 28};
    std::uint32_t map_size = 40;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        map_size += (counts[i] + 1) * strides[i];
    }
    Bytes bytes(kMapOffset + map_size + players * (0x2718 + map_size));
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 73 + i / 7 + 19) & 0xff);
    }
    put_u32(bytes, 0, 0x26);
    bytes[4] = 29;
    bytes[5] = 2;
    put_u16(bytes, 6, 2000);
    put_u16(bytes, 8, 2);
    put_u16(bytes, 10, 1);
    put_u32(bytes, 12, players);
    put_u32(bytes, 0x1b0, 1);
    put_u32(bytes, 0x2676, players - 1);
    put_u32(bytes, 0x268a, 200000);
    put_u32(bytes, 0x268e, 3);
    put_u32(bytes, 0x2747, map_size);
    std::uint32_t relative_offset = 40;
    for (std::size_t i = 0; i < counts.size(); ++i) {
        put_u32(bytes, kMapOffset + i * 8, counts[i]);
        put_u32(bytes, kMapOffset + i * 8 + 4, relative_offset);
        relative_offset += (counts[i] + 1) * strides[i];
    }
    for (std::uint32_t i = 0; i < players; ++i) {
        const auto base = 0x10 + i * 0x68;
        put_u32(bytes, base, 0xffffffff); // Serialized address must never be dereferenced.
        put_u32(bytes, base + 4, 0x01234567 + i);
        put_u16(bytes, base + 8, static_cast<std::uint16_t>(320 + i));
        put_u16(bytes, base + 10, static_cast<std::uint16_t>(240 + i));
        put_u16(bytes, base + 12, static_cast<std::uint16_t>(5 + i));
        put_u16(bytes, base + 14, static_cast<std::uint16_t>(4 + i));
        bytes[base + 16] = 1;
        bytes[base + 17] = 2;
        bytes[base + 18] = 3;
        bytes[base + 19] = static_cast<std::uint8_t>(7 + i);
        bytes[base + 20] = 1;
        bytes[base + 21] = static_cast<std::uint8_t>(i == 0 ? 5 : 2);
        put_u32(bytes, base + 28, 0xfffffff9); // -7, signed two's complement.
        put_u32(bytes, base + 32, 100000 + i);
        put_u32(bytes, base + 36, 12345 + i);
        put_u32(bytes, base + 40, 6789 + i);
        put_u16(bytes, base + 48, static_cast<std::uint16_t>(90 + i));
        for (std::size_t j = 0; j < 15; ++j) {
            bytes[0x654 + i * 15 + j] = static_cast<std::uint8_t>(j + 1);
            bytes[0x690 + i * 15 + j] = static_cast<std::uint8_t>(j * 2);
        }
    }
    return bytes;
}

template <typename Exception, typename Function>
void expect_error(Function &&function, const std::string &label) {
    try {
        function();
    } catch (const Exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection: " + label);
}

void verify_regions(const rich4::SaveFile &save, std::size_t total_size) {
    std::size_t next = 0;
    bool found_raw = false;
    for (const auto &region : save.regions()) {
        require(region.offset == next, "regions cover every byte once, without gaps");
        require(region.byte_size > 0, "nonempty region");
        next += region.byte_size;
        if (region.kind == rich4::SaveRegionKind::raw_reserved) {
            require(region.name.starts_with("raw_reserved_"), "opaque regions are named explicitly");
            found_raw = true;
        }
    }
    require(next == total_size && found_raw, "complete region inventory");
}

void check_decode_and_roundtrip() {
    const auto bytes = fixture();
    const auto save = rich4::SaveFile::decode(bytes);
    require(save.encode() == bytes, "byte-identical round trip, including unknown fields");
    verify_regions(save, bytes.size());
    const auto header = save.header();
    require(header.signature == 0x26 && header.date.day == 29 && header.date.month == 2 &&
                header.date.year == 2000 && header.map_id == 2 && header.stage_id == 1 &&
                header.player_count == 4 && header.human_player_count == 1 && header.current_player == 3 &&
                header.initial_fund == 200000 && header.price_index == 3,
            "header fields decode at the documented offsets");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto player = save.player(i);
        require(player.slot == i && player.color == 0x01234567 + i && player.character == 7 + i &&
                    player.cash == -7 && player.bank_balance == 100000 + static_cast<std::int32_t>(i) &&
                    player.loan == 12345 + static_cast<std::int32_t>(i) &&
                    player.special_finance == 6789 + static_cast<std::int32_t>(i),
                "player financial and identity fields");
        require(player.x == 320 + i && player.y == 240 + i && player.node_id == 5 + i &&
                    player.last_node_id == 4 + i && player.direction == 1 && player.traffic_method == 2 &&
                    player.dice_count == 3 && player.sex == 1 &&
                    player.controller_flags == (i == 0 ? 5 : 2) && player.points == 90 + i,
                "player position and controller fields");
        for (std::size_t j = 0; j < 15; ++j) {
            require(player.cards[j] == j + 1 && player.tool_amounts[j] == j * 2,
                    "independent card/tool inventory rows");
        }
    }
    const auto map = save.map_summary();
    require(map.nodes.count == 7 && map.nodes.byte_offset == 40 && map.nodes.record_bytes == 40 &&
                map.lands.count == 3 && map.facilities.count == 1 && map.commercials.count == 2 &&
                map.raw_reserved_table_4.count == 4 && map.byte_size == 976,
            "map count/offset pairs and record strides");

    auto unaligned = bytes;
    unaligned.insert(unaligned.begin(), 0x12);
    require(rich4::SaveFile::decode(std::span(unaligned).subspan(1)).encode() == bytes,
            "decode does not require aligned addresses");
    auto trailer = bytes;
    trailer.insert(trailer.end(), {0x00, 0xfe, 0x12, 0xff});
    const auto with_trailer = rich4::SaveFile::decode(trailer);
    require(with_trailer.encode() == trailer, "unknown trailing bytes are preserved");
    verify_regions(with_trailer, trailer.size());
    require(with_trailer.regions().back().name == "raw_reserved_trailing_bytes", "trailer named");
}

void check_edits() {
    const auto bytes = fixture();
    constexpr std::array<std::int32_t, 5> values{0, -1, 123456789, std::numeric_limits<std::int32_t>::min(),
                                                 std::numeric_limits<std::int32_t>::max()};
    for (std::size_t slot = 0; slot < 4; ++slot) {
        for (const auto value : values) {
            auto save = rich4::SaveFile::decode(bytes);
            auto expected = bytes;
            save.set_cash(slot, value);
            put_u32(expected, 0x10 + slot * 0x68 + 28, static_cast<std::uint32_t>(value));
            require(save.encode() == expected, "cash edit changes only its four agreed bytes");
            require(save.player(slot).cash == value, "signed cash edit reads back");
            save.set_bank_balance(slot, value);
            put_u32(expected, 0x10 + slot * 0x68 + 32, static_cast<std::uint32_t>(value));
            require(save.encode() == expected, "bank edit changes only its four agreed bytes");
            require(rich4::SaveFile::decode(save.encode()).player(slot).bank_balance == value,
                    "signed bank edit survives re-decoding");
        }
    }
    auto save = rich4::SaveFile::decode(fixture(1));
    for (const auto invalid : {std::size_t{1}, std::size_t{4}, std::numeric_limits<std::size_t>::max()}) {
        const auto before = save.encode();
        expect_error<std::out_of_range>([&] { (void)save.player(invalid); }, "invalid player read");
        expect_error<std::out_of_range>([&] { save.set_cash(invalid, 1); }, "invalid cash edit");
        expect_error<std::out_of_range>([&] { save.set_bank_balance(invalid, 1); }, "invalid bank edit");
        require(save.encode() == before, "failed edit has no side effects");
    }
}

void check_invalid_input() {
    const auto bytes = fixture(1);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        expect_error<rich4::SaveError>([&] { (void)rich4::SaveFile::decode(std::span(bytes).first(size)); },
                                       "every truncated prefix");
    }
    const auto corrupt = [&](std::size_t offset, std::uint32_t value, const char *label) {
        auto changed = bytes;
        put_u32(changed, offset, value);
        expect_error<rich4::SaveError>([&] { (void)rich4::SaveFile::decode(changed); }, label);
    };
    corrupt(0, 0x27, "unsupported signature");
    corrupt(12, 0, "zero players");
    corrupt(12, 5, "too many players");
    corrupt(12, 0xffffffff, "negative serialized player count");
    corrupt(0x1b0, 2, "human count exceeds slots");
    corrupt(0x2676, 1, "current player exceeds slots");
    corrupt(0x2747, 39, "map smaller than its header");
    corrupt(0x2747, 0xffffffff, "map size overflows available bytes");
    corrupt(kMapOffset, 0xffffffff, "table count cannot overflow count plus one");
    corrupt(kMapOffset + 4, 39, "table overlaps map header");
    corrupt(kMapOffset + 4, 0xffffffff, "table offset exceeds map");
    corrupt(kMapOffset + 12, 40, "tables overlap each other");
}

void verify_local_file(const char *path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), std::string("Cannot read save: ") + path);
    const Bytes bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto save = rich4::SaveFile::decode(bytes);
    require(save.encode() == bytes, "local original save byte-identical round trip");
    verify_regions(save, bytes.size());
    const auto header = save.header();
    std::cout << "Local save verified (read only): " << path << "\n"
              << "  bytes=" << bytes.size() << " date=" << header.date.year << '-'
              << static_cast<unsigned>(header.date.month) << '-' << static_cast<unsigned>(header.date.day)
              << " players=" << header.player_count << " current=" << header.current_player
              << " nodes=" << save.map_summary().nodes.count << '\n';
    for (std::size_t i = 0; i < header.player_count; ++i) {
        const auto player = save.player(i);
        std::cout << "  slot=" << player.slot << " character=" << static_cast<unsigned>(player.character)
                  << " cash=" << player.cash << " bank=" << player.bank_balance << " node=" << player.node_id
                  << '\n';
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        check_decode_and_roundtrip();
        check_edits();
        check_invalid_input();
        for (int i = 1; i < argc; ++i)
            verify_local_file(argv[i]);
        std::cout << "Save codec tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Save codec test failed: " << error.what() << '\n';
        return 1;
    }
}
