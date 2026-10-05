// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/graphics.hpp"
#include "rich4/assets.hpp"
#include <climits>
#include <iostream>
#include <stdexcept>

using namespace rich4::graphics;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void set_word(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
void sprite_tests() {
    // A synthetic two-pixel sprite. Palette[0] is deliberately nonzero and
    // palette[1] is zero: transparency belongs to the index, not the color.
    std::vector<std::uint8_t> bytes(24 + 512 + 2);
    bytes[0] = 'S'; bytes[1] = 'P'; bytes[2] = 'R';
    bytes[4] = 1; bytes[8] = 24;
    set_word(bytes, 12, 2); set_word(bytes, 14, 1);
    set_word(bytes, 16, 0xffff); set_word(bytes, 18, 3);
    bytes[20] = 2;
    set_word(bytes, 24, 0x7c00);
    bytes[536] = 0; bytes[537] = 1;
    const auto source = decode_frame(bytes, 0);
    check(source.origin_x == -1 && source.origin_y == 3, "signed sprite anchors");
    check(source.pixels == std::vector<std::uint16_t>({0x7c00, 0}), "SPR palette lookup");
    auto destination = make_canvas(2, 1, 9);
    blit(destination, source, -1, 3);
    check(destination.pixels == std::vector<std::uint16_t>({9, 0}), "SPR transparent index zero, opaque black");
    blit(destination, source, -1, 3, Transparency::opaque);
    check(destination.pixels == source.pixels, "SPR opaque mode preserves palette zero");
    bytes.resize(28); bytes[1] = 'M'; bytes[2] = 'P'; bytes[20] = 4;
    set_word(bytes, 24, 0x1234); set_word(bytes, 26, 0x5678);
    const auto raw = decode_frame(bytes, 0);
    check(raw.pixels == std::vector<std::uint16_t>({0x1234, 0x5678}), "SMP little-endian raw pixels");
    check(raw.opaque_pixels.empty(), "SMP uses raw zero-color key");
    bytes[20] = 3; bytes.resize(27);
    bool rejected{};
    try { (void)decode_frame(bytes, 0); } catch (const rich4::assets::AssetError&) { rejected = true; }
    check(rejected, "pixel byte count must match dimensions");
}
}

int main() {
    try {
        sprite_tests();
        auto destination = make_canvas(3, 2, 9);
        Image source{2, 2, 1, 1, {1, 0, 3, 4}};
        blit(destination, source, 2, 1);
        check(destination.pixels == std::vector<std::uint16_t>({9, 1, 9, 9, 3, 4}), "anchor/transparent copy");
        destination = make_canvas(3, 2, 9);
        blit(destination, source, 0, 0, Transparency::opaque);
        check(destination.pixels == std::vector<std::uint16_t>({4, 9, 9, 9, 9, 9}), "negative-coordinate clipping");
        destination = make_canvas(3, 2, 9);
        Image wide{7, 1, 0, 0, {1, 2, 3, 4, 5, 6, 7}};
        blit(destination, wide, -2, 1);
        check(destination.pixels == std::vector<std::uint16_t>({9, 9, 9, 3, 4, 5}), "both-edge clipping must stay in bounds");
        destination = make_canvas(3, 2, 9);
        blit(destination, source, 2, 1, {2, 1, 3, 2});
        check(destination.pixels == std::vector<std::uint16_t>({9, 9, 9, 9, 9, 4}), "explicit clip rectangle");
        blit(destination, source, INT_MAX, INT_MIN);
        check(destination.pixels.back() == 4, "extreme coordinates must not wrap");
        Image overlap{4, 1, 0, 0, {1, 2, 3, 4}};
        blit(overlap, overlap, 1, 0);
        check(overlap.pixels == std::vector<std::uint16_t>({1, 1, 2, 3}), "self-copy snapshot");
        check(rgb555_to_rgb565(0x7fff) == 0xffdf, "original white conversion keeps green low bit zero");
        check(rgb555_to_rgb565(0x7c00) == 0xf800, "red conversion");
        check(rgb555_to_rgb565(0x03e0) == 0x07c0, "green conversion");
        check(rgb555_to_rgb565(0x8000) == 0, "unused high bit is discarded");
        auto color_image = make_canvas(1, 1, 0x7c00);
        check(to_argb8888(color_image)[0] == 0xffff0000, "display channel order");
        auto invalid = make_canvas(2, 2);
        invalid.pixels.pop_back();
        bool rejected{};
        try { blit(destination, invalid, 0, 0); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "malformed image storage must be rejected");
        std::cout << "graphics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
