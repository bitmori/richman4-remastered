// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from Rich4 drawing/pixel conversion behavior documented by Iru Cai.
#include "rich4/graphics.hpp"
#include "rich4/assets.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace rich4::graphics {
namespace {
std::size_t pixel_count(int width, int height) {
    if (width < 0 || height < 0 || width > 32767 || height > 32767)
        throw std::invalid_argument("image dimensions outside the asset format range");
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (count > 32U * 1024U * 1024U)
        throw std::invalid_argument("image exceeds the 32-million-pixel allocation limit");
    return count;
}
void validate(const Image& image) {
    if (image.pixels.size() != pixel_count(image.width, image.height))
        throw std::invalid_argument("pixel storage does not match image dimensions");
    if (!image.opaque_pixels.empty() && image.opaque_pixels.size() != image.pixels.size())
        throw std::invalid_argument("transparency mask does not match image dimensions");
}
std::uint16_t word(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2)
        throw assets::AssetError("truncated pixel or palette entry");
    return static_cast<std::uint16_t>(data[offset] | (static_cast<unsigned>(data[offset + 1]) << 8));
}
}

Image make_canvas(int width, int height, std::uint16_t fill) {
    return {width, height, 0, 0, std::vector<std::uint16_t>(pixel_count(width, height), fill), {}};
}

Image decode_frame(std::span<const std::uint8_t> resource, std::size_t frame) {
    const auto graphics = assets::parse_graphics(resource);
    if (frame >= graphics.entries.size()) throw std::out_of_range("frame index out of range");
    const auto& entry = graphics.entries[frame];
    Image result = make_canvas(entry.width, entry.height);
    result.origin_x = entry.x;
    result.origin_y = entry.y;
    const auto count = result.pixels.size();
    if (graphics.kind == assets::GraphicsKind::smp && entry.size == count * 2) {
        for (std::size_t i = 0; i < count; ++i) result.pixels[i] = word(resource, entry.offset + i * 2);
    } else if (graphics.kind == assets::GraphicsKind::spr && entry.size == count && graphics.palette_offset) {
        result.opaque_pixels.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = resource[entry.offset + i];
            result.pixels[i] = word(resource, *graphics.palette_offset + static_cast<std::size_t>(index) * 2);
            result.opaque_pixels[i] = index != 0 ? 1 : 0;
        }
    } else {
        throw assets::AssetError("frame is not a supported raw 16-bit or indexed 8-bit image");
    }
    return result;
}

void blit(Image& destination, const Image& source, int x, int y, Transparency transparency) {
    blit(destination, source, x, y, {0, 0, destination.width, destination.height}, transparency);
}

void blit(Image& destination, const Image& source, int x, int y, Rect clip, Transparency transparency) {
    validate(destination);
    validate(source);
    // Wide intermediates avoid overflow at extreme input coordinates. Intersect
    // both edges independently: the old helper could overrun for oversized sprites.
    const std::int64_t left = static_cast<std::int64_t>(x) - source.origin_x;
    const std::int64_t top = static_cast<std::int64_t>(y) - source.origin_y;
    const auto start_x = std::max<std::int64_t>({0, clip.left, left});
    const auto start_y = std::max<std::int64_t>({0, clip.top, top});
    const auto end_x = std::min<std::int64_t>({destination.width, clip.right, left + source.width});
    const auto end_y = std::min<std::int64_t>({destination.height, clip.bottom, top + source.height});
    if (start_x >= end_x || start_y >= end_y) return;
    // Source may alias destination; snapshot only when necessary.
    const std::vector<std::uint16_t> snapshot = &destination == &source ? source.pixels : std::vector<std::uint16_t>{};
    const auto& input = &destination == &source ? snapshot : source.pixels;
    for (auto row = start_y; row < end_y; ++row) {
        for (auto column = start_x; column < end_x; ++column) {
            const auto source_index = static_cast<std::size_t>((row - top) * source.width + column - left);
            const auto color = input[source_index];
            const bool visible = source.opaque_pixels.empty() ? color != 0 : source.opaque_pixels[source_index] != 0;
            if (transparency == Transparency::opaque || visible) {
                const auto destination_index = static_cast<std::size_t>(row * destination.width + column);
                destination.pixels[destination_index] = color;
            }
        }
    }
}

std::uint16_t rgb555_to_rgb565(std::uint16_t color) {
    // Exact original pixel_fmt=1 conversion. The sixth green bit remains zero.
    return static_cast<std::uint16_t>((color & 0x001fU) | ((static_cast<unsigned>(color) << 1U) & 0xffc0U));
}

std::vector<std::uint32_t> to_argb8888(const Image& image) {
    validate(image);
    std::vector<std::uint32_t> result;
    result.reserve(image.pixels.size());
    for (const auto raw : image.pixels) {
        const auto pixel = rgb555_to_rgb565(raw);
        const auto r = static_cast<std::uint32_t>((pixel >> 11) & 31) * 255U / 31U;
        const auto g = static_cast<std::uint32_t>((pixel >> 5) & 63) * 255U / 63U;
        const auto b = static_cast<std::uint32_t>(pixel & 31) * 255U / 31U;
        result.push_back(0xff000000U | (r << 16) | (g << 8) | b);
    }
    return result;
}

void write_ppm(const Image& image, const std::filesystem::path& path) {
    const auto rgba = to_argb8888(image);
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot create image: " + path.string());
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (const auto color : rgba) {
        const char rgb[] = {static_cast<char>((color >> 16) & 255), static_cast<char>((color >> 8) & 255), static_cast<char>(color & 255)};
        output.write(rgb, 3);
    }
    if (!output) throw std::runtime_error("failed to write image: " + path.string());
}
} // namespace rich4::graphics
