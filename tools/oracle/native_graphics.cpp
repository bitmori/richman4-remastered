// SPDX-License-Identifier: GPL-3.0-or-later
// Compare portable graphics with the output of original_mkf.c, not a second
// implementation of the expected rendering rules.
#include "rich4/graphics.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
int integer(std::string_view& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    int result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{}) throw std::runtime_error("invalid golden integer");
    value.remove_prefix(static_cast<std::size_t>(end - value.data()));
    return result;
}

int field(std::string_view line, std::string_view key) {
    const auto marker = std::string("\"") + std::string(key) + "\":";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) throw std::runtime_error("missing golden field: " + std::string(key));
    line.remove_prefix(position + marker.size());
    return integer(line);
}

std::array<std::uint16_t, 42> expected_pixels(std::string_view line) {
    constexpr std::string_view marker = "\"pixels\":[";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) throw std::runtime_error("missing golden pixel array");
    line.remove_prefix(position + marker.size());
    std::array<std::uint16_t, 42> pixels{};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const auto value = integer(line);
        if (value < 0 || value > 65535) throw std::runtime_error("golden pixel out of range");
        pixels[i] = static_cast<std::uint16_t>(value);
        if (line.empty() || line.front() != (i + 1 == pixels.size() ? ']' : ','))
            throw std::runtime_error("invalid golden pixel array size");
        line.remove_prefix(1);
    }
    return pixels;
}

std::vector<int> array_field(std::string_view line, std::string_view key) {
    const auto marker = std::string("\"") + std::string(key) + "\":[";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) throw std::runtime_error("missing golden array");
    line.remove_prefix(position + marker.size());
    std::vector<int> result;
    while (!line.empty() && line.front() != ']') {
        result.push_back(integer(line));
        if (line.empty() || (line.front() != ',' && line.front() != ']'))
            throw std::runtime_error("invalid golden array separator");
        if (line.front() == ',') line.remove_prefix(1);
    }
    if (line.empty()) throw std::runtime_error("unterminated golden array");
    return result;
}

rich4::graphics::Image synthetic_spr(int x, int y) {
    std::vector<std::uint8_t> bytes(24 + 512 + 20);
    auto put16 = [&](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    };
    bytes[0] = 'S'; bytes[1] = 'P'; bytes[2] = 'R';
    bytes[4] = 1; bytes[8] = 24;
    put16(12, 5); put16(14, 4);
    put16(16, static_cast<std::uint16_t>(x));
    put16(18, static_cast<std::uint16_t>(y));
    bytes[20] = 20;
    put16(24, 0x1234); put16(26, 0); put16(28, 0x4567); put16(30, 0x4321);
    for (std::size_t i = 0; i < 20; ++i) bytes[24 + 512 + i] = static_cast<std::uint8_t>(i % 4);
    return rich4::graphics::decode_frame(bytes, 0);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: rich4-graphics-oracle GOLDEN_DIRECTORY");
        const std::filesystem::path golden(argv[1]);
        std::ifstream binary(golden / "pixels-1.bin", std::ios::binary);
        if (!binary) throw std::runtime_error("missing original pixel vectors");
        const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(binary), {}};
        if (bytes.size() != 65536 * 2) throw std::runtime_error("unexpected original pixel vector size");
        for (std::size_t i = 0; i < 65536; ++i) {
            const auto expected = static_cast<std::uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8));
            if (rich4::graphics::rgb555_to_rgb565(static_cast<std::uint16_t>(i)) != expected)
                throw std::runtime_error("original RGB565 mismatch at input " + std::to_string(i));
        }
        std::ifstream vectors(golden / "blit.jsonl");
        if (!vectors) throw std::runtime_error("missing original blit vectors");
        std::size_t cases{};
        std::string line;
        while (std::getline(vectors, line)) {
            auto source = rich4::graphics::make_canvas(5, 4);
            source.origin_x = field(line, "anchor_x");
            source.origin_y = field(line, "anchor_y");
            for (std::size_t i = 0; i < source.pixels.size(); ++i)
                source.pixels[i] = i % 4 == 0 ? 0 : static_cast<std::uint16_t>(0x101 + i);
            auto destination = rich4::graphics::make_canvas(7, 6, 0x7bde);
            rich4::graphics::blit(destination, source, field(line, "x"), field(line, "y"),
                field(line, "transparent") ? rich4::graphics::Transparency::source_key
                                           : rich4::graphics::Transparency::opaque);
            const auto expected = expected_pixels(line);
            if (!std::equal(expected.begin(), expected.end(), destination.pixels.begin(), destination.pixels.end()))
                throw std::runtime_error("original blit mismatch in case " + std::to_string(cases));
            ++cases;
        }
        if (cases != 1344) throw std::runtime_error("incomplete original blit corpus");
        std::ifstream spr_vectors(golden / "spr.jsonl");
        if (!spr_vectors) throw std::runtime_error("missing original SPR vectors");
        std::size_t spr_cases = 0;
        while (std::getline(spr_vectors, line)) {
            auto source = synthetic_spr(field(line, "anchor_x"), field(line, "anchor_y"));
            auto destination = rich4::graphics::make_canvas(640, 480, 0x7bde);
            rich4::graphics::blit(destination, source, field(line, "x"), field(line, "y"),
                field(line, "transparent") ? rich4::graphics::Transparency::source_key
                                           : rich4::graphics::Transparency::opaque);
            const auto changed = array_field(line, "changed");
            if (changed.size() % 2) throw std::runtime_error("odd SPR changed-pixel vector");
            auto expected = rich4::graphics::make_canvas(640, 480, 0x7bde);
            for (std::size_t i = 0; i < changed.size(); i += 2) {
                if (changed[i] < 0 || changed[i] >= 640 * 480 || changed[i + 1] < 0 || changed[i + 1] > 65535)
                    throw std::runtime_error("invalid SPR changed-pixel vector");
                expected.pixels[static_cast<std::size_t>(changed[i])] = static_cast<std::uint16_t>(changed[i + 1]);
            }
            if (expected.pixels != destination.pixels)
                throw std::runtime_error("original SPR mismatch in case " + std::to_string(spr_cases));
            ++spr_cases;
        }
        if (spr_cases != 114) throw std::runtime_error("incomplete original SPR corpus");
        std::cout << "{\"pixel_cases\":65536,\"blit_cases\":" << cases
                  << ",\"spr_cases\":" << spr_cases
                  << ",\"all_pixels_identical\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "graphics oracle: " << error.what() << '\n';
        return 1;
    }
}
