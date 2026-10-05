// SPDX-License-Identifier: GPL-3.0-or-later
// The original Help screen (_rich4_ui_help_entry, rich4_ui_help.asm): a 400x400 window on
// help.mkf 0 with 8 category buttons (green), the category's topic list (red, 8 rows, scrolled
// by 8 with 0:4/0:5) and the topic's help.mkf text (yellow, 14 lines per page; '@' lines break
// pages, flipped with 0:8/0:9). Pure reference text: no game state is read or changed.
// Touch: every target's hit area is enlarged to finger size, a tap on the text pane turns the
// page, and a tap outside the window (or Back / right-click) closes it.
#pragma once

#include "draw.hpp"

#include <array>
#include <string>
#include <vector>

namespace rich4::native::screens {

namespace ids {
inline constexpr int help_category = 1120, // + category 0..7
    help_topic = 1130,                     // + visible row 0..7
    help_list_up = 1140, help_list_down = 1141, help_page_prev = 1142, help_page_next = 1143,
                     help_text = 1144, help_window = 1145, help_outside = 1146;
} // namespace ids

class HelpScreen {
  public:
    // Toolbar "?" opens at (20, 60); Options passes (-1, -1) = centred (ui_options.asm:2719).
    void open(int x, int y);
    void close() noexcept { open_ = false; }
    [[nodiscard]] bool active() const noexcept { return open_; }
    void draw(Host &host);
    void cancel(Host &host); // right-click / Back / Escape

    [[nodiscard]] unsigned category() const noexcept { return category_; }
    [[nodiscard]] unsigned topic() const noexcept { return topic_; }
    [[nodiscard]] std::size_t page() const noexcept { return page_; }
    [[nodiscard]] std::size_t page_count() const noexcept { return pages_.size(); }

  private:
    void select(Artwork &artwork, unsigned category, unsigned topic);
    void scroll(int direction);

    bool open_ = false;
    int x_ = 20, y_ = 60;
    unsigned category_ = 0, topic_ = 0; // ref_00476018 / ref_0047601c (absolute topic index)
    std::array<unsigned, 8> first_{};   // ref_004761c4: first visible topic, kept across opens
    std::size_t page_ = 0;
    std::vector<std::vector<std::string>> pages_; // UTF-8 lines of the loaded topic
    int loaded_ = -1;                              // help.mkf entry in pages_
};

} // namespace rich4::native::screens
