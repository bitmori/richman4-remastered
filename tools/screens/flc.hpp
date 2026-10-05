// SPDX-License-Identifier: GPL-3.0-or-later
// Autodesk FLC (0xAF12) decoder for the dice-roll animations Panel 4-6 (DICE1-1.FLC and
// friends). Palette index 0 is the transparency key, as with the SPR sprites.
#pragma once

#include "rich4/graphics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace rich4::native {

inline std::vector<graphics::Image> decode_flc(std::span<const std::uint8_t> data) {
    auto u8 = [&](std::size_t at) -> unsigned {
        if (at >= data.size())
            throw std::runtime_error("truncated FLC");
        return data[at];
    };
    auto u16 = [&](std::size_t at) { return u8(at) | (u8(at + 1) << 8); };
    auto u32 = [&](std::size_t at) { return u16(at) | (static_cast<std::uint32_t>(u16(at + 2)) << 16); };
    if (data.size() < 128 || u16(4) != 0xaf12)
        throw std::runtime_error("not an FLC animation");
    const unsigned frames = u16(6), width = u16(8), height = u16(10);
    if (!width || !height || width > 1024 || height > 1024 || frames > 1000)
        throw std::runtime_error("FLC dimensions out of range");
    std::vector<std::uint8_t> indices(std::size_t(width) * height);
    std::array<std::uint16_t, 256> palette{};
    std::vector<graphics::Image> result;
    std::size_t offset = u32(80) ? u32(80) : 128;
    while (result.size() < frames && offset + 16 <= data.size()) {
        const auto frame_size = u32(offset);
        const auto frame_type = u16(offset + 4);
        if (frame_size < 16)
            break;
        if (frame_type == 0xf1fa) {
            const unsigned chunks = u16(offset + 6);
            std::size_t chunk = offset + 16;
            for (unsigned c = 0; c < chunks && chunk + 6 <= offset + frame_size; ++c) {
                const auto chunk_size = u32(chunk);
                const auto type = u16(chunk + 4);
                std::size_t at = chunk + 6;
                if (type == 4 || type == 11) { // COLOR_256 / COLOR_64
                    const unsigned shift = type == 4 ? 3 : 1;
                    unsigned packets = u16(at), colour = 0;
                    at += 2;
                    while (packets--) {
                        colour += u8(at++);
                        unsigned count = u8(at++);
                        if (!count)
                            count = 256;
                        for (unsigned i = 0; i < count && colour < 256; ++i, ++colour, at += 3)
                            palette[colour] = static_cast<std::uint16_t>(
                                (std::min(31U, u8(at) >> shift) << 10) | (std::min(31U, u8(at + 1) >> shift) << 5) |
                                std::min(31U, u8(at + 2) >> shift));
                    }
                } else if (type == 15) { // BYTE_RUN
                    for (unsigned y = 0; y < height; ++y) {
                        ++at; // obsolete packet count
                        for (unsigned x = 0; x < width;) {
                            const auto count = static_cast<std::int8_t>(u8(at++));
                            if (count >= 0) {
                                const auto value = static_cast<std::uint8_t>(u8(at++));
                                for (int i = 0; i < count && x < width; ++i)
                                    indices[y * width + x++] = value;
                            } else
                                for (int i = 0; i < -count && x < width; ++i)
                                    indices[y * width + x++] = static_cast<std::uint8_t>(u8(at++));
                        }
                    }
                } else if (type == 7) { // DELTA_FLC (word oriented)
                    unsigned lines = u16(at), y = 0;
                    at += 2;
                    while (lines && y < height) {
                        const auto word = static_cast<std::int16_t>(u16(at));
                        at += 2;
                        if ((word & 0xc000) == 0xc000) {
                            y += static_cast<unsigned>(-word);
                            continue;
                        }
                        if ((word & 0xc000) == 0x8000) {
                            indices[y * width + width - 1] = static_cast<std::uint8_t>(word & 0xff);
                            continue;
                        }
                        unsigned x = 0;
                        for (int packet = 0; packet < word; ++packet) {
                            x += u8(at++);
                            const auto count = static_cast<std::int8_t>(u8(at++));
                            if (count >= 0)
                                for (int i = 0; i < count * 2; ++i, ++x) {
                                    if (x < width)
                                        indices[y * width + x] = static_cast<std::uint8_t>(u8(at));
                                    ++at;
                                }
                            else {
                                const auto a = static_cast<std::uint8_t>(u8(at)), b = static_cast<std::uint8_t>(u8(at + 1));
                                at += 2;
                                for (int i = 0; i < -count; ++i, x += 2) {
                                    if (x < width)
                                        indices[y * width + x] = a;
                                    if (x + 1 < width)
                                        indices[y * width + x + 1] = b;
                                }
                            }
                        }
                        ++y;
                        --lines;
                    }
                } else if (type == 12) { // DELTA_FLI (byte oriented)
                    unsigned y = u16(at), lines = u16(at + 2);
                    at += 4;
                    for (; lines-- && y < height; ++y) {
                        unsigned packets = u8(at++), x = 0;
                        while (packets--) {
                            x += u8(at++);
                            const auto count = static_cast<std::int8_t>(u8(at++));
                            if (count >= 0)
                                for (int i = 0; i < count; ++i, ++x) {
                                    if (x < width)
                                        indices[y * width + x] = static_cast<std::uint8_t>(u8(at));
                                    ++at;
                                }
                            else {
                                const auto value = static_cast<std::uint8_t>(u8(at++));
                                for (int i = 0; i < -count; ++i, ++x)
                                    if (x < width)
                                        indices[y * width + x] = value;
                            }
                        }
                    }
                } else if (type == 13) { // BLACK
                    std::fill(indices.begin(), indices.end(), 0);
                } else if (type == 16) { // FLI_COPY
                    for (std::size_t i = 0; i < indices.size(); ++i)
                        indices[i] = static_cast<std::uint8_t>(u8(at + i));
                }
                if (chunk_size < 6)
                    break;
                chunk += chunk_size;
            }
            graphics::Image image = graphics::make_canvas(int(width), int(height));
            image.opaque_pixels.resize(indices.size());
            for (std::size_t i = 0; i < indices.size(); ++i) {
                image.pixels[i] = palette[indices[i]];
                image.opaque_pixels[i] = indices[i] != 0;
            }
            result.push_back(std::move(image));
        }
        offset += frame_size;
    }
    return result;
}

} // namespace rich4::native
