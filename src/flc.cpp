// SPDX-License-Identifier: GPL-3.0-or-later
// FLC decoding as performed by the original player fcn_0045144f / fcn_00450f04
// (.local/upstream/asm rich4.asm:25016-25526). See include/rich4/flc.hpp.
#include "rich4/flc.hpp"

#include <algorithm>

namespace rich4::flc {
namespace {
constexpr std::size_t header_size = 0x80;
constexpr std::uint16_t flc_magic = 0xAF12;
constexpr std::uint16_t frame_type = 0xF1FA;

std::uint16_t u16(std::span<const std::uint8_t> data, std::size_t at) {
    if (at > data.size() || data.size() - at < 2)
        throw FlcError("truncated FLC word");
    return static_cast<std::uint16_t>(data[at] | (static_cast<unsigned>(data[at + 1]) << 8U));
}
std::uint32_t u32(std::span<const std::uint8_t> data, std::size_t at) {
    if (at > data.size() || data.size() - at < 4)
        throw FlcError("truncated FLC double word");
    return static_cast<std::uint32_t>(u16(data, at)) | (static_cast<std::uint32_t>(u16(data, at + 2)) << 16U);
}
std::uint8_t u8(std::span<const std::uint8_t> data, std::size_t at) {
    if (at >= data.size())
        throw FlcError("truncated FLC byte");
    return data[at];
}
} // namespace

bool is_flc(std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= header_size && (bytes[4] | (bytes[5] << 8)) == flc_magic;
}

Decoder::Decoder(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    if (!is_flc(bytes_))
        throw FlcError("not an FLC movie (missing 0xAF12 header)");
    frame_count_ = u16(bytes_, 6);
    width_ = u16(bytes_, 8);
    height_ = u16(bytes_, 10);
    delay_ms_ = u32(bytes_, 16);
    if (width_ <= 0 || height_ <= 0 || width_ > 4096 || height_ > 4096)
        throw FlcError("FLC dimensions out of range");
    rewind();
}

void Decoder::rewind() {
    cursor_ = header_size;
    decoded_ = 0;
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0);
    palette_.fill(0);
}

bool Decoder::next() {
    if (decoded_ >= frame_count_)
        return false;
    const std::span<const std::uint8_t> data(bytes_);
    while (cursor_ + 6 <= data.size()) {
        const auto size = u32(data, cursor_);
        const auto type = u16(data, cursor_ + 4);
        if (size < 6 || size > data.size() - cursor_)
            throw FlcError("FLC chunk size outside the movie");
        const auto offset = cursor_;
        cursor_ += size;
        if (type != frame_type)
            continue; // 0xF100 prefix chunk and anything else at top level.
        apply_frame(offset, size);
        ++decoded_;
        return true;
    }
    return false;
}

void Decoder::apply_frame(std::size_t offset, std::size_t size) {
    const std::span<const std::uint8_t> frame = std::span<const std::uint8_t>(bytes_).subspan(offset, size);
    if (frame.size() < 16)
        throw FlcError("truncated FLC frame header");
    const auto chunks = u16(frame, 6);
    std::size_t at = 16;
    for (unsigned i = 0; i < chunks; ++i) {
        const auto chunk_size = u32(frame, at);
        const auto type = u16(frame, at + 4);
        if (chunk_size < 6 || chunk_size > frame.size() - at)
            throw FlcError("FLC sub-chunk size outside its frame");
        const auto body = frame.subspan(at + 6, chunk_size - 6);
        switch (type) {
        case 4:
            color_256(body);
            break;
        case 7:
            delta_flc(body);
            break;
        case 13:
            std::fill(pixels_.begin(), pixels_.end(), std::uint8_t{0});
            break;
        case 15:
            byte_run(body);
            break;
        case 16:
            copy(body);
            break;
        default: // 18 PSTAMP thumbnail and unknown chunks: ignored, as by the game.
            break;
        }
        at += chunk_size;
    }
}

void Decoder::color_256(std::span<const std::uint8_t> data) {
    const auto packets = u16(data, 0);
    std::size_t at = 2;
    unsigned index = 0;
    for (unsigned packet = 0; packet < packets; ++packet) {
        index += u8(data, at);
        unsigned count = u8(data, at + 1);
        if (!count)
            count = 256;
        at += 2;
        for (unsigned k = 0; k < count; ++k, at += 3) {
            const unsigned r = u8(data, at), g = u8(data, at + 1), b = u8(data, at + 2);
            palette_[(index + k) & 255U] = static_cast<std::uint16_t>(((r >> 3U) << 10U) | ((g >> 3U) << 5U) | (b >> 3U));
        }
        index += count;
    }
}

void Decoder::byte_run(std::span<const std::uint8_t> data) {
    const auto w = static_cast<std::size_t>(width_);
    std::size_t at = 0;
    for (std::size_t y = 0; y < static_cast<std::size_t>(height_); ++y) {
        ++at; // Obsolete packet count byte.
        std::size_t x = 0;
        while (x < w) {
            const auto count = static_cast<std::int8_t>(u8(data, at++));
            if (count == 0)
                throw FlcError("zero-length BYTE_RUN packet");
            if (count > 0) {
                const auto n = static_cast<std::size_t>(count);
                if (n > w - x)
                    throw FlcError("BYTE_RUN run past line end");
                std::fill_n(pixels_.begin() + static_cast<std::ptrdiff_t>(y * w + x), n, u8(data, at++));
                x += n;
            } else {
                const auto n = static_cast<std::size_t>(-static_cast<int>(count));
                if (n > w - x || n > data.size() - std::min(at, data.size()))
                    throw FlcError("BYTE_RUN literal past line end or chunk end");
                std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), n,
                            pixels_.begin() + static_cast<std::ptrdiff_t>(y * w + x));
                at += n;
                x += n;
            }
        }
    }
}

void Decoder::delta_flc(std::span<const std::uint8_t> data) {
    const auto w = static_cast<std::size_t>(width_), h = static_cast<std::size_t>(height_);
    unsigned lines = u16(data, 0);
    std::size_t at = 2, y = 0;
    while (lines > 0) {
        unsigned packets = 0;
        bool has_last = false;
        std::uint8_t last = 0;
        for (;;) {
            const auto op = u16(data, at);
            at += 2;
            if ((op & 0xC000U) == 0xC000U) {
                y += 0x10000U - op; // Negative count: skip lines.
                continue;
            }
            if ((op & 0xC000U) == 0x8000U) {
                last = static_cast<std::uint8_t>(op & 0xFFU);
                has_last = true;
                continue;
            }
            if (op & 0x4000U)
                throw FlcError("undefined DELTA_FLC opcode");
            packets = op;
            break;
        }
        if (y >= h)
            throw FlcError("DELTA_FLC line past frame end");
        auto *row = pixels_.data() + y * w;
        std::size_t x = 0;
        for (unsigned packet = 0; packet < packets; ++packet) {
            x += u8(data, at);
            const auto count = static_cast<std::int8_t>(u8(data, at + 1));
            at += 2;
            if (count >= 0) {
                const auto n = static_cast<std::size_t>(count) * 2U;
                if (x > w || n > w - x || at > data.size() || n > data.size() - at)
                    throw FlcError("DELTA_FLC literal past line or chunk end");
                std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), n, row + x);
                at += n;
                x += n;
            } else {
                const auto n = static_cast<std::size_t>(-static_cast<int>(count)) * 2U;
                const auto a = u8(data, at), b = u8(data, at + 1);
                at += 2;
                if (x > w || n > w - x)
                    throw FlcError("DELTA_FLC run past line end");
                for (std::size_t k = 0; k < n; k += 2) {
                    row[x + k] = a;
                    row[x + k + 1] = b;
                }
                x += n;
            }
        }
        if (has_last)
            row[w - 1] = last;
        ++y;
        --lines;
    }
}

void Decoder::copy(std::span<const std::uint8_t> data) {
    if (data.size() < pixels_.size())
        throw FlcError("truncated FLI_COPY chunk");
    std::copy_n(data.begin(), pixels_.size(), pixels_.begin());
}

graphics::Image Decoder::image() const {
    auto result = graphics::make_canvas(width_, height_);
    result.opaque_pixels.resize(pixels_.size());
    for (std::size_t i = 0; i < pixels_.size(); ++i) {
        const auto index = pixels_[i];
        result.pixels[i] = palette_[index];
        result.opaque_pixels[i] = index != 0 ? 1 : 0;
    }
    return result;
}

} // namespace rich4::flc
