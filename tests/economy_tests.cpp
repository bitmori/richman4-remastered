// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/economy.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace rich4;

void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function> void invalid(Function &&function) {
    try {
        function();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("Expected invalid economy input rejection");
}

LandEconomyRecord land(std::string name, std::uint8_t owner, std::uint8_t chain, std::uint8_t level,
                       std::uint16_t base) {
    LandEconomyRecord result{std::move(name), chain, owner, level, {}};
    for (std::size_t i = 0; i < result.toll_by_level.size(); ++i) {
        result.toll_by_level[i] = static_cast<std::uint16_t>(base + i * 10);
    }
    return result;
}

void decode_fields() {
    std::array<std::uint8_t, 52> bytes{};
    bytes[4] = 0xa4;
    bytes[5] = 0x40;
    bytes[6] = 0xa4;
    bytes[7] = 0x41;
    bytes[0x18] = 255;
    bytes[0x19] = 4;
    bytes[0x1a] = 5;
    for (std::size_t i = 0; i < 6; ++i) {
        bytes[0x20 + i * 2] = static_cast<std::uint8_t>(0xc0 + i);
        bytes[0x21 + i * 2] = 0xab;
    }
    const auto result = decode_land_economy_record(bytes);
    require(result.street_name == "\xa4\x40\xa4\x41" && result.chain_store == 255 && result.owner_code == 4 &&
                result.level == 5,
            "fixed offsets preserve raw Big5 name/flags");
    for (std::size_t i = 0; i < 6; ++i)
        require(result.toll_by_level[i] == 0xabc0 + i, "little-endian toll table");
    for (std::size_t size = 0; size < 52; ++size) {
        invalid([&] { (void)decode_land_economy_record(std::span(bytes).first(size)); });
    }
    std::array<std::uint8_t, 53> too_large{};
    invalid([&] { (void)decode_land_economy_record(too_large); });
    for (std::size_t i = 4; i < 20; ++i)
        bytes[i] = 0xff;
    invalid([&] { (void)decode_land_economy_record(bytes); });
}

void toll_rules() {
    const std::string big5 = "\xa4\x40";
    std::vector<LandEconomyRecord> lands{
        land(big5, 1, 0, 0, 100), land(big5, 1, 0, 5, 200),   land(big5 + "\xa4\x41", 1, 0, 2, 300),
        land(big5, 2, 0, 3, 400), land(big5, 1, 1, 255, 500), land("different", 1, 255, 255, 600),
        land("", 1, 0, 1, 700),   land("AB", 0, 1, 0, 800),   land("AB", 255, 1, 0, 900)};
    require(chain_store_count(lands, 1) == 2 && chain_store_count(lands, 0) == 1 &&
                chain_store_count(lands, 255) == 1 && chain_store_count(lands, -1) == 0 &&
                chain_store_count(lands, 256) == 0,
            "original owner code and any nonzero chain flag");
    require(land_toll(lands, 1, 3) == 12000, "null street sums chain stores at 2000 each");
    require(land_toll(lands, 1, 3, big5) == 1050, "same Big5 street sums ordinary owned lands by level");
    require(land_toll(lands, 1, 1, big5 + "\xa4\x41") == 320,
            "street name comparison is exact, not a prefix match");
    require(land_toll(lands, 2, 2, big5) == 860, "other owner selected explicitly");
    require(land_toll(lands, 1, 1, "") == 710, "empty name differs from null street");
    require(land_toll(lands, 1, 1, "missing") == 0 && land_toll(lands, 1, 0, big5) == 0,
            "no matching lands / zero price index");
    require(land_toll(lands, 1, -2, big5) == -700, "signed price index preserved");
    std::vector<LandEconomyRecord> nul_names{land(std::string("AB\0ignored", 10), 1, 0, 0, 123)};
    require(land_toll(nul_names, 1, 1, std::string_view("AB\0different", 12)) == 123 &&
                land_toll(nul_names, 1, 1, "ab") == 0,
            "NUL-terminated byte semantics, no case folding");
    require(chain_store_count({}, 1) == 0 && land_toll({}, 1, 1) == 0 && land_toll({}, 1, 1, big5) == 0,
            "empty collection");
    lands[0].level = 6;
    invalid([&] { (void)land_toll(lands, 1, 1, big5); });
    require(land_toll(lands, 1, 1) == 4000 && land_toll(lands, 2, 1, big5) == 430,
            "invalid level is only relevant when the original branch reads it");
}

void integer_wrapping() {
    auto record = land("overflow", 1, 0, 0, 0);
    record.toll_by_level.fill(65535);
    std::vector<LandEconomyRecord> records{record};
    require(land_toll(records, 1, 65537, "overflow") == -1, "32-bit multiplication keeps low bits");
    require(land_toll(records, 1, std::numeric_limits<std::int32_t>::min(), "overflow") ==
                std::numeric_limits<std::int32_t>::min(),
            "INT32_MIN multiplier uses defined modular arithmetic");
    records.assign(65538, record);
    require(land_toll(records, 1, 1, "overflow") == 65534, "addition itself wraps before multiplication");
    records = {land("chain", 1, 1, 0, 0)};
    require(land_toll(records, 1, std::numeric_limits<std::int32_t>::max()) == -2000 &&
                land_toll(records, 1, std::numeric_limits<std::int32_t>::min()) == 0,
            "chain-store branch has identical imul overflow semantics");
}

template <std::size_t Size> void read_bytes(std::istream &input, std::array<std::uint8_t, Size> &bytes) {
    for (auto &byte : bytes) {
        unsigned value;
        require(static_cast<bool>(input >> value) && value <= 255, "invalid economy oracle byte");
        byte = static_cast<std::uint8_t>(value);
    }
}

void compare_original(const char *path) {
    std::ifstream input(path);
    std::string signature;
    std::size_t cases;
    require(static_cast<bool>(input >> signature >> cases) && signature == "rich4-economy-v1" && cases > 0 &&
                cases <= 1000000,
            "invalid economy oracle header");
    for (std::size_t case_index = 0; case_index < cases; ++case_index) {
        std::int32_t owner, price;
        int street_present;
        require(static_cast<bool>(input >> owner >> price >> street_present) &&
                    (street_present == 0 || street_present == 1),
                "invalid economy oracle parameters");
        std::array<std::uint8_t, 16> query_bytes;
        read_bytes(input, query_bytes);
        std::string query;
        for (const auto byte : query_bytes)
            query.push_back(std::bit_cast<char>(byte));
        std::size_t count, stored;
        require(static_cast<bool>(input >> count >> stored) && count <= 100000 &&
                    (stored == count || (stored == 1 && count > 0)),
                "invalid economy record count");
        std::vector<LandEconomyRecord> lands;
        lands.reserve(count);
        for (std::size_t i = 0; i < stored; ++i) {
            std::array<std::uint8_t, 52> raw;
            read_bytes(input, raw);
            lands.push_back(decode_land_economy_record(raw));
        }
        if (stored == 1 && count > 1)
            lands.resize(count, lands.front());
        std::int32_t expected_count, expected_toll;
        require(static_cast<bool>(input >> expected_count >> expected_toll),
                "missing economy oracle outputs");
        const std::optional<std::string_view> street =
            street_present ? std::optional<std::string_view>{query} : std::nullopt;
        require(chain_store_count(lands, owner) == expected_count &&
                    land_toll(lands, owner, price, street) == expected_toll,
                "Original economy mismatch at case " + std::to_string(case_index));
    }
    input >> std::ws;
    require(input.eof(), "extra economy oracle bytes");
    std::cout << "Original EXE economy cases matched: " << cases << '\n';
}

} // namespace

int main(int argc, char **argv) {
    try {
        decode_fields();
        toll_rules();
        integer_wrapping();
        for (int i = 1; i < argc; ++i)
            compare_original(argv[i]);
        std::cout << "Economy tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Economy tests failed: " << error.what() << '\n';
        return 1;
    }
}
