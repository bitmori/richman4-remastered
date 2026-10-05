// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/assets.hpp"
#include "rich4/terrain.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace rich4::terrain;
using Bytes = std::vector<std::uint8_t>;
void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
template <class Operation> void rejects(Operation operation, const char *message) {
    try {
        operation();
    } catch (const rich4::assets::AssetError &) {
        return;
    } catch (const std::invalid_argument &) {
        return;
    } catch (const std::out_of_range &) {
        return;
    }
    throw std::runtime_error(message);
}
void set16(Bytes &bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8U);
}
void set32(Bytes &bytes, std::size_t at, std::uint32_t value) {
    set16(bytes, at, static_cast<std::uint16_t>(value));
    set16(bytes, at + 2, static_cast<std::uint16_t>(value >> 16U));
}
Bytes ground_fixture() {
    Bytes bytes(532 + 2048, 0);
    set32(bytes, 0, 0x00444e47U);
    set16(bytes, 4, 2);
    set16(bytes, 6, 1);
    set32(bytes, 8, 2);
    set32(bytes, 12, 0x12345678U);
    set16(bytes, 18, 0x1234);
    set16(bytes, 20, 0x4321);
    set16(bytes, 528, 1);
    set16(bytes, 530, 0);
    std::fill(bytes.begin() + 532, bytes.begin() + 1556, 1);
    std::fill(bytes.begin() + 1556, bytes.end(), 2);
    return bytes;
}
void ground_tests() {
    auto bytes = ground_fixture();
    const auto original = bytes;
    const auto ground = parse_ground(bytes);
    check(bytes == original, "GND parsing changed input bytes");
    check(ground.width_tiles == 2 && ground.height_tiles == 1 && ground.tile_count() == 2, "GND dimensions");
    check(ground.reserved == 0x12345678U, "GND reserved field lost");
    check(ground.palette[1] == 0x1234 && ground.palette[2] == 0x4321, "GND little-endian palette");
    check(ground.tile_indices == std::vector<std::uint16_t>{1, 0}, "GND tile order");
    check(ground.tile(0)[1023] == 1 && ground.tile(1)[0] == 2, "GND tile slices");
    auto image = render_overview(ground);
    check(image.width == 64 && image.height == 32, "GND overview dimensions");
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            check(image.pixels[static_cast<std::size_t>(y * 64 + x)] == (x < 32 ? 0x4321 : 0x1234),
                  "GND overview tile placement");
        }
    }
    rejects([&] { (void)ground.tile(2); }, "Invalid GND tile accepted");
    rejects([&] { (void)render_overview(ground, 2047); }, "GND overview limit ignored");
    for (const auto length : {0U, 15U, 527U, 531U, 2579U}) {
        rejects([&] { (void)parse_ground(std::span(bytes).first(length)); }, "Truncated GND accepted");
    }
    bytes = original;
    bytes[3] = 1;
    rejects([&] { (void)parse_ground(bytes); }, "Invalid GND signature accepted");
    bytes = original;
    set16(bytes, 4, 0);
    rejects([&] { (void)parse_ground(bytes); }, "Zero GND dimension accepted");
    bytes = original;
    set32(bytes, 8, 0xffffffffU);
    rejects([&] { (void)parse_ground(bytes); }, "Overflowing GND tile count accepted");
    bytes = original;
    set16(bytes, 528, 2);
    rejects([&] { (void)parse_ground(bytes); }, "Out-of-range GND tile index accepted");
}

void draw_tests() {
    std::array<std::uint8_t, 1024> texture{};
    std::array<std::uint16_t, 256> palette{};
    palette[0] = 0x1234;
    auto image = rich4::graphics::make_canvas(8, 8, 0x7bde);
    image.opaque_pixels.assign(64, 0);
    const Quad corners{{{2, 2}, {2, 6}, {6, 6}, {6, 2}}};
    draw_tile(image, texture, palette, corners, {0, 0, 8, 8});
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool inside = x >= 2 && x < 6 && y >= 2 && y < 6;
            const auto index = static_cast<std::size_t>(y * 8 + x);
            check(image.pixels[index] == (inside ? 0x1234 : 0x7bde),
                  "Opaque zero-index tile or half-open boundaries");
            check(image.opaque_pixels[index] == static_cast<unsigned>(inside), "Terrain opacity not marked");
        }
    }
    image = rich4::graphics::make_canvas(8, 8, 0x7bde);
    draw_tile(image, texture, palette, corners, {3, 3, 5, 5});
    check(std::count(image.pixels.begin(), image.pixels.end(), 0x1234) == 4, "Terrain rectangular clip");
    auto saved = image.pixels;
    draw_tile(image, texture, palette, corners, {3, 3, 3, 5});
    draw_tile(image, texture, palette, Quad{}, {0, 0, 8, 8});
    check(image.pixels == saved, "Empty or degenerate tile changed canvas");
    rejects(
        [&] {
            draw_tile(image, texture, palette, Quad{{{-32768, 0}, {0, 2}, {2, 2}, {2, 0}}}, {0, 0, 8, 8});
        },
        "Ambiguous 16-bit edge-height overflow accepted");
    image.pixels.pop_back();
    rejects([&] { draw_tile(image, texture, palette, corners, {0, 0, 8, 8}); },
            "Invalid destination storage accepted");
}

std::vector<int> numbers(const std::string &line, const std::string &key) {
    const auto label = line.find('"' + key + '"');
    if (label == std::string::npos) {
        throw std::runtime_error("Missing terrain golden field");
    }
    const auto start = line.find('[', label);
    const auto end = line.find(']', start);
    if (start == std::string::npos || end == std::string::npos) {
        throw std::runtime_error("Malformed terrain golden array");
    }
    auto values = line.substr(start + 1, end - start - 1);
    std::replace(values.begin(), values.end(), ',', ' ');
    std::istringstream stream(values);
    std::vector<int> result;
    for (int value; stream >> value;) {
        result.push_back(value);
    }
    return result;
}

std::size_t check_original(const std::filesystem::path &manifest) {
    std::ifstream input(manifest);
    if (!input) {
        throw std::runtime_error("Cannot open terrain golden manifest");
    }
    std::array<std::uint8_t, 1024> texture;
    std::array<std::uint16_t, 256> palette;
    for (unsigned i = 0; i < 256; ++i) {
        palette[i] = static_cast<std::uint16_t>((i * 0x123U + 0x11U) & 0x7fffU);
    }
    for (unsigned y = 0; y < 32; ++y) {
        for (unsigned x = 0; x < 32; ++x) {
            texture[y * 32 + x] = static_cast<std::uint8_t>((x + y * 7) & 255U);
        }
    }
    std::size_t count = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.empty()) {
            continue;
        }
        const auto file_key = line.find("\"file\"");
        const auto colon = line.find(':', file_key);
        const auto start = line.find('"', colon + 1);
        const auto end = line.find('"', start + 1);
        if (file_key == std::string::npos || colon == std::string::npos || start == std::string::npos ||
            end == std::string::npos) {
            throw std::runtime_error("Missing terrain golden filename");
        }
        const auto filename = line.substr(start + 1, end - start - 1);
        const auto points = numbers(line, "corners");
        const auto rectangle = numbers(line, "clip");
        check(points.size() == 8 && rectangle.size() == 4, "Malformed terrain golden geometry");
        Quad corners;
        for (std::size_t i = 0; i < 4; ++i) {
            check(points[i * 2] >= -32768 && points[i * 2] <= 32767 && points[i * 2 + 1] >= -32768 &&
                      points[i * 2 + 1] <= 32767,
                  "Terrain golden vertex out of range");
            corners[i] = {static_cast<std::int16_t>(points[i * 2]),
                          static_cast<std::int16_t>(points[i * 2 + 1])};
        }
        auto image = rich4::graphics::make_canvas(640, 480, 0x7bde);
        draw_tile(image, texture, palette, corners, {rectangle[0], rectangle[1], rectangle[2], rectangle[3]});
        std::ifstream pixels(manifest.parent_path() / filename, std::ios::binary);
        if (!pixels) {
            throw std::runtime_error("Cannot open terrain golden pixels");
        }
        const Bytes expected(std::istreambuf_iterator<char>{pixels}, {});
        check(expected.size() == image.pixels.size() * 2, "Wrong terrain golden payload size");
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            const auto value = static_cast<std::uint16_t>(expected[i * 2] |
                                                          (static_cast<unsigned>(expected[i * 2 + 1]) << 8U));
            if (image.pixels[i] != value) {
                throw std::runtime_error(
                    "Terrain oracle mismatch in " + filename + " at x=" + std::to_string(i % 640) +
                    " y=" + std::to_string(i / 640) + " native=" + std::to_string(image.pixels[i]) +
                    " original=" + std::to_string(value));
            }
        }
        ++count;
    }
    check(count > 0, "Empty terrain golden manifest");
    return count;
}
} // namespace

int main(int argc, char **argv) {
    try {
        ground_tests();
        draw_tests();
        std::cout << "Terrain unit tests passed\n";
        if (argc > 1) {
            std::cout << check_original(argv[1]) << " original terrain frames matched pixel-for-pixel\n";
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Terrain test failed: " << error.what() << '\n';
        return 1;
    }
}
