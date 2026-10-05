// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 AI settings window (託管AI, _rich4_ui_ai_settings_entry, rich4_ui_ai_settings.asm;
// docs/ui/finance-menus.md §11): toolbar icon 2. Panel 77 at (102,62) lists the human players; each can
// be handed to the computer (託管) and given its AI profile (cards/tools, 個性, money ratios).
// The window only edits copies; 確定 writes them back through GameSession::set_ai_settings.
#pragma once

#include "draw.hpp"
#include "rich4/game.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>

namespace rich4 {
class World;
}

namespace rich4::native::screens {

namespace ids {
inline constexpr int ai_tab = 1600; // + player slot; tapping the selected tab toggles 託管
inline constexpr int ai_cards = 1610, ai_tools = 1611, ai_personality = 1612; // personality + 0..2
inline constexpr int ai_cash_down = 1620, ai_cash_up = 1621, ai_cash_bar = 1622, ai_stock_down = 1623,
                     ai_stock_up = 1624, ai_stock_bar = 1625;
inline constexpr int ai_ok = 1630, ai_cancel = 1631, ai_outside = 1632;
} // namespace ids

struct AiSettingsActions {
    std::function<void(std::size_t, const AiSettings &)> apply; // 確定: one call per changed player
    std::function<void()> close;
};

class AiSettingsScreen {
  public:
    // Loads every human player's settings and selects `current` when it is listed. Returns false
    // when no player can be listed (all-computer game).
    bool open(const GameSession &session, std::size_t current);
    void draw(Host &host, const World &world, const AiSettingsActions &actions);
    void cancel(Host &host, const AiSettingsActions &actions); // right-click / Back = 取消
    [[nodiscard]] std::optional<AiSettings> edited(std::size_t slot) const {
        return slot < edits_.size() ? edits_[slot] : std::nullopt;
    }

  private:
    std::array<std::optional<AiSettings>, 4> edits_{}, original_{};
    std::size_t selected_ = 0;
};

} // namespace rich4::native::screens
