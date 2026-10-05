// SPDX-License-Identifier: GPL-3.0-or-later
// Original 大富翁4 finance screens (docs/ui/finance-menus.md §3, §4, §7, §8, §9):
// the shared number pad (Panel 21/22), ATM (Panel 24), loan desk and director's office
// (Panel 23), primary-share subscription, the stock market (Panel 75), player trading
// (Panel 73), the lottery bar (Panel 12) and the auction room (Panel 26).
//
// The screens only draw, register hit areas and call back into GameSession; every rule
// stays in the core. Button ids are stable because the replay harness clicks by id.
#pragma once

#include "draw.hpp"
#include "rich4/game.hpp"
#include "rich4/trading.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rich4 {
class World;
}

namespace rich4::native::screens {

// Button ids (the replay harness depends on them).
namespace ids {
inline constexpr int pad_body = 900, pad_cancel = 901, pad_max = 902, pad_enter = 903, pad_clear = 904,
                     pad_zero = 905, pad_back = 906, pad_gauge = 916;
int pad_digit(int digit); // 905 for 0, 907..915 for 7 8 9 4 5 6 1 2 3
inline constexpr int atm_withdraw = 920, atm_deposit = 921, atm_exit = 922, atm_gauge = 923, atm_max = 936,
                     atm_enter = 937;
int atm_digit(int digit);
inline constexpr int atm_clear = 933, atm_back = 935;
inline constexpr int desk_exit = 940, desk_loan = 941, desk_repay = 942, desk_window = 943, director_finance = 944,
                     director_return = 945, director_exit = 946, bank_skip = 947;
inline constexpr int stock_page = 434, stock_buy = 430, stock_sell = 431, stock_info = 435, stock_exit = 60,
                     stock_info_close = 436, stock_row = 400;
} // namespace ids

// Panel 21/22 shared numeric pad (fcn_00453544). Modal: while active the caller draws
// nothing clickable underneath.
class NumberPad {
  public:
    using Done = std::function<void(std::int32_t)>;
    void open(std::int32_t maximum, Done done, int x = 256, int y = 144);
    void close() {
        active_ = false;
    }
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }
    [[nodiscard]] std::int32_t value() const;
    [[nodiscard]] std::int32_t maximum() const noexcept {
        return maximum_;
    }
    [[nodiscard]] const std::string &digits() const noexcept {
        return text_;
    }
    void draw(Host &host);
    void cancel(Host &host); // right-click / Back: returns 0, as the original
    // Keyboard: '0'..'9', 'M' (MAX), 'C' (clear), '\b', '\r' (Enter), 'H' (gauge middle).
    void key(Host &host, char key);
    // Replay helper: the id of the next key that moves the display towards target (Enter when done).
    [[nodiscard]] int next_key_towards(std::int32_t target) const;

  private:
    void press(int key);
    void set_from_gauge(int x);
    bool active_ = false, dragging_ = false;
    int x_ = 256, y_ = 144;
    std::int32_t maximum_ = 0;
    std::string text_ = "0";
    Done done_;
};

// What the bank screens need from the application: answer the pending decision with the
// option whose value is `value` (see BankScreen) and an amount.
struct BankActions {
    std::function<bool(std::int32_t value, std::int32_t amount)> respond;
};

// DecisionKind::bank: ATM (Panel 24) first, then on a stop the loan desk (Panel 23) with the
// director's office for the owner of this bank. Values: 1 存款, 2 提款, 3 贷款, 4 还贷,
// 5 融资, 6 归还融资, 0 离开 (GameSession::respond).
class BankScreen {
  public:
    void reset() {
        *this = {};
    }
    void draw(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
              const BankActions &actions);
    [[nodiscard]] bool at_atm() const noexcept {
        return stage_ == Stage::atm;
    }
    [[nodiscard]] bool at_desk() const noexcept {
        return stage_ == Stage::desk;
    }
    [[nodiscard]] bool at_director() const noexcept {
        return stage_ == Stage::director;
    }
    [[nodiscard]] int atm_mode() const noexcept {
        return mode_;
    }
    [[nodiscard]] const std::string &atm_digits() const noexcept {
        return atm_text_;
    }
    void cancel(Host &host, const GameDecision &decision, const BankActions &actions);

  private:
    enum class Stage { atm, desk, director };
    void draw_atm(Host &host, const World &world, const GameDecision &decision, const BankActions &actions);
    void draw_desk(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                   const BankActions &actions);
    void draw_director(Host &host, const World &world, const GameDecision &decision, NumberPad &pad,
                       const BankActions &actions);
    void draw_side_panel(Host &host, const World &world, std::size_t slot);
    void atm_key(Host &host, int index, const World &world, const GameDecision &decision,
                 const BankActions &actions);
    void leave_atm(Host &host, const GameDecision &decision, const BankActions &actions);
    void say(Host &host, std::vector<std::string> lines);
    std::int32_t atm_maximum(const World &world, std::size_t slot) const;

    Stage stage_ = Stage::atm;
    bool started_ = false, atm_dragging_ = false, side_panel_ = false;
    int mode_ = 0; // 0 提款, 1 存款
    std::string atm_text_ = "0";
    std::vector<std::string> lines_;
    std::uint64_t line_started_ = 0, side_started_ = 0, popup_until_ = 0;
    std::string popup_;
};

// DecisionKind::primary_stock: Panel 73:5 prompt box above the number pad.
void draw_primary_stock(Host &host, const GameDecision &decision, NumberPad &pad, const BankActions &actions);

// Panel 75 stock market (rich4_ui_stock.asm).
struct StockActions {
    std::function<bool(std::size_t stock, std::int32_t quantity)> buy, sell;
    std::function<void()> close;
};
class StockMarket {
  public:
    void reset() {
        *this = {};
    }
    void draw(Host &host, const World &world, std::size_t player, NumberPad &pad, const StockActions &actions);
    void cancel(Host &host, const StockActions &actions); // EXIT / right-click / Back
    std::optional<std::size_t> selected;
    int page = 0;
    bool info_open = false;

  private:
    void draw_prices(Host &host, const World &world, std::size_t player);
    void draw_holdings(Host &host, const World &world);
    void draw_info(Host &host, const World &world, std::size_t stock);
    std::uint64_t popup_until_ = 0;
    std::string popup_;
};

bool market_closed(const World &world);

} // namespace rich4::native::screens
