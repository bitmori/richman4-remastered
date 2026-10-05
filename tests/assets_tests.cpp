// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/assets.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace rich4::assets;

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Operation>
void rejects(Operation operation, const char* message) {
    try {
        operation();
    } catch (const AssetError&) {
        return;
    }
    throw std::runtime_error(message);
}

void set_u16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8U);
}

void set_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8U * i));
    }
}

Bytes one_entry(const Bytes& payload, std::uint32_t decoded_size) {
    Bytes bytes(4 + 16 + payload.size() + 4, 0);
    set_u32(bytes, 0, static_cast<std::uint32_t>(bytes.size() - 4));
    set_u32(bytes, 4, decoded_size);
    set_u32(bytes, 8, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), bytes.begin() + 20);
    set_u32(bytes, bytes.size() - 4, 4);
    return bytes;
}

// Synthetic fixtures encode explicit symbols through the upstream initial
// offset-based tree, independently of the native decoder. No game data is used.
const Bytes abc{0xfb, 0x0f, 0x1c, 0x04};
const Bytes overlap{0xfb, 0x83, 0x0f, 0x00}; // Literal A, length 8, distance 1.
const Bytes abc_repeat{0xfb, 0x0f, 0x1c, 0x0c, 0xbc, 0x00}; // ABC + length 6, distance 3.

void compression_tests() {
    check(decompress(abc, 3) == Bytes({'A', 'B', 'C'}), "literal decoding");
    check(decompress(overlap, 9) == Bytes(9, 'A'), "overlapping match decoding");
    check(decompress(abc_repeat, 9) == Bytes({'A','B','C','A','B','C','A','B','C'}),
          "nontrivial match distance");
    check(decompress(overlap, 4) == Bytes(4, 'A'), "original final-match clipping");
    check(decompress({}, 0).empty(), "empty output must not read input");
    rejects([] { (void)decompress({}, 1); }, "empty input accepted");
    for (std::size_t length = 0; length < abc.size(); ++length) {
        rejects([length] { (void)decompress(std::span(abc).first(length), 3); },
                "truncated literal stream accepted");
    }
    rejects([] { (void)decompress(Bytes{0x7e, 0x07, 0x00}, 3); },
            "back-reference before output accepted");
    rejects([] { (void)decompress(Bytes{0x7e, 0x00, 0x3f}, 3); },
            "early terminator accepted");
    rejects([] { (void)decompress(abc, 3, 2); }, "decompression allocation limit ignored");

    // Independently encoded 65,000 literal As. After this prefix the adaptive
    // code is one bit; the run crosses the 0x8000 rescaling boundary repeatedly.
    const Bytes rescale_prefix{
        0xfb,0xfb,0x7a,0xbd,0xae,0xeb,0xba,0xd6,0x5a,0x6b,0xad,0xb5,0xaa,0xaa,0xaa,0xaa,
        0xaa,0xaa,0xaa,0xaa,0x24,0x49,0x92,0x24,0x49,0x92,0x24,0x49,0x92,0x24,0x49,0x12,
        0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0xd5,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80
    };
    Bytes rescale_stream(8165, 0xff);
    std::copy(rescale_prefix.begin(), rescale_prefix.end(), rescale_stream.begin());
    rescale_stream.back() = 0x7f;
    check(decompress(rescale_stream, 65000) == Bytes(65000, 'A'), "adaptive frequency rescaling");

    // Each call must own its adaptive state; this catches the old global tables.
    auto task = [] {
        for (int i = 0; i < 100; ++i) {
            check(decompress(abc_repeat, 9) == Bytes({'A','B','C','A','B','C','A','B','C'}),
                  "concurrent decoder state interference");
        }
    };
    auto first = std::async(std::launch::async, task);
    auto second = std::async(std::launch::async, task);
    first.get();
    second.get();
}

void archive_tests() {
    const Bytes payload{0x00, 0xff, 0x53, 0x80, 0x42};
    Archive plain(one_entry(payload, static_cast<std::uint32_t>(payload.size())));
    check(plain.size() == 1, "archive count");
    check(plain.decode(0) == payload, "raw bytes were modified");
    check(plain.metadata(0).offset == 4 && !plain.metadata(0).compressed(), "metadata");
    rejects([&] { (void)plain.metadata(1); }, "invalid metadata index accepted");
    rejects([&] { (void)plain.decode(1); }, "invalid decode index accepted");
    rejects([&] { (void)plain.decode(0, 4); }, "raw output allocation limit ignored");

    Archive packed(one_entry(abc_repeat, 9));
    check(packed.metadata(0).compressed(), "compressed metadata");
    check(packed.decode(0) == Bytes({'A','B','C','A','B','C','A','B','C'}), "archive decompression");
    check(Archive(Bytes{4,0,0,0}).size() == 0, "empty archive");
    check(Archive(one_entry({}, 0)).decode(0).empty(), "empty archive entry");
    rejects([] { (void)Archive(Bytes{}); }, "missing archive header accepted");
    rejects([] { (void)Archive(Bytes{3,0,0,0}); }, "index overlapping header accepted");
    rejects([] { (void)Archive(Bytes{8,0,0,0}); }, "out-of-range index accepted");
    rejects([] { (void)Archive(Bytes{4,0,0,0,0}); }, "partial index entry accepted");

    auto invalid = one_entry(payload, 5);
    set_u32(invalid, invalid.size() - 4, 0xffffffffU);
    rejects([&] { (void)Archive(invalid); }, "overflowing entry offset accepted");
    invalid = one_entry(payload, 5);
    set_u32(invalid, 8, 6);
    rejects([&] { (void)Archive(invalid); }, "payload overlapping index accepted");
    invalid = one_entry(payload, 5);
    set_u32(invalid, 12, 4);
    set_u32(invalid, 16, 2);
    rejects([&] { (void)Archive(invalid); }, "out-of-range graphics metadata accepted");
    invalid = one_entry(payload, 0xffffffffU);
    Archive excessive(invalid);
    rejects([&] { (void)excessive.decode(0); }, "huge declared output accepted");
}

Bytes sample_graphics(bool palette) {
    const std::size_t start = 36;
    const std::size_t pixels = start + (palette ? 512 : 0);
    Bytes bytes(pixels + 6, 0);
    set_u32(bytes, 0, palette ? 0x00525053U : 0x00504d53U);
    set_u32(bytes, 4, 2);
    set_u32(bytes, 8, static_cast<std::uint32_t>(start));
    set_u16(bytes, 12, 2);
    set_u16(bytes, 14, 1);
    set_u16(bytes, 16, 0xffffU); // Signed x = -1.
    set_u16(bytes, 18, 0x8000U); // Signed y = -32768.
    set_u32(bytes, 20, 4);
    set_u16(bytes, 24, 1);
    set_u16(bytes, 26, 1);
    set_u32(bytes, 32, 2);
    bytes[pixels] = 0x13;
    bytes[pixels + 5] = 0xfe;
    return bytes;
}

void graphics_tests() {
    for (const bool palette : {false, true}) {
        auto bytes = sample_graphics(palette);
        const auto original = bytes;
        const auto graphics = parse_graphics(bytes);
        check(bytes == original, "graphics parser modified source bytes");
        check(graphics.kind == (palette ? GraphicsKind::spr : GraphicsKind::smp), "graphics kind");
        check(graphics.entries.size() == 2, "graphics count");
        const auto& entry = graphics.entries.front();
        check(entry.width == 2 && entry.height == 1 && entry.x == -1 && entry.y == -32768,
              "fixed-width signed fields");
        check(entry.offset == (palette ? 548U : 36U) && entry.size == 4, "graphics offset/size");
        check(graphics.entries[1].offset == entry.offset + 4, "cumulative graphics offset");
        check(graphics.palette_offset == (palette ? std::optional<std::size_t>(36) : std::nullopt),
              "palette location");
        bytes.pop_back();
        rejects([&] { (void)parse_graphics(bytes); }, "truncated graphic accepted");
    }
    rejects([] { (void)parse_graphics(Bytes(11)); }, "short graphics header accepted");
    auto invalid = sample_graphics(false);
    invalid[3] = 1;
    rejects([&] { (void)parse_graphics(invalid); }, "non-NUL signature accepted");
    invalid = sample_graphics(false);
    set_u32(invalid, 4, 0xffffffffU);
    rejects([&] { (void)parse_graphics(invalid); }, "huge graphics count accepted");
    invalid = sample_graphics(false);
    set_u32(invalid, 8, 24);
    rejects([&] { (void)parse_graphics(invalid); }, "graphics overlapping table accepted");
    invalid = sample_graphics(true);
    invalid.resize(36 + 511);
    rejects([&] { (void)parse_graphics(invalid); }, "truncated palette accepted");
    invalid = sample_graphics(false);
    set_u32(invalid, 20, 0xffffffffU);
    rejects([&] { (void)parse_graphics(invalid); }, "overflowing graphics size accepted");
}

// Deterministic bounded malformed-input smoke corpus, useful under ASan/UBSan.
void malformed_corpus() {
    std::uint32_t random = 0x52494348U;
    auto next = [&] {
        random ^= random << 13U;
        random ^= random >> 17U;
        random ^= random << 5U;
        return random;
    };
    for (unsigned trial = 0; trial < 2000; ++trial) {
        Bytes bytes(next() % 64);
        for (auto& byte : bytes) {
            byte = static_cast<std::uint8_t>(next());
        }
        try { (void)Archive(bytes); } catch (const AssetError&) {}
        try { (void)parse_graphics(bytes); } catch (const AssetError&) {}
        try { (void)decompress(bytes, next() % 512, 512); } catch (const AssetError&) {}
    }
}
} // namespace

int main() {
    try {
        compression_tests();
        archive_tests();
        graphics_tests();
        malformed_corpus();
        std::cout << "Asset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset test failed: " << error.what() << '\n';
        return 1;
    }
}
