// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/graphics.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rich4::terrain {
inline constexpr std::size_t tile_side = 32;
inline constexpr std::size_t tile_pixels = tile_side * tile_side;

struct Ground {
    std::uint16_t width_tiles{};
    std::uint16_t height_tiles{};
    std::uint32_t reserved{};
    std::array<std::uint16_t, 256> palette{};
    std::vector<std::uint16_t> tile_indices;
    std::vector<std::uint8_t> tile_bytes;

    [[nodiscard]] std::size_t tile_count() const noexcept {
        return tile_bytes.size() / tile_pixels;
    }
    [[nodiscard]] std::span<const std::uint8_t, tile_pixels> tile(std::size_t index) const;
};

// GND little-endian disk values are parsed separately from host memory layouts.
// Rejects malformed dimensions, indices and truncated palette/index/tile data.
[[nodiscard]] Ground parse_ground(std::span<const std::uint8_t> decoded);
// Raw top-down texture atlas arranged by the map's tile indices, not the game camera.
[[nodiscard]] graphics::Image render_overview(const Ground &ground,
                                              std::size_t max_pixels = 16U * 1024U * 1024U);

struct Point {
    std::int16_t y{}, x{};
}; // Original corner order is y, then x.
using Quad = std::array<Point, 4>;

// Original 16.16 scan conversion, indexed8 sampling and opaque palette lookup.
// The native destination can have any positive dimensions, with explicit clip.
void draw_tile(graphics::Image &destination, std::span<const std::uint8_t, tile_pixels> texture,
               std::span<const std::uint16_t, 256> palette, const Quad &corners, graphics::Rect clip);
} // namespace rich4::terrain
