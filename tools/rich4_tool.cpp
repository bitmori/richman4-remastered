// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4/assets.hpp"
#include "rich4/graphics.hpp"
#include "rich4/save.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
std::size_t index_of(std::string_view text) {
    std::size_t result{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("expected a non-negative integer index");
    return result;
}
void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot create " + path.string());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("write failed: " + path.string());
}
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void usage() {
    std::cerr << "Usage:\n"
              << "  rich4-tool inspect ARCHIVE.mkf\n"
              << "  rich4-tool dump ARCHIVE.mkf OUTPUT_DIRECTORY\n"
              << "  rich4-tool extract ARCHIVE.mkf ENTRY OUTPUT_FILE\n"
              << "  rich4-tool frame ARCHIVE.mkf ENTRY FRAME OUTPUT.ppm\n"
              << "  rich4-tool roundtrip-save INPUT.dat OUTPUT.dat\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 3) { usage(); return 2; }
        const std::string_view command(argv[1]);
        if (command == "roundtrip-save" && argc == 4) {
            const auto original = read_bytes(argv[2]);
            const auto save = rich4::SaveFile::decode(original);
            const auto encoded = save.encode();
            if (original != encoded) throw std::runtime_error("save roundtrip changed bytes");
            write_bytes(argv[3], encoded);
            std::cout << "{\"bytes\":" << encoded.size() << ",\"identical\":true}\n";
            return 0;
        }
        if (command != "inspect" && command != "dump" && command != "extract" && command != "frame") {
            usage(); return 2;
        }
        const rich4::assets::Archive archive(argv[2]);
        if (command == "inspect" && argc == 3) {
            std::size_t compressed{}, decoded_bytes{}, graphical_entries{}, frames{}, pixels{};
            for (std::size_t i = 0; i < archive.size(); ++i) {
                const auto& entry = archive.metadata(i);
                compressed += entry.compressed() ? 1U : 0U;
                decoded_bytes += entry.decoded_size;
                const auto data = archive.decode(i);
                const bool signature = data.size() >= 4 && data[0] == 'S' && data[3] == 0 &&
                    ((data[1] == 'M' && data[2] == 'P') || (data[1] == 'P' && data[2] == 'R'));
                if (signature) {
                    ++graphical_entries;
                    const auto parsed = rich4::assets::parse_graphics(data);
                    frames += parsed.entries.size();
                    for (std::size_t frame = 0; frame < parsed.entries.size(); ++frame)
                        pixels += rich4::graphics::decode_frame(data, frame).pixels.size();
                }
            }
            std::cout << "{\"entries\":" << archive.size() << ",\"compressed\":" << compressed
                      << ",\"decoded_bytes\":" << decoded_bytes << ",\"graphic_resources\":" << graphical_entries
                      << ",\"frames\":" << frames << ",\"decoded_pixels\":" << pixels << "}\n";
        } else if (command == "dump" && argc == 4) {
            const std::filesystem::path directory(argv[3]);
            std::filesystem::create_directories(directory);
            for (std::size_t i = 0; i < archive.size(); ++i) {
                std::ostringstream name;
                name << std::setfill('0') << std::setw(6) << i << ".bin";
                write_bytes(directory / name.str(), archive.decode(i));
            }
            std::cout << "{\"entries\":" << archive.size() << "}\n";
        } else if (command == "extract" && argc == 5) {
            write_bytes(argv[4], archive.decode(index_of(argv[3])));
        } else if (command == "frame" && argc == 6) {
            const auto data = archive.decode(index_of(argv[3]));
            rich4::graphics::write_ppm(rich4::graphics::decode_frame(data, index_of(argv[4])), argv[5]);
        } else { usage(); return 2; }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rich4-tool: " << error.what() << '\n';
        return 1;
    }
}
