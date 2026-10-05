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
#include <initializer_list>
#include <map>
#include <memory>
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
inline constexpr int magic_cast_skip_id = 1660;

// Services the Application provides; kept as callbacks so this file stays independent of it.
struct Host {
    std::function<void()> close;                                  // back to the board
    std::function<void(bool, std::string_view)> notify;           // success flag, failure toast
    std::function<bool(std::size_t)> respond;                     // answer the pending decision
};

namespace detail {
struct CastMovie; // Panel 20 hexagram FLC (magic-house cast), decoded progressively
}

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
    // Computer magic-house cast (magic_house.asm:1790-1813): the witch with "%s\n\n%s" (group, effect)
    // for 1500 ms, then the Panel 20 movie. Presentation only; a tap (magic_cast_skip_id) ends it.
    void start_magic_cast(int group, int effect);
    [[nodiscard]] bool magic_cast_active() const noexcept { return cast_.has_value(); }
    void draw_magic_cast(Frame &);
    // Queue popups for computer and passive card uses reported by the core.
    void observe(const GameEvent &, const World &);
    // Animated original cursor sprites on the map targets of a decision.
    void draw_targets(Frame &, const GameDecision &, const board::Camera &, std::optional<std::size_t> selected,
                      const std::function<void(std::size_t)> &tap);
    // Shop, magic-house and rescue decisions, the remote-dice picker (Panel 72) and the 嫁禍卡
    // victim chooser (Data 518). Returns false for other decision kinds.
    bool draw_decision(Frame &, GameSession &, const Host &);
    // No decision is pending: per-visit screen state is discarded.
    void end_visits();
    // A new game or loaded save: drop everything that refers to the old session.
    void reset();

    // Replay support: show the card or tool shelf that holds a shop option.
    bool shop_show_option(std::int32_t value); // true when the shelf had to switch
    // The shop option tapped once and awaiting its confirming second tap.
    [[nodiscard]] std::optional<std::int32_t> shop_pending_value() const;

    // Developer gallery: draws each screen with synthetic state; `background` paints the board and
    // `save` stores the canvas under a name.
    void gallery(Frame &, const World &, const std::function<void()> &background,
                 const std::function<void(const std::string &)> &save);

    std::function<void(std::size_t)> sound; // Effect.mkf entry; may be empty
    // AUDIO: Speaking.mkf clip (shop clerk 0-10, witch 37-57, nurse 127-130, thanks 123-126);
    // the flag queues it after the current voice. May be empty.
    std::function<void(std::size_t, bool)> speech;
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
        int pick_kind = -1; // 0 card row, 1 tool row, 2 own card tile, 3 own tool tile
        std::size_t pick_index = 0;
        std::int32_t pick_value = 0;
    };
    struct Magic {
        bool active = false;
        std::uint64_t opened = 0;
        int category = -1;  // witch-chosen group (index into the native category list)
        int selected = -1;  // effect tapped once
        std::uint64_t casting = 0;
        bool scheduled = false;
        std::shared_ptr<detail::CastMovie> movie; // Panel 20 after the witch's cast (fcn_0043380a)
    };
    struct Rescue {
        bool active = false;
        int selected = -1;
        std::uint64_t poor_until = 0;
        std::uint64_t opened = 0;
        int thanks = -1;                 // rescued NPC slot 4..7 thanking the player (Panel 64)
        std::uint64_t thanks_until = 0;
        bool thanks_scheduled = false;
    };

    [[nodiscard]] std::uint64_t now(const Frame &) const;
    void resolve_press(Frame &, std::string_view owner);
    void press(Frame &, std::string owner, graphics::Rect rect, std::function<void()> action, int item = 0);
    const std::string &description(Artwork &, bool card, unsigned id);
    const graphics::Image &card_art(Artwork &, std::uint8_t card);
    void draw_shop(Frame &, GameSession &, const GameDecision &, const Host &);
    void draw_magic(Frame &, GameSession &, const GameDecision &, const Host &);
    void draw_rescue(Frame &, GameSession &, const GameDecision &, const Host &);
    // AUDIO: speaks `clips` (in turn) once each time `key` changes; an empty key re-arms.
    void voice_once(const std::string &key, std::initializer_list<std::size_t> clips);
    void rescue_voice(bool hospital, int slot);
    std::string voiced_;
    void draw_remote_dice(Frame &, const GameDecision &, const Host &);
    void draw_steal(Frame &, GameSession &, const GameDecision &, const Host &);
    std::optional<std::size_t> steal_pick_; // 搶奪卡: option index selected by the first tap
    void draw_frame_up(Frame &, GameSession &, const GameDecision &, const Host &);
    bool draw_cast_movie(Frame &, std::shared_ptr<detail::CastMovie> &, std::uint64_t elapsed);

    std::deque<Popup> popups_;
    std::vector<std::function<void()>> deferred_;
    std::optional<Press> press_;
    std::uint8_t last_card_ = 0;
    unsigned last_tool_ = 0;
    std::map<std::pair<bool, unsigned>, std::string> descriptions_;
    std::map<std::uint8_t, graphics::Image> card_art_;
    std::vector<std::uint8_t> magic_mask_;
    struct MagicCast {
        int group = 0, effect = 0;
        std::uint64_t started = 0; // 0 = stamped on the next frame
        std::shared_ptr<detail::CastMovie> movie;
    };
    std::optional<MagicCast> cast_;
    Shop shop_;
    Magic magic_;
    Rescue rescue_;
};

} // namespace rich4::native::items
