// SPDX-License-Identifier: GPL-3.0-or-later
// Renders every frame of a range of MKF entries into one labelled PPM, so UI
// artwork can be located by "entry:frame WxH" when wiring the native interface.
#include "native_ui.hpp"
#include "rich4/assets.hpp"
#include "rich4/graphics.hpp"

#include <iostream>
#include <tuple>

using namespace rich4;

int main(int argc, char **argv) {
    if (argc < 5) {
        std::cerr << "Usage: rich4-contact-sheet ARCHIVE.mkf FIRST LAST OUTPUT.ppm [MAX_FRAMES] [FONT]\n"
                     "Convert with: sips -s format png OUTPUT.ppm --out OUTPUT.png\n";
        return 2;
    }
    try {
        const assets::Archive archive{std::filesystem::path(argv[1])};
        const std::size_t first = std::stoul(argv[2]);
        const std::size_t last = std::min<std::size_t>(std::stoul(argv[3]), archive.size() - 1);
        const std::size_t max_frames = argc > 5 ? std::stoul(argv[5]) : 12;
        ui::Font font(argc > 6 ? argv[6] : "/System/Library/Fonts/Supplemental/Arial.ttf");
        struct Cell {
            std::size_t entry, frame;
            graphics::Image image;
        };
        std::vector<Cell> cells;
        for (std::size_t entry = first; entry <= last; ++entry) {
            std::vector<std::uint8_t> data;
            try {
                data = archive.decode(entry);
            } catch (const std::exception &) {
                continue;
            }
            for (std::size_t frame = 0; frame < max_frames; ++frame) {
                try {
                    cells.push_back({entry, frame, graphics::decode_frame(data, frame)});
                } catch (const std::exception &) {
                    break;
                }
            }
        }
        constexpr int sheet_width = 1600;
        int x = 0, y = 0, row_height = 0;
        std::vector<std::tuple<int, int, std::size_t>> positions;
        for (std::size_t index = 0; index < cells.size(); ++index) {
            const auto &image = cells[index].image;
            const int width = std::min(image.width, 640) + 4, height = std::min(image.height, 480) + 18;
            if (x + width > sheet_width) {
                x = 0;
                y += row_height;
                row_height = 0;
            }
            positions.emplace_back(x, y, index);
            x += width;
            row_height = std::max(row_height, height);
        }
        auto canvas = graphics::make_canvas(sheet_width, y + row_height, ui::color(255, 0, 255));
        for (const auto [left, top, index] : positions) {
            const auto &cell = cells[index];
            const auto &image = cell.image;
            graphics::blit(canvas, image, left + image.origin_x, top + 16 + image.origin_y,
                           graphics::Rect{left, top + 16, left + 640, top + 16 + 480});
            ui::fill(canvas, {left, top, left + std::max(40, std::min(image.width, 640)), top + 15},
                     ui::color(255, 255, 255));
            font.draw(canvas,
                      std::to_string(cell.entry) + ":" + std::to_string(cell.frame) + " " +
                          std::to_string(image.width) + "x" + std::to_string(image.height),
                      left + 1, top - 1, 11);
        }
        graphics::write_ppm(canvas, argv[4]);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
