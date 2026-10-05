// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace rich4::assets {

class AssetError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline constexpr std::size_t default_max_output_bytes = 256U * 1024U * 1024U;

struct EntryInfo {
    std::uint32_t offset;
    std::uint32_t decoded_size;
    std::uint32_t stored_size;
    std::uint32_t graphics_offset;
    std::uint32_t graphics_size;

    [[nodiscard]] bool compressed() const noexcept { return decoded_size != stored_size; }
};

// Owns archive bytes, making const reads independent of file handles and other archives.
class Archive {
public:
    explicit Archive(const std::filesystem::path& path);
    explicit Archive(std::vector<std::uint8_t> bytes);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const EntryInfo& metadata(std::size_t index) const;
    // Returns exactly the on-disk payload after decompression. No palette/pointer fixups.
    [[nodiscard]] std::vector<std::uint8_t> decode(
        std::size_t index, std::size_t max_output_bytes = default_max_output_bytes) const;

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<EntryInfo> entries_;
    void read_index();
};

// Invalid/truncated streams throw AssetError. The caller supplies the decoded length.
[[nodiscard]] std::vector<std::uint8_t> decompress(
    std::span<const std::uint8_t> source, std::size_t decoded_size,
    std::size_t max_output_bytes = default_max_output_bytes);

enum class GraphicsKind { spr, smp };

struct GraphicEntry {
    std::int16_t width;
    std::int16_t height;
    std::int16_t x;
    std::int16_t y;
    std::size_t offset;  // Byte offset into the immutable decoded payload.
    std::uint32_t size; // Original gsize, not a host pointer or inferred pixel count.
};

struct Graphics {
    GraphicsKind kind;
    std::vector<GraphicEntry> entries;
    // SPR palettes occupy 512 raw bytes (256 little-endian 16-bit colors).
    std::optional<std::size_t> palette_offset;
};

// Does not decode sprite drawing commands or rewrite 16-bit source pixels.
[[nodiscard]] Graphics parse_graphics(std::span<const std::uint8_t> decoded);

} // namespace rich4::assets
