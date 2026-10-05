// SPDX-License-Identifier: GPL-3.0-or-later
// Original facility-type picker (docs/ui/facility-build.md): fcn_00440aac + window proc
// fcn_0043fae4. The Data.mkf 517:5 message box with "請選擇設施類別" and the Data.mkf 517:4 strip of
// five 68x68 icons (公園, 旅館, 購物中心, 加油站, 研究所). Used when a human builds on an owned
// empty facility lot (landing, construction company, 天使卡, 福神, magic house, 機器工人) and by
// 改建卡. Touch: the first tap selects a slot (the original hover: yellow frame and the type name
// in the box), a second tap on it or 確定 commits; 取消 / Back cancel where the original allows
// right-click (改建卡 only).
#pragma once

#include "draw.hpp"
#include "rich4/game.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace rich4::native::facility {

inline constexpr int confirm_button = 207; // 確定 (same id as the map-target confirm)

// construct_facility decisions and the 改建卡 type choice (action_target "請選擇設施類別").
[[nodiscard]] bool handles(const GameDecision &decision);

class Picker {
  public:
    // `key` identifies the decision instance; a new key clears the selection. Option ids stay
    // 210 + option index (slot taps and 取消), so the replay harness keeps working.
    void draw(screens::Host &host, const GameDecision &decision, const std::string &key,
              const std::function<void(std::size_t)> &respond);
    // True when the decision has a cancel option (改建卡: the original's right-click).
    [[nodiscard]] static bool cancellable(const GameDecision &decision);
    // Android Back / Escape: answers the cancel option with Effect 4.
    void cancel(screens::Host &host, const GameDecision &decision, const std::function<void(std::size_t)> &respond);
    // Gallery: preselects a facility type slot (0..4).
    void select(std::optional<int> slot, std::string key) {
        key_ = std::move(key);
        selected_ = slot;
    }

  private:
    std::string key_;
    std::optional<int> selected_; // facility type slot 0..4
};

} // namespace rich4::native::facility
