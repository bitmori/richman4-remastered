/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * MKF format and adaptive decoder derived from mytbk/rich4:
 * Copyright (C) 2018 Iru Cai <mytbk920423@gmail.com> (mkf.c).
 * See docs/migrations/001-assets.md for provenance and compatibility rules.
 */
#include "rich4/assets.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace rich4::assets {
namespace {

void require_range(std::size_t offset, std::size_t length, std::size_t total, const char *description) {
    if (offset > total || length > total - offset) {
        throw AssetError(std::string(description) + " exceeds resource bounds");
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(offset, 2, bytes.size(), "16-bit field");
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                      (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U));
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    require_range(offset, 4, bytes.size(), "32-bit field");
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::int16_t read_i16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<std::int16_t>(read_u16(bytes, offset));
}

std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw AssetError("Cannot open archive: " + path.string());
    }
    const auto length = file.tellg();
    if (length < 0 ||
        static_cast<std::uintmax_t>(length) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max()) {
        throw AssetError("Unsupported archive size: " + path.string());
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        throw AssetError("Cannot read complete archive: " + path.string());
    }
    return bytes;
}

// Rich4 consumes each byte least-significant bit first. Separate byte/bit positions
// avoid overflowing a bit count for large inputs; no speculative word reads occur.
class BitReader {
  public:
    explicit BitReader(std::span<const std::uint8_t> source) : source_(source) {}

    std::uint32_t read(unsigned count) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (byte_ == source_.size()) {
                throw AssetError("Truncated compressed bitstream");
            }
            value |= static_cast<std::uint32_t>((source_[byte_] >> bit_) & 1U) << i;
            if (++bit_ == 8) {
                ++byte_;
                bit_ = 0;
            }
        }
        return value;
    }

    [[nodiscard]] std::uint32_t peek(unsigned count) const {
        auto copy = *this;
        return copy.read(count);
    }

  private:
    std::span<const std::uint8_t> source_;
    std::size_t byte_ = 0;
    unsigned bit_ = 0;
};

// 256 literal symbols and 65 match-length symbols. Internal nodes contain the
// index of the first of two consecutive children; leaves contain symbol+641.
// Upstream stored byte offsets and let parent[641..961] alias another struct
// member. Here every link is an explicit array index in a single valid array.
class AdaptiveTree {
  public:
    static constexpr std::size_t symbol_count = 321;
    static constexpr std::size_t node_count = 2 * symbol_count - 1;
    static constexpr std::size_t root = node_count - 1;

    AdaptiveTree() {
        for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
            frequency_[symbol] = 1;
            child_[symbol] = symbol + node_count;
            parent_[symbol + node_count] = symbol;
        }
        for (std::size_t node = symbol_count; node < node_count; ++node) {
            const auto first_child = 2 * (node - symbol_count);
            child_[node] = first_child;
            frequency_[node] =
                static_cast<std::uint16_t>(frequency_[first_child] + frequency_[first_child + 1]);
            parent_[first_child] = node;
            parent_[first_child + 1] = node;
        }
        parent_[root] = 0;
        frequency_[node_count] = 0xffffU; // Search sentinel, never a tree node.
    }

    std::size_t decode(BitReader &bits) {
        std::size_t node = root;
        while (child_[node] < node_count) {
            node = child_[node] + bits.read(1);
        }
        const auto symbol = child_[node] - node_count;
        if (frequency_[root] == 0x8000U) {
            rescale();
        }
        increment(symbol);
        return symbol;
    }

  private:
    std::array<std::uint16_t, node_count + 1> frequency_{};
    std::array<std::size_t, node_count> child_{};
    std::array<std::size_t, node_count + symbol_count> parent_{};

    void set_parent(std::size_t first_child, std::size_t node) {
        parent_[first_child] = node;
        if (first_child < node_count) {
            parent_[first_child + 1] = node;
        }
    }

    void increment(std::size_t symbol) {
        auto node = parent_[node_count + symbol];
        do {
            const auto increased = ++frequency_[node];
            if (increased > frequency_[node + 1]) {
                // Move after the last node with the old frequency, exactly as
                // the original adaptive-tree update does (stable tie order).
                auto target = node + 1;
                while (frequency_[target + 1] == increased - 1) {
                    ++target;
                }
                std::swap(frequency_[node], frequency_[target]);
                std::swap(child_[node], child_[target]);
                set_parent(child_[node], node);
                set_parent(child_[target], target);
                node = target;
            }
            node = parent_[node];
        } while (node != 0);
    }

    void rescale() {
        // Rounding odd leaf frequencies up through the existing update routine
        // preserves the original tree ordering before every frequency is halved.
        for (std::size_t symbol = 0; symbol < symbol_count; ++symbol) {
            if ((frequency_[parent_[node_count + symbol]] & 1U) != 0) {
                increment(symbol);
            }
        }
        for (std::size_t node = 0; node < node_count; ++node) {
            frequency_[node] >>= 1U;
        }
    }
};

#include "assets/distance_tables.inc"

void check_output_limit(std::size_t size, std::size_t maximum) {
    if (size > maximum) {
        throw AssetError("Decoded resource exceeds the configured output limit");
    }
}

} // namespace

Archive::Archive(const std::filesystem::path &path) : Archive(read_file(path)) {}

Archive::Archive(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    read_index();
}

void Archive::read_index() {
    const auto index_offset = read_u32(bytes_, 0);
    if (index_offset < 4 || index_offset > bytes_.size() || (bytes_.size() - index_offset) % 4 != 0) {
        throw AssetError("Invalid MKF index table");
    }
    const auto entry_count = (bytes_.size() - index_offset) / 4;
    entries_.reserve(entry_count);
    for (std::size_t index = 0; index < entry_count; ++index) {
        const auto offset = read_u32(bytes_, index_offset + 4 * index);
        if (offset < 4) {
            throw AssetError("MKF entry overlaps the archive header");
        }
        require_range(offset, 16, index_offset, "MKF entry header");
        EntryInfo entry{
            offset,
            read_u32(bytes_, static_cast<std::size_t>(offset) + 0),
            read_u32(bytes_, static_cast<std::size_t>(offset) + 4),
            read_u32(bytes_, static_cast<std::size_t>(offset) + 8),
            read_u32(bytes_, static_cast<std::size_t>(offset) + 12),
        };
        require_range(static_cast<std::size_t>(offset) + 16, entry.stored_size, index_offset,
                      "MKF entry payload");
        if (entry.graphics_size != 0) {
            require_range(entry.graphics_offset, entry.graphics_size, entry.decoded_size,
                          "MKF graphics metadata");
        }
        entries_.push_back(entry);
    }
}

const EntryInfo &Archive::metadata(std::size_t index) const {
    if (index >= entries_.size()) {
        throw AssetError("MKF entry index out of range: " + std::to_string(index));
    }
    return entries_[index];
}

std::vector<std::uint8_t> Archive::decode(std::size_t index, std::size_t max_output_bytes) const {
    const auto &entry = metadata(index);
    check_output_limit(entry.decoded_size, max_output_bytes);
    const auto stored = std::span<const std::uint8_t>(bytes_).subspan(
        static_cast<std::size_t>(entry.offset) + 16, entry.stored_size);
    if (!entry.compressed()) {
        return {stored.begin(), stored.end()};
    }
    return decompress(stored, entry.decoded_size, max_output_bytes);
}

std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> source, std::size_t decoded_size,
                                     std::size_t max_output_bytes) {
    check_output_limit(decoded_size, max_output_bytes);
    std::vector<std::uint8_t> output;
    output.reserve(decoded_size);
    BitReader bits(source);
    AdaptiveTree tree;
    while (output.size() < decoded_size) {
        const auto symbol = tree.decode(bits);
        if (symbol < 256) {
            output.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        const auto prefix = bits.peek(8);
        const auto prefix_length = distance_prefix_bits[prefix & 0xfU];
        (void)bits.read(prefix_length);
        const auto distance_code = (static_cast<std::size_t>(distance_prefix[prefix]) << 6U) | bits.read(6);
        if (distance_code == 0xfffU) {
            throw AssetError("Compressed resource ended before its declared decoded size");
        }
        const auto distance = distance_code + 1;
        if (distance > output.size()) {
            throw AssetError("Compressed back-reference precedes the decoded resource");
        }
        // Match the upstream C decoder's bounded-output contract. The original
        // EXE has no length argument and trusts EOS; malformed streams must not
        // be allowed to overwrite the caller's declared output allocation.
        const auto length = std::min(symbol - 0xfdU, decoded_size - output.size());
        for (std::size_t i = 0; i < length; ++i) {
            // Intentionally supports overlap: each emitted byte can become the
            // source for the following byte, unlike memcpy/memmove semantics.
            output.push_back(output[output.size() - distance]);
        }
    }
    return output;
}

Graphics parse_graphics(std::span<const std::uint8_t> decoded) {
    require_range(0, 12, decoded.size(), "SPR/SMP header");
    const auto signature = read_u32(decoded, 0);
    Graphics graphics{};
    if (signature == 0x00525053U) { // SPR\0
        graphics.kind = GraphicsKind::spr;
    } else if (signature == 0x00504d53U) { // SMP\0
        graphics.kind = GraphicsKind::smp;
    } else {
        throw AssetError("Resource is not SPR or SMP");
    }
    const auto count = read_u32(decoded, 4);
    if (count > (decoded.size() - 12) / 12) {
        throw AssetError("Truncated SPR/SMP entry table");
    }
    const std::size_t table_end = 12 + static_cast<std::size_t>(count) * 12;
    std::size_t data_offset = read_u32(decoded, 8);
    if (data_offset < table_end) {
        throw AssetError("SPR/SMP payload overlaps its entry table");
    }
    require_range(data_offset, 0, decoded.size(), "SPR/SMP payload");
    if (graphics.kind == GraphicsKind::spr) {
        require_range(data_offset, 512, decoded.size(), "SPR palette");
        graphics.palette_offset = data_offset;
        data_offset += 512;
    }
    graphics.entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto record = 12 + i * 12;
        const auto size = read_u32(decoded, record + 8);
        require_range(data_offset, size, decoded.size(), "SPR/SMP graphic");
        graphics.entries.push_back({read_i16(decoded, record), read_i16(decoded, record + 2),
                                    read_i16(decoded, record + 4), read_i16(decoded, record + 6), data_offset,
                                    size});
        data_offset += size;
    }
    return graphics;
}

} // namespace rich4::assets
