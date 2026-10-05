// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic FLC streams for every supported chunk type, truncation robustness, and an optional
// real-asset pass: `flc_tests GAME_ASSET_DIR` (or RICH4_FLC_ASSETS) decodes every FLC movie in
// Data.mkf, Panel.mkf and Jump.mkf.
#include "rich4/assets.hpp"
#include "rich4/flc.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace rich4;
namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
using Bytes = std::vector<std::uint8_t>;
void put16(Bytes &b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8U));
}
void put32(Bytes &b, std::uint32_t v) {
    put16(b, v & 0xFFFFU);
    put16(b, v >> 16U);
}
Bytes chunk(unsigned type, const Bytes &body) {
    Bytes b;
    put32(b, static_cast<std::uint32_t>(body.size() + 6));
    put16(b, type);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
Bytes frame(const std::vector<Bytes> &chunks) {
    Bytes body(10, 0); // chunk count + 8 reserved, after the 6-byte chunk header
    body[0] = static_cast<std::uint8_t>(chunks.size());
    for (const auto &c : chunks)
        body.insert(body.end(), c.begin(), c.end());
    return chunk(0xF1FA, body);
}
Bytes movie(int width, int height, const std::vector<Bytes> &frames, bool prefix = true) {
    Bytes b(0x80, 0);
    b[4] = 0x12;
    b[5] = 0xAF;
    b[6] = static_cast<std::uint8_t>(frames.size());
    b[8] = static_cast<std::uint8_t>(width);
    b[10] = static_cast<std::uint8_t>(height);
    b[12] = 8;
    b[16] = 70; // delay ms
    if (prefix) {
        const auto p = chunk(0xF100, Bytes(10, 0xEE));
        b.insert(b.end(), p.begin(), p.end());
    }
    for (const auto &f : frames)
        b.insert(b.end(), f.begin(), f.end());
    b[0] = static_cast<std::uint8_t>(b.size());
    b[1] = static_cast<std::uint8_t>(b.size() >> 8U);
    return b;
}

// 4x2 movie exercising COLOR_256, BYTE_RUN, PSTAMP, DELTA_FLC (skip/last-pixel/runs), FLI_COPY, BLACK.
Bytes sample() {
    Bytes palette;
    put16(palette, 2);                                      // packets
    palette.insert(palette.end(), {0, 2, 255, 0, 0, 0, 255, 0}); // idx0 red, idx1 green
    palette.insert(palette.end(), {1, 1, 8, 16, 248});           // skip 1 -> idx3 = (8,16,248)
    Bytes run;
    // line 0: run 2 of idx1, literal 2: idx3 idx0 ; line 1: run 4 of idx0
    run.insert(run.end(), {0, 2, 1, static_cast<std::uint8_t>(-2), 3, 0});
    run.insert(run.end(), {0, 4, 0});
    const auto first = frame({chunk(4, palette), chunk(18, Bytes(20, 0x55)), chunk(15, run)});
    Bytes delta;
    put16(delta, 1);      // one line with packets
    put16(delta, 0xFFFF); // skip 1 line -> y = 1
    put16(delta, 0x8003); // last pixel of the line = 3
    put16(delta, 1);      // one packet
    delta.insert(delta.end(), {0, static_cast<std::uint8_t>(-1), 1, 3}); // repeat word (1,3) once at x0
    const auto second = frame({chunk(7, delta)});
    const auto third = frame({chunk(16, Bytes{3, 3, 3, 3, 1, 1, 1, 1})});
    Bytes delta2;
    put16(delta2, 1);
    put16(delta2, 1);
    delta2.insert(delta2.end(), {2, 1, 0, 1}); // skip 2, literal one word (0,1)
    const auto fourth = frame({chunk(7, delta2)});
    const auto fifth = frame({chunk(13, {})});
    return movie(4, 2, {first, second, third, fourth, fifth});
}

void synthetic() {
    flc::Decoder decoder(sample());
    check(decoder.width() == 4 && decoder.height() == 2 && decoder.frame_count() == 5, "header");
    check(decoder.delay_ms() == 70, "delay");
    check(decoder.next(), "frame 0");
    check(decoder.indices() == Bytes({1, 1, 3, 0, 0, 0, 0, 0}), "BYTE_RUN");
    check(decoder.palette()[0] == 0x7C00 && decoder.palette()[1] == 0x03E0, "COLOR_256 to RGB555");
    check(decoder.palette()[3] == ((1U << 10U) | (2U << 5U) | 31U), "COLOR_256 skip and >>3");
    const auto image = decoder.image();
    check(image.pixels[0] == 0x03E0 && image.opaque_pixels[0] == 1 && image.opaque_pixels[3] == 0,
          "index 0 transparent");
    check(decoder.next(), "frame 1");
    check(decoder.indices() == Bytes({1, 1, 3, 0, 1, 3, 0, 3}), "DELTA_FLC skip, word run, last pixel");
    check(decoder.next(), "frame 2");
    check(decoder.indices() == Bytes({3, 3, 3, 3, 1, 1, 1, 1}), "FLI_COPY");
    check(decoder.next(), "frame 3");
    check(decoder.indices() == Bytes({3, 3, 0, 1, 1, 1, 1, 1}), "DELTA_FLC literal");
    check(decoder.next(), "frame 4");
    check(decoder.indices() == Bytes(8, 0), "BLACK");
    check(!decoder.next(), "stops after frame_count");
    decoder.rewind();
    check(decoder.next() && decoder.indices() == Bytes({1, 1, 3, 0, 0, 0, 0, 0}), "rewind");
}

void robustness() {
    const auto good = sample();
    bool rejected_header = false;
    try {
        flc::Decoder bad(Bytes(good.begin(), good.begin() + 40));
    } catch (const flc::FlcError &) {
        rejected_header = true;
    }
    check(rejected_header, "short header rejected");
    // Every truncation and every single-byte corruption must either decode or throw FlcError.
    for (std::size_t size = 0x80; size < good.size(); ++size) {
        try {
            flc::Decoder decoder(Bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(size)));
            while (decoder.next())
                (void)decoder.image();
        } catch (const flc::FlcError &) {
        }
    }
    for (std::size_t at = 0x80; at < good.size(); ++at)
        for (const std::uint8_t value : {0x00, 0x01, 0x7F, 0x80, 0xFF}) {
            auto bytes = good;
            bytes[at] = value;
            try {
                flc::Decoder decoder(bytes);
                while (decoder.next())
                    (void)decoder.image();
            } catch (const flc::FlcError &) {
            }
        }
}

void real_assets(const std::filesystem::path &directory) {
    std::size_t movies = 0;
    for (const auto &[name, upper] : {std::pair{"Data.mkf", "DATA.MKF"}, std::pair{"Panel.mkf", "PANEL.MKF"},
                                      std::pair{"Jump.mkf", "JUMP.MKF"}}) {
        auto path = directory / name;
        if (!std::filesystem::exists(path))
            path = directory / upper;
        if (!std::filesystem::exists(path))
            continue;
        const assets::Archive archive(path);
        for (std::size_t entry = 0; entry < archive.size(); ++entry) {
            auto bytes = archive.decode(entry);
            if (!flc::is_flc(bytes))
                continue;
            flc::Decoder decoder(std::move(bytes));
            std::size_t frames = 0;
            while (decoder.next())
                ++frames;
            check(frames == decoder.frame_count(),
                  std::string(name) + " entry " + std::to_string(entry) + " frame count");
            ++movies;
        }
    }
    check(movies > 0, "no FLC movies found in " + directory.string());
    // Spot checks against docs/ui/effects.md.
    const assets::Archive data(directory / "Data.mkf");
    flc::Decoder missile(data.decode(528));
    check(missile.width() == 440 && missile.height() == 440 && missile.frame_count() == 19, "Data 528");
    const assets::Archive panel(directory / "Panel.mkf");
    flc::Decoder dice(panel.decode(4));
    check(dice.width() == 189 && dice.height() == 285 && dice.frame_count() == 36, "Panel 4");
    std::cout << "flc: decoded " << movies << " original movies\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        synthetic();
        robustness();
        const char *environment = std::getenv("RICH4_FLC_ASSETS");
        if (argc > 1)
            real_assets(argv[1]);
        else if (environment && *environment)
            real_assets(environment);
        std::cout << "flc tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "flc tests failed: " << error.what() << '\n';
        return 1;
    }
}
