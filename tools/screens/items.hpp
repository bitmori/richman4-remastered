// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 item screens (docs/ui/cards-tools-shops.md): the use-card and use-tool
// grids, the card-art popup, on-map target cursors, the point shop, the magic house and the
// hospital/prison bail screens. Adapted for touch: no hover or right-click is needed.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/game.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rich4::native::items {

// Button ids used by these screens (the replay harness pushes them by id).
inline constexpr int card_tile_id = 300;   // + card id, also + tool id on the tool grid
inline constexpr int dismount_id = 340;    // 下車 tile
inline constexpr int back_id = 60;         // visible 返回 button
inline constexpr int outside_id = 61;      // tap outside the grid closes it
inline constexpr int popup_skip_id = 360;  // tap to dismiss the card-art popup
inline constexpr int option_id = 210;      // + decision option index (shop, rescue, magic)
inline constexpr int shop_switch_id = 280; // card shop <-> tool shop
inline constexpr int yes_id = 290, no_id = 291;
inline constexpr int magic_mask_id = 299;

// Services the Application provides; kept as callbacks so this file stays independent of it.
struct Host {
    std::function<void()> close;                                  // back to the board
    std::function<void(bool, std::string_view)> notify;           // success flag, failure toast
    std::function<bool(std::size_t)> respond;                     // answer the pending decision
};

class Screens {
  public:
    // Use-card (cards=true) or use-tool grid over the board viewport.
    void draw_inventory(Frame &, GameSession &, bool cards, const Host &);
    // Card-art popup (Data 570+id). While active it owns input and pauses game time.
    [[nodiscard]] bool popup_active() const noexcept { return !popups_.empty(); }
    // Actions decided while drawing (release on a tile, timeouts) run at the start of the next frame,
    // so the frame on screen always matches its registered buttons.
    [[nodiscard]] bool has_deferred() const noexcept { return !deferred_.empty(); }
    void run_deferred();
    void draw_popup(Frame &);
    // Queue popups for computer and passive card uses reported by the core.
    void observe(const GameEvent &, const World &);
    // Animated original cursor sprites on the map targets of a decision.
    void draw_targets(Frame &, const GameDecision &, const board::Camera &, std::optional<std::size_t> selected,
                      const std::function<void(std::size_t)> &tap);
    // Shop, magic-house and rescue decisions. Returns false for other decision kinds.
    bool draw_decision(Frame &, GameSession &, const Host &);
    // No decision is pending: per-visit screen state is discarded.
    void end_visits();
    // A new game or loaded save: drop everything that refers to the old session.
    void reset();

    // Replay support: the option the witch chose, and showing the shop shelf of an option.
    [[nodiscard]] std::size_t magic_choice(const GameSession &) const;
    bool shop_show_option(std::int32_t value); // true when the shelf had to switch

    std::function<void(std::size_t)> sound; // Effect.mkf entry; may be empty
    // Replay runs use a synthetic clock so timed balloons and popups advance per frame.
    std::optional<std::uint64_t> clock;
    // Name of the item screen drawn this frame (for headless screenshots); cleared by the caller.
    std::string shot;

  private:
    struct Popup {
        std::uint8_t card;
        std::uint64_t until = 0;
        std::function<void()> then;
    };
    struct Press {
        std::string owner;
        graphics::Rect rect;
        std::function<void()> action;
        int item = 0; // card or tool id shown in the description strip
    };
    struct Shop {
        bool active = false;
        int mode = 0; // 0 card shop, 1 tool shop
        std::vector<std::uint8_t> cards, tools;
        std::vector<bool> cards_bought, tools_bought;
        std::uint64_t opened = 0, balloon_until = 0;
        std::string balloon;
        bool leaving = false;
        std::uint64_t leave_at = 0;
    };
    struct Magic {
        bool active = false;
        std::uint64_t opened = 0;
        int category = -1;  // witch-chosen group (index into the native category list)
        int selected = -1;  // effect tapped once
        std::uint64_t casting = 0;
        bool scheduled = false;
    };
    struct Rescue {
        bool active = false;
        int selected = -1;
        std::uint64_t poor_until = 0;
        std::uint64_t opened = 0;
    };

    [[nodiscard]] std::uint64_t now(const Frame &) const;
    void resolve_press(Frame &, std::string_view owner);
    void press(Frame &, std::string owner, graphics::Rect rect, std::function<void()> action, int item = 0);
    const std::string &description(Artwork &, bool card, unsigned id);
    const graphics::Image &card_art(Artwork &, std::uint8_t card);
    void draw_shop(Frame &, GameSession &, const GameDecision &, const Host &);
    void draw_magic(Frame &, GameSession &, const GameDecision &, const Host &);
    void draw_rescue(Frame &, GameSession &, const GameDecision &, const Host &);

    std::deque<Popup> popups_;
    std::vector<std::function<void()>> deferred_;
    std::optional<Press> press_;
    std::uint8_t last_card_ = 0;
    unsigned last_tool_ = 0;
    std::map<std::pair<bool, unsigned>, std::string> descriptions_;
    std::map<std::uint8_t, graphics::Image> card_art_;
    std::vector<std::uint8_t> magic_mask_;
    Shop shop_;
    Magic magic_;
    Rescue rescue_;
};

} // namespace rich4::native::items
