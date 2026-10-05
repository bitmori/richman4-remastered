// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace rich4::graphics {

// Asset colors are stored as RGB555 words. Indexed SPR sprites use index zero
// as their transparency key, independently of the color in palette slot zero.
struct Image {
    int width{};
    int height{};
    int origin_x{};
    int origin_y{};
    std::vector<std::uint16_t> pixels;
    // Empty for raw SMP/canvas images (color zero is transparent). SPR images
    // keep one byte per pixel: 0 for transparent index zero, 1 otherwise.
    std::vector<std::uint8_t> opaque_pixels;
};

struct Rect {
    int left{}, top{}, right{}, bottom{};
}; // right/bottom are exclusive
enum class Transparency { opaque, source_key };

Image decode_frame(std::span<const std::uint8_t> resource, std::size_t frame);
Image make_canvas(int width, int height, std::uint16_t fill = 0);
// Anchor positioning matches the source graph_st x/y semantics.
void blit(Image &destination, const Image &source, int anchor_x, int anchor_y,
          Transparency transparency = Transparency::source_key);
void blit(Image &destination, const Image &source, int anchor_x, int anchor_y, Rect clip,
          Transparency transparency = Transparency::source_key);
std::uint16_t rgb555_to_rgb565(std::uint16_t value);
std::vector<std::uint32_t> to_argb8888(const Image &image);
void write_ppm(const Image &image, const std::filesystem::path &path);

} // namespace rich4::graphics
