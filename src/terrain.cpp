// SPDX-License-Identifier: GPL-3.0-or-later
// GND format and terrain conversion derived from mytbk/rich4; see migration 007.
#include "rich4/terrain.hpp"
#include "rich4/assets.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace rich4::terrain {
namespace {
void require_range(std::size_t offset, std::size_t count, std::size_t total, const char* message) {
    if (offset > total || count > total - offset) {
        throw assets::AssetError(message);
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(offset, 2, bytes.size(), "Truncated GND 16-bit field");
    return static_cast<std::uint16_t>(bytes[offset] |
                                      (static_cast<unsigned>(bytes[offset + 1]) << 8U));
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(offset, 4, bytes.size(), "Truncated GND 32-bit field");
    return static_cast<std::uint32_t>(read_u16(bytes, offset)) |
           (static_cast<std::uint32_t>(read_u16(bytes, offset + 2)) << 16U);
}
} // namespace

std::span<const std::uint8_t, tile_pixels> Ground::tile(std::size_t index) const {
    if (index >= tile_count()) {
        throw std::out_of_range("GND tile index out of range");
    }
    return std::span<const std::uint8_t, tile_pixels>(tile_bytes.data() + index * tile_pixels,
                                                      tile_pixels);
}

Ground parse_ground(std::span<const std::uint8_t> decoded) {
    require_range(0, 16, decoded.size(), "Truncated GND header");
    if (read_u32(decoded, 0) != 0x00444e47U) {
        throw assets::AssetError("Resource is not GND");
    }
    Ground ground;
    ground.width_tiles = read_u16(decoded, 4);
    ground.height_tiles = read_u16(decoded, 6);
    const auto count = read_u32(decoded, 8);
    ground.reserved = read_u32(decoded, 12);
    if (ground.width_tiles == 0 || ground.height_tiles == 0 || count == 0) {
        throw assets::AssetError("GND dimensions and tile count must be nonzero");
    }
    require_range(16, 512, decoded.size(), "Truncated GND palette");
    for (std::size_t i = 0; i < ground.palette.size(); ++i) {
        ground.palette[i] = read_u16(decoded, 16 + i * 2);
    }
    const auto index_count = static_cast<std::size_t>(ground.width_tiles) * ground.height_tiles;
    if (index_count > (decoded.size() - 528) / 2) {
        throw assets::AssetError("Truncated GND tile index");
    }
    const auto tile_offset = 528 + index_count * 2;
    if (count > (decoded.size() - tile_offset) / tile_pixels) {
        throw assets::AssetError("Truncated GND texture data");
    }
    ground.tile_indices.reserve(index_count);
    for (std::size_t i = 0; i < index_count; ++i) {
        const auto index = read_u16(decoded, 528 + i * 2);
        if (index >= count) {
            throw assets::AssetError("GND index references a nonexistent tile");
        }
        ground.tile_indices.push_back(index);
    }
    const auto tiles = decoded.subspan(tile_offset, static_cast<std::size_t>(count) * tile_pixels);
    ground.tile_bytes.assign(tiles.begin(), tiles.end());
    return ground;
}

graphics::Image render_overview(const Ground& ground, std::size_t max_pixels) {
    const auto width = static_cast<std::size_t>(ground.width_tiles) * tile_side;
    const auto height = static_cast<std::size_t>(ground.height_tiles) * tile_side;
    if (width == 0 || height == 0 || width > max_pixels / height) {
        throw std::invalid_argument("GND overview exceeds the configured pixel limit");
    }
    if (ground.tile_indices.size() !=
        static_cast<std::size_t>(ground.width_tiles) * ground.height_tiles) {
        throw std::invalid_argument("GND index count does not match dimensions");
    }
    auto image = graphics::make_canvas(static_cast<int>(width), static_cast<int>(height));
    for (std::size_t row = 0; row < ground.height_tiles; ++row) {
        for (std::size_t column = 0; column < ground.width_tiles; ++column) {
            const auto tile = ground.tile(ground.tile_indices[row * ground.width_tiles + column]);
            for (std::size_t y = 0; y < tile_side; ++y) {
                for (std::size_t x = 0; x < tile_side; ++x) {
                    image.pixels[(row * tile_side + y) * width + column * tile_side + x] =
                        ground.palette[tile[y * tile_side + x]];
                }
            }
        }
    }
    return image;
}

namespace {
constexpr std::int32_t empty_left = 0x7fff0000;
struct Scanline {
    std::int32_t left = empty_left;
    std::int32_t right = std::numeric_limits<std::int32_t>::min();
    std::int32_t left_u = 0, left_v = 0, right_u = 0, right_v = 0;
};
struct TexturePoint {
    std::int32_t u, v;
};

std::int32_t signed_bits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

std::int32_t fixed_integer(std::int32_t value) {
    return signed_bits(static_cast<std::uint32_t>(value) << 16U);
}

std::int32_t wrapped_subtract(std::int32_t first, std::int32_t second) {
    return signed_bits(static_cast<std::uint32_t>(first) - static_cast<std::uint32_t>(second));
}

std::int32_t advance_fixed(std::int32_t value, std::int32_t step, std::int32_t count = 1) {
    return signed_bits(static_cast<std::uint32_t>(value) +
                       static_cast<std::uint32_t>(step) * static_cast<std::uint32_t>(count));
}

std::int32_t floor_fixed(std::int32_t value) {
    const auto upper = static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) >> 16U);
    return std::bit_cast<std::int16_t>(upper);
}

void scan_edge(Point first, Point second, TexturePoint uv_first, TexturePoint uv_second,
               TexturePoint texture_delta, graphics::Rect clip, std::vector<Scanline>& lines,
               int& first_row) {
    const auto signed_height = static_cast<int>(second.y) - first.y;
    if (signed_height == 0) {
        return;
    }
    if (signed_height < -32768 || signed_height > 32767) {
        throw std::invalid_argument(
            "Terrain edge height exceeds the original signed-16-bit domain");
    }
    auto top = first;
    auto bottom = second;
    auto start_uv = uv_first;
    if (signed_height < 0) {
        std::swap(top, bottom);
        start_uv = uv_second;
    }
    if (bottom.y <= clip.top || top.y >= clip.bottom) {
        return;
    }
    const auto top_row = std::max(static_cast<int>(top.y), clip.top);
    const auto bottom_row = std::min(static_cast<int>(bottom.y), clip.bottom);
    first_row = std::min(first_row, top_row);
    const auto du = fixed_integer(texture_delta.u) / signed_height;
    const auto dv = fixed_integer(texture_delta.v) / signed_height;
    auto u = signed_bits(static_cast<std::uint32_t>(fixed_integer(start_uv.u)) |
                         (du < 0 ? 0xffffU : 0U));
    auto v = signed_bits(static_cast<std::uint32_t>(fixed_integer(start_uv.v)) |
                         (dv < 0 ? 0xffffU : 0U));
    auto x = fixed_integer(top.x);
    const auto dx = wrapped_subtract(fixed_integer(bottom.x), x) /
                    (signed_height < 0 ? -signed_height : signed_height);
    const auto skipped = top_row - top.y;
    u = advance_fixed(u, du, skipped);
    v = advance_fixed(v, dv, skipped);
    x = advance_fixed(x, dx, skipped);
    for (int y = top_row; y < bottom_row; ++y) {
        auto& line = lines[static_cast<std::size_t>(y)];
        const auto store_uv = [&] {
            // UV side follows the edge's original vertical direction, even
            // when that edge updates the opposite X bound (reverse winding).
            if (signed_height < 0) {
                line.left_u = u;
                line.left_v = v;
            } else {
                line.right_u = u;
                line.right_v = v;
            }
        };
        if (x < line.left) {
            line.left = x;
            store_uv();
        }
        if (x > line.right) {
            line.right = x;
            store_uv();
        }
        u = advance_fixed(u, du);
        v = advance_fixed(v, dv);
        x = advance_fixed(x, dx);
    }
}
} // namespace

void draw_tile(graphics::Image& destination, std::span<const std::uint8_t, tile_pixels> texture,
               std::span<const std::uint16_t, 256> palette, const Quad& corners,
               graphics::Rect clip) {
    if (destination.width <= 0 || destination.height <= 0 || destination.width > 32767 ||
        destination.height > 32767 ||
        destination.pixels.size() != static_cast<std::size_t>(destination.width) *
                                         static_cast<std::size_t>(destination.height)) {
        throw std::invalid_argument("Invalid terrain destination dimensions or storage");
    }
    if (!destination.opaque_pixels.empty() &&
        destination.opaque_pixels.size() != destination.pixels.size()) {
        throw std::invalid_argument("Invalid terrain destination opacity storage");
    }
    clip.left = std::max(clip.left, 0);
    clip.top = std::max(clip.top, 0);
    clip.right = std::min(clip.right, destination.width);
    clip.bottom = std::min(clip.bottom, destination.height);
    if (clip.left >= clip.right || clip.top >= clip.bottom) {
        return;
    }
    constexpr std::array<TexturePoint, 4> uv{{{0, 0}, {31, 0}, {31, 31}, {0, 31}}};
    constexpr std::array<TexturePoint, 4> delta{{{32, 0}, {0, 32}, {-32, 0}, {0, -32}}};
    std::vector<Scanline> lines(static_cast<std::size_t>(destination.height));
    auto first_row = destination.height;
    for (std::size_t edge = 0; edge < 4; ++edge) {
        const auto next = (edge + 1) % 4;
        scan_edge(corners[edge], corners[next], uv[edge], uv[next], delta[edge], clip, lines,
                  first_row);
    }
    for (int y = first_row; y < destination.height; ++y) {
        const auto& line = lines[static_cast<std::size_t>(y)];
        // The original exits at the first untouched scanline, not merely at the
        // bottom clipping boundary. Each native call owns fresh scratch state.
        if (line.left == empty_left) {
            break;
        }
        const auto left = floor_fixed(line.left);
        const auto right = floor_fixed(line.right);
        const auto span = right - left;
        if (span <= 0 || right <= clip.left || left >= clip.right) {
            continue;
        }
        const auto du = wrapped_subtract(line.right_u, line.left_u) / span;
        const auto dv = wrapped_subtract(line.right_v, line.left_v) / span;
        const auto begin = std::max(left, clip.left);
        const auto end = std::min(right, clip.right);
        auto u = advance_fixed(line.left_u, du, begin - left);
        auto v = advance_fixed(line.left_v, dv, begin - left);
        for (int x = begin; x < end; ++x) {
            const auto texture_x = (static_cast<std::uint32_t>(u) >> 16U) & 31U;
            const auto texture_y = (static_cast<std::uint32_t>(v) >> 16U) & 31U;
            const auto pixel =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(destination.width) +
                static_cast<std::size_t>(x);
            destination.pixels[pixel] = palette[texture[texture_y * 32U + texture_x]];
            if (!destination.opaque_pixels.empty()) {
                destination.opaque_pixels[pixel] = 1;
            }
            u = advance_fixed(u, du);
            v = advance_fixed(v, dv);
        }
    }
}

} // namespace rich4::terrain
