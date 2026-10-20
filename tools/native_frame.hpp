// SPDX-License-Identifier: GPL-3.0-or-later
// Shared drawing kit for the native game's screens. Each UI component draws into
// the 640x480 canvas through a Frame and registers its hit areas as Buttons; the
// Application dispatches clicks by button id (the replay tests depend on stable ids).
#pragma once

#include "native_ui.hpp"
#include "rich4/assets.hpp"
#include "rich4/graphics.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <tuple>

namespace rich4::native {

inline constexpr std::array<const char *, 12> names{"約翰喬", "沙隆巴斯", "忍太郎",   "錢夫人",
                                                    "阿土伯", "莎拉公主", "宮本寶藏", "糖糖",
                                                    "烏咪",   "孫小美",   "小丹尼",   "金貝貝"};
inline constexpr std::array<const char *, 4> map_names{"台灣", "中國", "日本", "美國"};

// Archives are opened lazily: 'd' Data, 'p' Panel, 'm' Map, 'e' Effect, 's' Speaking, 'j' Jump, 'h' Help.
class Artwork {
  public:
    explicit Artwork(std::filesystem::path directory) : directory_(std::move(directory)) {}
    assets::Archive &archive(char id) {
        auto &slot = archives_[id];
        if (!slot) {
            const char *file = id == 'd'   ? "Data.mkf"
                               : id == 'p' ? "Panel.mkf"
                               : id == 'm' ? "Map.mkf"
                               : id == 'e' ? "Effect.mkf"
                               : id == 's' ? "Speaking.mkf"
                               : id == 'j' ? "Jump.mkf"
                                           : "Help.mkf";
            slot = std::make_unique<assets::Archive>(ui::asset_path(directory_, file));
        }
        return *slot;
    }
    const graphics::Image &frame(char id, std::size_t entry, std::size_t frame_index) {
        const auto key = std::tuple(id, entry, frame_index);
        if (const auto found = images_.find(key); found != images_.end())
            return found->second;
        auto &decoded = resources_[std::pair(id, entry)];
        if (decoded.empty())
            decoded = archive(id).decode(entry);
        return images_.emplace(key, graphics::decode_frame(decoded, frame_index)).first->second;
    }
    // Number of frames in a graphics entry (0 when the entry holds no graphics).
    std::size_t frame_count(char id, std::size_t entry) {
        auto &decoded = resources_[std::pair(id, entry)];
        if (decoded.empty())
            decoded = archive(id).decode(entry);
        try {
            return assets::parse_graphics(decoded).entries.size();
        } catch (const std::exception &) {
            return 0;
        }
    }

  private:
    std::filesystem::path directory_;
    std::map<char, std::unique_ptr<assets::Archive>> archives_;
    std::map<std::pair<char, std::size_t>, std::vector<std::uint8_t>> resources_;
    std::map<std::tuple<char, std::size_t, std::size_t>, graphics::Image> images_;
};

struct Button {
    int id;
    graphics::Rect rectangle;
    bool enabled;
    std::function<void()> action;
    bool click_sound = true; // false: the action plays its own original UI effect
};

// Per-frame drawing context. `pointer` is the logical mouse/touch position (or -1)
// so components can draw hover/pressed states.
class Frame {
  public:
    Frame(graphics::Image &canvas, Artwork &artwork, ui::Font &font, std::vector<Button> &buttons)
        : canvas(canvas), artwork(artwork), font(font), buttons(buttons) {}

    // Draws a sprite honouring its stored origin offset, like the original graph_st x/y.
    void picture(const graphics::Image &image, int x, int y) {
        graphics::blit(canvas, image, x + image.origin_x, y + image.origin_y);
    }
    void picture(char archive, std::size_t entry, std::size_t frame_index, int x, int y) {
        picture(artwork.frame(archive, entry, frame_index), x, y);
    }
    // Copies the whole frame, black (colour zero) included, like the original _draw_image_in_rect
    // (fcn_00456280 / fcn_004563f5); e.g. the Data 2 portraits, whose black outlines are not holes.
    void opaque_picture(char archive, std::size_t entry, std::size_t frame_index, int x, int y) {
        const auto &image = artwork.frame(archive, entry, frame_index);
        graphics::blit(canvas, image, x + image.origin_x, y + image.origin_y, graphics::Transparency::opaque);
    }
    void hit(int id, graphics::Rect rect, std::function<void()> action, bool enabled = true) {
        buttons.push_back({id, rect, enabled, std::move(action)});
    }
    void button(int id, graphics::Rect rect, std::string_view label, std::function<void()> action,
                bool enabled = true) {
        hit(id, rect, std::move(action), enabled);
        ui::fill(canvas, rect, enabled ? ui::paper : ui::color(190, 183, 158));
        ui::border(canvas, rect, ui::color(106, 72, 28));
        int size = 16;
        while (size > 12 && font.measure(label, size) > rect.right - rect.left - 8)
            size -= 2;
        font.draw(canvas, label, rect.left + (rect.right - rect.left - font.measure(label, size)) / 2,
                  rect.top + (rect.bottom - rect.top - size - 3) / 2, size,
                  enabled ? ui::ink : ui::color(115, 110, 90));
    }
    void dialog(graphics::Rect rect, std::string_view title) {
        ui::fill(canvas, rect, ui::paper);
        ui::border(canvas, rect, ui::color(110, 74, 26));
        ui::border(canvas, {rect.left + 3, rect.top + 3, rect.right - 3, rect.bottom - 3},
                   ui::color(190, 146, 69));
        font.draw(canvas, title, rect.left + 18, rect.top + 15, 20, ui::blue);
    }
    void text(std::string_view value, int x, int y, int size = 16, std::uint16_t colour = ui::ink,
              int wrap_width = 0) {
        font.draw(canvas, value, x, y, size, colour, wrap_width);
    }
    void centered(std::string_view value, int centre_x, int y, int size = 16, std::uint16_t colour = ui::ink) {
        font.draw(canvas, value, centre_x - font.measure(value, size) / 2, y, size, colour);
    }
    bool pointer_in(graphics::Rect rect) const {
        return ui::contains(rect, pointer_x, pointer_y);
    }

    graphics::Image &canvas;
    Artwork &artwork;
    ui::Font &font;
    std::vector<Button> &buttons;
    int pointer_x = -1, pointer_y = -1;
    bool pointer_down = false;
    std::uint64_t ticks = 0;
};

} // namespace rich4::native
