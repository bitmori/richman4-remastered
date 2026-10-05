// SPDX-License-Identifier: GPL-3.0-or-later
// Autodesk FLC movies as stored in Data.mkf / Panel.mkf / Jump.mkf (docs/ui/effects.md §0).
// Portable: no SDL. Every read and write is bounds-checked; corrupt or truncated input throws
// FlcError and never touches memory outside the decoded buffers.
#pragma once

#include "rich4/graphics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace rich4::flc {

class FlcError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// 128-byte header: +4 magic 0xAF12, +6 frame count, +8 width, +10 height, +16 delay ms.
[[nodiscard]] bool is_flc(std::span<const std::uint8_t> bytes) noexcept;

// Sequential decoder. Supported frame chunks: 4 COLOR_256 (8-bit RGB), 7 DELTA_FLC (word RLE),
// 13 BLACK, 15 BYTE_RUN, 16 FLI_COPY; 18 PSTAMP and unknown chunks are skipped, as is the
// 0xF100 prefix chunk the game skips (fcn_00450f04).
class Decoder {
  public:
    explicit Decoder(std::vector<std::uint8_t> bytes);

    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] std::uint32_t delay_ms() const noexcept { return delay_ms_; }
    // Number of frames decoded since construction / rewind().
    [[nodiscard]] std::size_t decoded_frames() const noexcept { return decoded_; }

    // Applies the next frame. Returns false (state unchanged) once frame_count() frames
    // have been decoded or no further frame chunk exists.
    bool next();
    void rewind();

    // Current 8-bit frame buffer and its palette converted to RGB555 exactly like the
    // original (fcn_0045520d, pixel_fmt 0: each channel >> 3).
    [[nodiscard]] const std::vector<std::uint8_t> &indices() const noexcept { return pixels_; }
    [[nodiscard]] const std::array<std::uint16_t, 256> &palette() const noexcept { return palette_; }

    // Overlay transparency choice: palette INDEX 0 is transparent (opaque_pixels = 0), matching
    // the original overlay delta writer fcn_004506c7, which substitutes the background pixel
    // wherever the FLC index is 0. Blit with Transparency::source_key for overlays, or
    // Transparency::opaque for the original opaque blit (flags bit 0 clear).
    [[nodiscard]] graphics::Image image() const;

  private:
    void apply_frame(std::size_t offset, std::size_t size);
    void color_256(std::span<const std::uint8_t> data);
    void byte_run(std::span<const std::uint8_t> data);
    void delta_flc(std::span<const std::uint8_t> data);
    void copy(std::span<const std::uint8_t> data);

    std::vector<std::uint8_t> bytes_;
    int width_ = 0, height_ = 0;
    std::size_t frame_count_ = 0;
    std::uint32_t delay_ms_ = 0;
    std::size_t cursor_ = 0, decoded_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::array<std::uint16_t, 256> palette_{};
};

} // namespace rich4::flc
