// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 speech bubbles, message boxes, yes/no strip, news/fortune screens and god
// arrival captions (docs/ui/messages.md). Core events are translated into a queue of modal
// overlays; while one is showing the Application pauses the game, as the original blocks.
#pragma once

#include "../native_frame.hpp"
#include "rich4/game.hpp"
#include "rich4/world.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rich4::native::messages {

inline constexpr int skip_button = 900; // Full-screen tap-to-skip hit while an overlay is up.

struct Item {
    enum class Kind { bubble, box, news, fortune, god_caption };
    Kind kind = Kind::bubble;
    std::optional<std::size_t> focus; // Player slot the camera centres on (bubbles).
    std::uint8_t character = 0;       // Bubble portrait / fortune portrait.
    std::uint8_t mood = 0;            // Bubble portrait frame - 1.
    std::string text;                 // Bubble line, box text, news/fortune body, god caption.
    int pictogram = -1;               // 金貝貝: Data.mkf 519 frame instead of text.
    std::size_t voice = 0;            // Speaking.mkf entry (0 = silent).
    std::uint32_t minimum_ms = 1000;  // Hold: max(minimum, voice length) when wait_voice.
    bool wait_voice = false;
    int box_x = 220; // Message box anchor x (320 for the shifted variant).
    // News/fortune screen.
    std::size_t photo = 0; // Data.mkf raw 388x251 photo entry.
    std::string title;
    struct Row {
        std::uint8_t character;
        int portrait_frame;
        std::string text; // Empty: portrait only (prison/hospital news).
    };
    std::vector<Row> rows;
};

struct Audio {
    std::function<std::uint32_t(std::size_t)> speak; // Plays Speaking.mkf[n]; returns clip ms (0 if silent).
    std::function<void()> stop_voice;
};

// Translates a core event into its original overlay (nothing for plain notices).
[[nodiscard]] std::optional<Item> from_event(const GameEvent &event, const World &world);
// Yes/no prompts (§2.2): buy/upgrade land or facility. Others keep their own dialogs.
[[nodiscard]] bool is_yes_no(const GameDecision &decision);
[[nodiscard]] std::string yes_no_question(const GameDecision &decision, const World &world);
// Draws the question box plus the Data.mkf 440 strip; registers YES/NO hits by id.
void draw_yes_no(Frame &frame, std::string_view question, int yes_id, std::function<void()> yes, int no_id,
                 std::function<void()> no);
// Original text modes of _rich4_draw_text: 0 top-left, 4 centre, 5 left/v-centre, 7 bottom-centre.
void draw_text(Frame &frame, std::string_view text, int x, int y, int mode, const ui::TextStyle &style);

class Overlay {
  public:
    explicit Overlay(Audio audio) : audio_(std::move(audio)) {}
    void push(Item item);
    [[nodiscard]] bool active() const noexcept {
        return !queue_.empty();
    }
    [[nodiscard]] const Item *current() const noexcept {
        return queue_.empty() ? nullptr : &queue_.front();
    }
    [[nodiscard]] std::optional<std::size_t> focus() const noexcept {
        return queue_.empty() ? std::nullopt : queue_.front().focus;
    }
    void clear();
    void skip();
    // Headless replay: every item is shown for exactly one drawn frame, so it never blocks.
    void set_instant(bool instant) noexcept {
        instant_ = instant;
    }
    // Retires the current item once its hold time (or, in instant mode, its one frame) is over.
    // Call before drawing the board so a following decision is drawn in the same frame.
    void retire();
    // Draws the current item, starting its voice and timer on first sight, and registers the
    // tap-to-skip hit. Returns false when nothing is showing.
    bool draw(Frame &frame);

  private:
    void start(std::uint64_t now);
    void draw_item(Frame &frame, const Item &item);
    const graphics::Image &photo(Frame &frame, std::size_t entry);
    Audio audio_;
    std::deque<Item> queue_;
    bool started_ = false, instant_ = false;
    std::uint64_t until_ = 0;
    std::string last_bubble_;
    std::map<std::size_t, graphics::Image> photos_;
};

} // namespace rich4::native::messages
