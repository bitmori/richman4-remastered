// SPDX-License-Identifier: GPL-3.0-or-later
// The three original interactive mini-games (docs/ui/mini-games.md), ported tick by tick from
// rich4_small_games.asm. Portable rules only: no SDL, no assets. Each game draws its randomness
// from a private copy of the core's Watcom stream (the original shares one rand() with the rules)
// and counts the draws, so GameSession::submit_mini_game can advance the real stream by exactly
// that many calls. A tick is one original timer message (100 ms penguin/balloon, 50 ms 喜從天降).
// Every tick also records what the original drew (`scene()`), so the app only paints it.
#pragma once

#include "rich4/random.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace rich4::minigames {

enum class Game : std::uint8_t {
    penguin = 0, // 企鵝挖寶, node kind 6
    balloon = 1, // 七彩氣球, node kind 7
    catcher = 2  // 喜從天降, node kind 8
};
[[nodiscard]] constexpr Game game_for_node_kind(unsigned kind) noexcept {
    return kind == 6 ? Game::penguin : kind == 7 ? Game::balloon : Game::catcher;
}
[[nodiscard]] constexpr unsigned tick_ms(Game game) noexcept {
    return game == Game::catcher ? 50 : 100;
}

// rand() as the original calls it, with a draw counter.
class Stream {
  public:
    explicit Stream(std::uint32_t seed) noexcept : random_(seed) {}
    std::uint16_t next() noexcept {
        ++draws_;
        return random_.next();
    }
    [[nodiscard]] std::uint32_t draws() const noexcept {
        return draws_;
    }
    [[nodiscard]] std::uint32_t state() const noexcept {
        return random_.state();
    }

  private:
    Random random_;
    std::uint32_t draws_ = 0;
};

// Original timeline shared by the three games.
enum class Stage : std::uint8_t {
    intro,  // timer counting down to message 0x405 (penguin: treasures shown)
    ready,  // the READY/GO FLC (Panel 78) plays; the app calls begin_play() when it ends
    play,   // the game runs
    ending, // the big score is shown and held
    done    // the window closed: submit score() and draws()
};

// One sprite the original drew this tick, in drawing order. archive 'p' Panel / 'd' Data.
struct Sprite {
    char archive = 'p';
    std::uint16_t entry = 0;
    std::uint16_t frame = 0;
    int x = 0, y = 0;            // anchor (graph_st x/y semantics)
    std::uint32_t scale = 0;     // 16.16 scaled draw (fcn_004568c2); 0 = normal
};

// Sound requests, drained by the app (Effect.mkf indices).
struct Cue {
    enum class Op : std::uint8_t { play, loop, stop } op;
    std::size_t effect;
};

// Frame box of a sprite: size and anchor, as stored in the SPR frame table.
struct Box {
    int width = 0, height = 0, origin_x = 0, origin_y = 0;
};

class Base {
  public:
    explicit Base(std::uint32_t seed) noexcept : random_(seed) {}
    [[nodiscard]] Stage stage() const noexcept {
        return stage_;
    }
    [[nodiscard]] std::uint32_t draws() const noexcept {
        return random_.draws();
    }
    [[nodiscard]] std::uint32_t random_state() const noexcept {
        return random_.state();
    }
    [[nodiscard]] const std::vector<Sprite> &scene() const noexcept {
        return scene_;
    }
    [[nodiscard]] std::vector<Cue> drain_cues() {
        return std::exchange(cues_, {});
    }
    // The READY/GO movie ended (message 0x405 returned).
    void begin_play() noexcept {
        if (stage_ == Stage::ready)
            stage_ = Stage::play;
    }
    // Remaining play ticks (bd2c), the number the HUD clock shows.
    [[nodiscard]] int ticks_left() const noexcept {
        return timer_;
    }

  protected:
    void play(std::size_t effect) {
        cues_.push_back({Cue::Op::play, effect});
    }
    void loop(std::size_t effect) {
        cues_.push_back({Cue::Op::loop, effect});
    }
    void stop(std::size_t effect) {
        cues_.push_back({Cue::Op::stop, effect});
    }

    Stream random_;
    Stage stage_ = Stage::intro;
    int timer_ = 0, countdown_ = 0, hold_ = 0;
    std::vector<Sprite> scene_;
    std::vector<Cue> cues_;
};

// ---------------------------------------------------------------------------------------------
// 企鵝挖寶 (fcn_00414858 / fcn_004124c8). Panel 80..90.
class Penguin : public Base {
  public:
    struct Cell {
        std::int16_t x = 0, y = 0; // screen anchor; x == 0 marks a hole in the 9x9 grid
        std::uint16_t flags = 0;   // low nibble treasure kind 1..5, 0x10 = behind the igloo
    };
    static constexpr int cells = 9;

    explicit Penguin(std::uint32_t seed); // places the 28 treasures (fcn_00412014)
    // Message 0x405 ends with fcn_0041461b(0): the treasure markers disappear.
    void begin_play() noexcept {
        if (stage_ == Stage::ready)
            scene_.clear();
        Base::begin_play();
    }
    void tick();
    // A click on grid cell (col,row) (Panel 81 hit map). Returns true when a walk starts.
    bool tap(int col, int row);
    // A click anywhere: during the final hold it closes the game sooner (bd2c = 1).
    void tap_anywhere() noexcept;

    [[nodiscard]] const Cell &cell(int col, int row) const {
        return grid_.at(std::size_t(row)).at(std::size_t(col));
    }
    [[nodiscard]] static bool valid(int col, int row) noexcept;
    [[nodiscard]] std::int32_t score() const noexcept; // diamond*20 + pink*12 + blue*8 + coin*5
    // Treasures dug per kind 2..5 (coin, pink, blue, diamond); index = kind.
    [[nodiscard]] const std::array<std::int32_t, 6> &found() const noexcept {
        return found_;
    }
    // Holes stamped into the background (cell anchors), in digging order.
    [[nodiscard]] const std::vector<std::pair<int, int>> &holes() const noexcept {
        return holes_;
    }
    // The big score has been stamped into the background (state 1/6 frame 4, 4/5 at once).
    [[nodiscard]] bool score_shown() const noexcept {
        return score_shown_;
    }
    [[nodiscard]] std::pair<int, int> position() const noexcept { // current cell (col,row)
        return {int(cur_x_ >> 16), int(cur_y_ >> 16)};
    }
    [[nodiscard]] bool idle() const noexcept {
        return state_ == 0 && done_ == 0 && stage_ == Stage::play;
    }

  private:
    bool aim(int col, int row);
    void step();
    void animate();
    void draw(std::uint16_t entry, std::uint16_t frame, int x, int y) {
        scene_.push_back({'p', entry, frame, x, y, 0});
    }

    std::array<std::array<Cell, 9>, 9> grid_{};
    std::uint32_t cur_x_ = 0x20000, cur_y_ = 0x60000;  // bd04 / bd08 (16.16 cells)
    std::uint32_t next_x_ = 0, next_y_ = 0;            // bd10 / bd0c
    std::uint32_t target_x_ = 0, target_y_ = 0;        // bcdc / bce0
    std::int32_t step_x_ = 0, step_y_ = 0;             // bcfc / bd00
    bool blocked_ = false;                             // bd5b
    std::uint32_t anim_ = 0;                           // bcc4
    int state_ = 0;                                    // bccc
    int done_ = 0;                                     // bd58
    int treasure_ = 0, treasure_x_ = 0, treasure_y_ = 0; // bd6c / bd70 / bd74
    std::array<std::int32_t, 6> found_{};               // ref_0048bbac + 4*kind
    std::vector<std::pair<int, int>> holes_;
    bool score_shown_ = false;
};

// ---------------------------------------------------------------------------------------------
// 七彩氣球 (fcn_00414bbc / fcn_00412f6f). Panel 91.
class Balloon : public Base {
  public:
    struct Slot {
        std::int16_t x = 0, y = 0; // x == 0: empty
        std::uint16_t kind = 0;    // 0..8 = "1".."9", 9 ×2, 10 ÷2, 11 ?; high nibble = bursting
    };
    explicit Balloon(std::uint32_t seed);
    void tick();
    // A click at (x,y). Returns the number of balloons popped.
    int tap(int x, int y);
    [[nodiscard]] std::int32_t score() const noexcept {
        return score_;
    }
    [[nodiscard]] const std::array<Slot, 16> &slots() const noexcept {
        return slots_;
    }
    [[nodiscard]] int freeze() const noexcept {
        return freeze_;
    }
    [[nodiscard]] int speed_modifier() const noexcept {
        return modifier_;
    }

  private:
    void step();
    std::array<Slot, 16> slots_{};
    std::int32_t score_ = 0; // bcec
    int done_ = 0;           // bd58
    int freeze_ = 0;         // bd59
    int modifier_ = 0;       // bcc8: -1 double speed, 1 half speed
};

// ---------------------------------------------------------------------------------------------
// 喜從天降 (fcn_00414fcd / fcn_00413248). Panel 92..99, 100+character, Data 526.
class Catcher : public Base {
  public:
    struct Slot {
        std::int16_t x = 0, y = 0; // x == 0: empty
        std::uint16_t bits = 0;    // kind | frame << 4 | drift << 8 (bbc8)
        std::int16_t vy = 0;       // bbca
    };
    // catcher_frames: Panel 100+character frame boxes (25 frames: 5 poses, 10 left, 10 right).
    // explosion_ticks: length of the Data 526 FLC in 50 ms ticks (8 frames x 114 ms).
    Catcher(std::uint32_t seed, std::uint8_t character, std::vector<Box> catcher_frames, int explosion_ticks = 19);
    void tick();
    // Pointer x the catcher walks toward (the original follows the mouse cursor x).
    void set_target(int x) noexcept {
        target_ = x;
    }
    [[nodiscard]] std::int32_t score() const noexcept; // chest*10 + sack*5 + gold*3 + coin
    [[nodiscard]] const std::array<std::int32_t, 5> &caught() const noexcept {
        return caught_;
    }
    [[nodiscard]] const std::array<Slot, 16> &slots() const noexcept {
        return slots_;
    }
    [[nodiscard]] int catcher_x() const noexcept {
        return catcher_x_;
    }
    // Data 526 explosion: anchor and ticks since it started, while it plays.
    [[nodiscard]] std::optional<std::pair<int, int>> explosion() const noexcept {
        if (!exploding_)
            return std::nullopt;
        return std::pair{explosion_x_, explosion_age_};
    }
    [[nodiscard]] int god_x() const noexcept {
        return god_x_;
    }
    // Original drift of a thrown object at x (fcn_004123d7), exposed for tests.
    [[nodiscard]] static int drift(int x);

  private:
    void step();
    void spawn(int x, bool bomb);
    [[nodiscard]] int catcher_frame() const noexcept;

    std::uint16_t catcher_entry_;
    std::vector<Box> frames_;
    int explosion_ticks_;
    std::array<Slot, 16> slots_{};
    std::array<std::int32_t, 5> caught_{}; // bbb4.. per kind 0..3 (4 = bombs, unused)
    int done_ = 0;                          // bd58
    bool exploding_ = false;                // bd5a
    int explosion_x_ = 0, explosion_age_ = 0;
    int goblin_ = -1, goblin_x_ = 0;        // bd42 / bd4a
    int god_state_ = 3, god_step_ = 4;      // bd44 / bd46
    int god_x_ = 110, throw_at_ = 0;        // bd4c / bd40
    int catcher_x_ = 320;                   // bd4e
    int direction_ = 0, walk_ = 0, pose_ = 0; // bd48 / bd50 / bd56
    int per_direction_ = 10;                // bd52
    int bombs_ = 0;                         // bd54
    int target_ = 320;
};

} // namespace rich4::minigames
