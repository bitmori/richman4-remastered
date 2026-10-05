// SPDX-License-Identifier: GPL-3.0-or-later
// Tick-for-tick port of rich4_small_games.asm ("SG:n" = line n); see docs/ui/mini-games.md.
#include "rich4/minigames.hpp"

#include "rich4/legacy_float.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace rich4::minigames {
namespace {

// ref_00474d7c: 9x9 penguin grid {x, y, flags} per cell (row-major, [row][col]).
constexpr std::array<std::array<Penguin::Cell, 9>, 9> penguin_grid{{
    {{{0, 0, 0x0}, {0, 0, 0x0}, {0, 0, 0x0}, {80, 153, 0x0}, {128, 129, 0x0}, {176, 105, 0x0}, {224, 81, 0x0}, {0, 0, 0x0}, {0, 0, 0x0}}},
    {{{0, 0, 0x0}, {32, 225, 0x0}, {80, 201, 0x0}, {128, 177, 0x0}, {176, 153, 0x0}, {224, 129, 0x0}, {272, 105, 0x0}, {320, 81, 0x0}, {0, 0, 0x0}}},
    {{{0, 0, 0x0}, {80, 249, 0x0}, {128, 225, 0x0}, {176, 201, 0x0}, {224, 177, 0x0}, {272, 153, 0x0}, {320, 129, 0x0}, {368, 105, 0x0}, {416, 81, 0x0}}},
    {{{80, 297, 0x0}, {128, 273, 0x0}, {176, 249, 0x0}, {224, 225, 0x10}, {272, 201, 0x10}, {320, 177, 0x10}, {368, 153, 0x0}, {416, 129, 0x0}, {464, 105, 0x0}}},
    {{{128, 321, 0x0}, {176, 297, 0x0}, {224, 273, 0x0}, {272, 249, 0x0}, {0, 0, 0x0}, {368, 201, 0x10}, {416, 177, 0x0}, {464, 153, 0x0}, {512, 129, 0x0}}},
    {{{176, 345, 0x0}, {224, 321, 0x0}, {272, 297, 0x0}, {320, 273, 0x0}, {368, 249, 0x0}, {416, 225, 0x10}, {464, 201, 0x0}, {512, 177, 0x0}, {560, 153, 0x0}}},
    {{{224, 369, 0x0}, {272, 345, 0x0}, {320, 321, 0x0}, {368, 297, 0x0}, {416, 273, 0x0}, {464, 249, 0x0}, {512, 225, 0x0}, {560, 201, 0x0}, {0, 0, 0x0}}},
    {{{0, 0, 0x0}, {320, 369, 0x0}, {368, 345, 0x0}, {416, 321, 0x0}, {464, 297, 0x0}, {512, 273, 0x0}, {560, 249, 0x0}, {608, 225, 0x0}, {0, 0, 0x0}}},
    {{{0, 0, 0x0}, {0, 0, 0x0}, {416, 369, 0x0}, {464, 345, 0x0}, {512, 321, 0x0}, {560, 297, 0x0}, {0, 0, 0x0}, {0, 0, 0x0}, {0, 0, 0x0}}},
}};

// Effect.mkf indices of the preset tables 0x475057 (penguin), 0x47509f (balloon), 0x4750bf (喜從天降).
constexpr std::size_t penguin_walk = 11, penguin_dig = 12, penguin_happy = 13, penguin_sad = 14,
                      penguin_bomb = 15;
constexpr std::array<std::size_t, 6> penguin_treasure_sound{0, 0, 16, 17, 17, 18}; // ref_00475051
constexpr std::size_t balloon_rise = 19, balloon_miss = 20, balloon_pop = 21;
constexpr std::size_t goblin_throw = 22, bomb_fuse = 24, bomb_blast = 15;

// Panel 80 frame boxes needed for sprite placement are the asset's; the rules only use anchors.
constexpr std::uint16_t penguin_field = 80, penguin_walk_sprite = 82, penguin_dig_sprite = 83,
                        penguin_happy_sprite = 84, penguin_sad_sprite = 85;
constexpr int happy_frames = 8, sad_frames = 6; // Panel 84 / 85 frame counts ([sprite+4])

int sar16(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(value) >> 16;
}
int cell_of(std::uint32_t value) noexcept {
    return int((value >> 16) & 0xffffU);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// 企鵝挖寶

bool Penguin::valid(int col, int row) noexcept {
    return col >= 0 && row >= 0 && col < cells && row < cells &&
           penguin_grid[std::size_t(row)][std::size_t(col)].x != 0;
}

Penguin::Penguin(std::uint32_t seed) : Base(seed) {
    grid_ = penguin_grid;
    // fcn_00412014: {3, 12, 3, 9, 1} treasures of kinds 1..5 on the 64 free cells.
    constexpr std::array<int, 5> counts{3, 12, 3, 9, 1};
    int free = 64;
    for (std::size_t kind = 0; kind < counts.size(); ++kind)
        for (int n = 0; n < counts[kind]; ++n) {
            const int pick = (int(random_.next()) * free) >> 15;
            int seen = 0;
            for (int row = 0; row < cells; ++row)
                for (int col = 0; col < cells; ++col) {
                    auto &cell = grid_[std::size_t(row)][std::size_t(col)];
                    if (!cell.x || (cell.flags & 0xfU))
                        continue;
                    if (seen++ == pick) {
                        cell.flags = static_cast<std::uint16_t>((cell.flags & 0xf0U) | (kind + 1));
                        --free;
                        row = col = cells; // leave both loops
                    }
                }
        }
    timer_ = 150;    // bd2c (0x96)
    countdown_ = 10; // bd7c: treasures stay visible, then message 0x405
    // fcn_0041461b(1): background + the treasure markers (Panel 80 frame kind+3).
    for (int row = 0; row < cells; ++row)
        for (int col = 0; col < cells; ++col)
            if (const auto &cell = grid_[std::size_t(row)][std::size_t(col)]; cell.flags & 0xfU)
                draw(penguin_field, static_cast<std::uint16_t>((cell.flags & 0xfU) + 3), cell.x, cell.y);
}

std::int32_t Penguin::score() const noexcept {
    return found_[5] * 20 + found_[3] * 12 + found_[4] * 8 + found_[2] * 5; // fcn_00413a4a
}

void Penguin::tap_anywhere() noexcept {
    if (done_ == 2)
        timer_ = 1; // SG:3622 (bd2c doubles as the closing hold)
}

bool Penguin::tap(int col, int row) {
    if (done_ == 2) {
        timer_ = 1;
        return false;
    }
    if (state_ != 0 || done_ != 0 || stage_ != Stage::play || !aim(col, row))
        return false;
    step();
    state_ = 2;
    loop(penguin_walk);
    return true;
}

// fcn_0041211c: aim a DDA walk at (col,row); the major axis advances one cell per step.
bool Penguin::aim(int col, int row) {
    if (!valid(col, row))
        return false;
    if (col == cell_of(cur_x_) && row == cell_of(cur_y_))
        return false;
    cur_x_ = (cur_x_ & 0xffff0000U) + 0x8000U;
    cur_y_ = (cur_y_ & 0xffff0000U) + 0x8000U;
    target_x_ = std::uint32_t(col) << 16;
    target_y_ = std::uint32_t(row) << 16;
    const int dx = col - cell_of(cur_x_), dy = row - cell_of(cur_y_);
    const int major = std::abs(dy) >= std::abs(dx) ? std::abs(dy) : std::abs(dx);
    step_x_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(dx) << 16) / major;
    step_y_ = static_cast<std::int32_t>(static_cast<std::uint32_t>(dy) << 16) / major;
    return true;
}

// fcn_00412287: next cell of the walk; an invalid cell makes it slide around (and re-aim later).
void Penguin::step() {
    const std::uint32_t nx = cur_x_ + static_cast<std::uint32_t>(step_x_);
    const std::uint32_t ny = cur_y_ + static_cast<std::uint32_t>(step_y_);
    if (valid(cell_of(nx), cell_of(ny))) {
        next_x_ = nx;
        next_y_ = ny;
        return;
    }
    const auto sign = [](std::int32_t v) { return v / std::abs(v); };
    if (std::abs(step_x_) == 0x10000) {
        next_x_ = nx;
        if (cell_of(next_y_) == sar16(ny)) {
            if (!step_y_)
                step_y_ = 1;
            next_y_ += static_cast<std::uint32_t>(sign(step_y_)) << 16;
        }
    } else {
        next_y_ = ny;
        if (cell_of(next_x_) == sar16(nx)) {
            if (!step_x_)
                step_x_ = 1;
            next_x_ += static_cast<std::uint32_t>(sign(step_x_)) << 16;
        }
    }
    blocked_ = true;
}

// fcn_00414858 WM_TIMER (SG:3487).
void Penguin::tick() {
    if (stage_ == Stage::done)
        return;
    if (done_ == 2) {
        if (--timer_ == 0)
            stage_ = Stage::done;
        return;
    }
    if (countdown_) {
        if (--countdown_ == 0)
            stage_ = Stage::ready; // message 0x405: READY/GO over the shown treasures
        return;
    }
    if (stage_ != Stage::play)
        return; // the FLC is modal in the original
    if (timer_ > 0 && --timer_ == 0) {
        stop(penguin_walk);
        anim_ &= 0xf00U;
        const auto points = score();
        if (points < 40) {
            state_ = 5;
            loop(penguin_sad);
        } else if (points > 55) {
            state_ = 4;
            loop(penguin_happy);
        } else
            state_ = 6;
    }
    animate();
    if (done_ == 1) {
        done_ = 2;
        timer_ = 20; // 2 s hold with the big score (fcn_00414789)
        stage_ = Stage::ending;
        score_shown_ = true;
    }
}

// fcn_004124c8: one animation frame of the penguin state machine.
void Penguin::animate() {
    scene_.clear();
    const int col = cell_of(cur_x_), row = cell_of(cur_y_);
    const int next_col = cell_of(next_x_), next_row = cell_of(next_y_);
    const auto &here = grid_.at(std::size_t(row)).at(std::size_t(col));
    int x = here.x, y = here.y;
    const auto sub = [this] { return anim_ & 3U; };
    switch (state_) {
    case 0: // idle
        draw(penguin_field, 1, x, y);
        break;
    case 1: // bomb: blackened from frame 3, the game ends at frame 15
        draw(penguin_field, (anim_ & 0xfU) < 3 ? 1 : 2, x, y);
        if ((++anim_ & 0xfU) == 0xf)
            done_ = 1;
        break;
    case 2: { // walking, 4 ticks per cell
        unsigned direction;
        if (sub() == 0) {
            const int dx = next_col - col, dy = next_row - row;
            if (dx > 0)
                direction = unsigned(4 - (dy + 1));
            else if (dx == 0)
                direction = dy > 0 ? 1 : 5;
            else
                direction = unsigned(dy + 7) & 7U;
            anim_ = (anim_ & 0xf0fU) | (direction << 4);
        } else
            direction = (anim_ >> 4) & 0xfU;
        const auto &there = grid_.at(std::size_t(next_row)).at(std::size_t(next_col));
        x += ((there.x - here.x) >> 2) * int(sub());
        y += ((there.y - here.y) >> 2) * int(sub());
        draw(penguin_walk_sprite, static_cast<std::uint16_t>(direction * 4 + sub()), x, y);
        anim_ = (anim_ + 1) & 0xff3U;
        if (sub())
            break;
        cur_x_ = next_x_;
        cur_y_ = next_y_;
        if (cell_of(cur_x_) == cell_of(target_x_) && cell_of(cur_y_) == cell_of(target_y_)) {
            stop(penguin_walk);
            play(penguin_dig);
            state_ = 3;
            break;
        }
        if (blocked_) {
            aim(cell_of(target_x_), cell_of(target_y_));
            blocked_ = false;
        }
        step();
        break;
    }
    case 3: { // digging
        if (sub() == 2) {
            holes_.emplace_back(x, y); // Panel 80:9 stamped into the background
            draw(penguin_field, 9, x, y);
        }
        if (sub() == 3) {
            auto &cell = grid_.at(std::size_t(row)).at(std::size_t(col));
            if (cell.flags & 0xfU) {
                anim_ = (anim_ & 0xffU) | 0x100U;
                treasure_ = cell.flags & 0xfU;
                treasure_x_ = cell.x;
                treasure_y_ = cell.y;
                cell.flags &= 0xf0U;
            } else
                treasure_ = 0;
        }
        draw(penguin_dig_sprite, static_cast<std::uint16_t>(((anim_ >> 4) & 0xfU) * 4 + sub()), x, y);
        anim_ = (anim_ + 1) & 0xff3U;
        if (sub())
            break;
        if (treasure_ == 0)
            state_ = 0;
        else if (treasure_ == 1) {
            state_ = 1;
            play(penguin_bomb);
        } else {
            state_ = 0;
            play(penguin_treasure_sound[std::size_t(treasure_)]);
            ++found_[std::size_t(treasure_)];
        }
        break;
    }
    case 4:
    case 5: { // happy / sad, 4 cycles
        const bool happy = state_ == 4;
        draw(happy ? penguin_happy_sprite : penguin_sad_sprite, static_cast<std::uint16_t>(anim_ & 0xfU), x, y);
        ++anim_;
        if (int(anim_ & 0xfU) == (happy ? happy_frames : sad_frames))
            anim_ = (anim_ & 0xf0U) + 0x10U;
        if ((anim_ & 0xf0U) == 0x40U) {
            stop(happy ? penguin_happy : penguin_sad);
            done_ = 1;
        }
        break;
    }
    case 6: // neither: idle for 15 frames
        draw(penguin_field, 1, x, y);
        if ((++anim_ & 0xfU) == 0xf)
            done_ = 1;
        break;
    default:
        break;
    }
    // Treasure pop-up (Panel 85+kind), 6 frames at the dug cell.
    if (((anim_ >> 8) & 0xfU) && treasure_ > 0) {
        draw(static_cast<std::uint16_t>(85 + treasure_), static_cast<std::uint16_t>(((anim_ >> 8) & 0xfU) - 1),
             treasure_x_, treasure_y_);
        anim_ += 0x100U;
        if ((anim_ & 0xf00U) == 0x700U)
            anim_ &= 0xffU;
    }
    if (here.flags & 0xf0U) // the penguin is behind the igloo: redraw it on top
        draw(penguin_field, 3, 320, 225);
    if (((state_ == 1 || state_ == 6) && (anim_ & 0xfU) == 4) || state_ == 4 || state_ == 5)
        score_shown_ = true;
}

// ---------------------------------------------------------------------------------------------
// 七彩氣球

namespace {
constexpr std::array<int, 12> balloon_speed{15, 15, 15, 15, 18, 18, 18, 24, 24, 24, 24, 18}; // ref_00475004
constexpr std::array<std::uint16_t, 10> balloon_special{9, 9, 10, 10, 10, 10, 10, 11, 11, 11}; // ref_00475039
// Panel 91 frame boxes: 1..6 44x141 @22,30; 7..12 36x116 @18,26; 13 60x122 @28,40.
Box balloon_box(unsigned frame) {
    if (frame == 13)
        return {60, 122, 28, 40};
    return frame <= 6 ? Box{44, 141, 22, 30} : Box{36, 116, 18, 26};
}
} // namespace

Balloon::Balloon(std::uint32_t seed) : Base(seed) {
    timer_ = 150;   // bd2c
    countdown_ = 5; // bd84 = 99, set to 5 by the first WM_PAINT
}

// fcn_00414bbc WM_TIMER (SG:3763).
void Balloon::tick() {
    if (stage_ == Stage::done)
        return;
    if (stage_ == Stage::ending) {
        if (--hold_ == 0)
            stage_ = Stage::done; // fcn_0045285e(2000) then close
        return;
    }
    if (countdown_) {
        if (--countdown_ == 0)
            stage_ = Stage::ready;
        return;
    }
    if (stage_ != Stage::play)
        return;
    if (freeze_)
        --freeze_;
    if (timer_ && --timer_ == 0)
        done_ = 1;
    step();
    if (done_ == 2) {
        stage_ = Stage::ending;
        hold_ = 20;
    }
}

// fcn_00412f6f.
void Balloon::step() {
    scene_.clear();
    bool any = false;
    for (auto &slot : slots_) {
        if (!slot.x) {
            if (done_)
                continue;
            const int r = random_.next() % 1000;
            int kind = -1;
            if (r < 20)
                kind = r >> 2;
            else if (r < 28)
                kind = ((27 - r) >> 1) + 5;
            else if (r < 30)
                kind = balloon_special[random_.next() % 10];
            if (kind < 0)
                continue;
            std::array<int, 8> lanes{};
            int free = 0;
            for (int lane = 40; lane < 640; lane += 80) {
                const bool taken = std::any_of(slots_.begin(), slots_.end(),
                                               [&](const Slot &other) { return other.x == lane && other.y > 300; });
                if (!taken)
                    lanes[std::size_t(free++)] = lane;
            }
            if (!free)
                continue;
            play(balloon_rise);
            slot.x = static_cast<std::int16_t>(lanes[std::size_t(random_.next() % free)]);
            slot.y = 420;
            slot.kind = static_cast<std::uint16_t>(kind);
            continue; // a new balloon is first drawn next tick
        }
        if (slot.kind & 0xf0U) {
            slot.kind = static_cast<std::uint16_t>(slot.kind - 0x10U);
            if (!(slot.kind & 0xf0U)) {
                slot = {};
                continue;
            }
        } else if (!freeze_) {
            int speed = balloon_speed.at(slot.kind & 0xfU);
            if (modifier_ == -1)
                speed += speed;
            else if (modifier_ == 1)
                speed >>= 1;
            slot.y = static_cast<std::int16_t>(slot.y - speed);
        }
        const unsigned frame = (slot.kind & 0xfU) + 1U;
        const auto box = balloon_box(frame);
        const int left = slot.x - box.origin_x, top = slot.y - box.origin_y;
        if (left >= 640 || left + box.width <= 0 || top >= 387 || top + box.height <= 0) {
            slot = {}; // _draw_non_zero_image_in_rect: entirely outside the 640x387 field
            continue;
        }
        scene_.push_back({'p', 91, static_cast<std::uint16_t>(frame), slot.x, slot.y, 0});
        any = true;
    }
    if (!any && done_ == 1)
        done_ = 2;
}

// WM_LBUTTONDOWN (SG:3881).
int Balloon::tap(int x, int y) {
    if (stage_ != Stage::play || done_ == 2 || countdown_)
        return 0;
    int popped = 0;
    for (auto &slot : slots_) {
        if (!slot.x || (slot.kind & 0xf0U))
            continue;
        const int half_w = slot.kind >= 6 ? 18 : 22, half_h = slot.kind >= 6 ? 26 : 30;
        if (x < slot.x - half_w || x > slot.x + half_w || y < slot.y - half_h || y > slot.y + half_h) {
            play(balloon_miss);
            continue;
        }
        play(balloon_pop);
        ++popped;
        switch (slot.kind) {
        case 9:
            score_ += score_;
            break;
        case 10:
            score_ >>= 1;
            break;
        case 11:
            freeze_ = 0;
            modifier_ = 0;
            switch (random_.next() % 6) { // ref_00414ba4
            case 0:
                timer_ = 1;
                break;
            case 1:
                freeze_ = 20;
                break;
            case 2:
                modifier_ = -1;
                break;
            case 3:
                modifier_ = 1;
                break;
            case 4:
                score_ = 0;
                break;
            default:
                score_ += score_;
                break;
            }
            break;
        default:
            score_ += slot.kind + 1;
            if (score_ >= 1000)
                score_ = 999;
            break;
        }
        slot.kind = 0x3c; // the burst (frame 13) for 3 ticks
    }
    return popped;
}

// ---------------------------------------------------------------------------------------------
// 喜從天降

namespace {
constexpr std::array<int, 5> fall_speed{24, 18, 15, 12, 15}; // ref_00475010
// ref_00475015: 財神 frame per (state * 6 + step).
constexpr std::array<std::uint8_t, 36> god_frames{0,  1,  2,  3,  5,  6,  0,  1,  2,  4,  5,  6,
                                                  7,  8,  9,  10, 11, 0,  11, 10, 9,  8,  7,  0,
                                                  12, 13, 14, 15, 17, 18, 12, 13, 14, 16, 17, 18};
using E = numeric::Extended;
} // namespace

Catcher::Catcher(std::uint32_t seed, std::uint8_t character, std::vector<Box> catcher_frames, int explosion_ticks)
    : Base(seed), catcher_entry_(static_cast<std::uint16_t>(100 + character % 12)), frames_(std::move(catcher_frames)), explosion_ticks_(std::max(1, explosion_ticks)) {
    if (frames_.size() < 5)
        throw std::invalid_argument("catcher sprite needs at least 5 frames");
    per_direction_ = int(frames_.size() - 5) >> 1; // bd52
    timer_ = 360;                                  // bd2c (0x168)
    countdown_ = 10;                               // bd8c = 99, 10 after the first WM_PAINT
    step();                                        // fcn_0041473b at message 0x401
}

int Catcher::drift(int x) {
    // fcn_004123d7: trunc((x-320)/210f*260f - (x-320)) on the x87.
    const auto t = E::from(double(float(x - 320)));
    return ((t / E::from(210.0) * E::from(260.0)) - t).integer();
}

std::int32_t Catcher::score() const noexcept {
    return caught_[1] * 5 + caught_[0] * 10 + caught_[2] * 3 + caught_[3]; // fcn_0041417e
}

int Catcher::catcher_frame() const noexcept {
    return direction_ ? (direction_ - 1) * per_direction_ + 5 + walk_ : pose_;
}

void Catcher::spawn(int x, bool bomb) {
    const auto free = std::find_if(slots_.begin(), slots_.end(), [](const Slot &slot) { return !slot.x; });
    if (free == slots_.end())
        return;
    int kind = 4;
    if (!bomb) {
        const int r = random_.next() % 20;
        kind = r < 9 ? 3 : r < 15 ? 2 : r < 18 ? 1 : 0;
    } else
        ++bombs_;
    free->x = static_cast<std::int16_t>(x);
    free->y = 100;
    free->bits = static_cast<std::uint16_t>(static_cast<std::uint32_t>(kind) + (static_cast<std::uint32_t>(drift(x)) << 8));
    free->vy = -16;
}

// fcn_00414fcd WM_TIMER (SG:4061).
void Catcher::tick() {
    if (stage_ == Stage::done)
        return;
    if (stage_ == Stage::ending) {
        if (--hold_ == 0)
            stage_ = Stage::done;
        return;
    }
    if (countdown_) {
        if (--countdown_ == 0)
            stage_ = Stage::ready;
        return;
    }
    if (stage_ != Stage::play)
        return;
    if (timer_ && --timer_ == 0)
        done_ = 1;
    step();
    if (done_ == 2) {
        if (pose_ != 4) {
            direction_ = 0;
            const auto points = score();
            pose_ = points < 40 ? 1 : points < 50 ? 2 : points < 60 ? 0 : 3;
        }
        step(); // fcn_0041473b redraws through one more simulation step
        stage_ = Stage::ending;
        hold_ = 40; // fcn_0045285e(2000)
    }
    if (exploding_ && ++explosion_age_ >= explosion_ticks_) { // fcn_00450f04 reports the end
        exploding_ = false;
        done_ = 2;
    }
}

// fcn_00413248.
void Catcher::step() {
    scene_.clear();
    bool any = false;
    for (auto &slot : slots_) {
        if (!slot.x)
            continue;
        const int above = slot.y >= 130 ? slot.y - 130 : 0;
        const float ratio = (E::from(above) / E::from(250.0)).single();
        const int vx = static_cast<std::int16_t>(slot.bits) >> 8;
        const int dx = (E::from(vx) * E::from(double(ratio))).integer();
        const auto scale = static_cast<std::uint32_t>(
            (E::from(double(ratio)) * E::from(32768.0) + E::from(32768.0)).integer());
        const int x = slot.x + dx;
        const int kind = slot.bits & 0xf;
        if (direction_) {
            const auto &box = frames_.at(std::size_t(catcher_frame()));
            const int left = catcher_x_ - box.origin_x, top = 380 - box.origin_y;
            if (x > left && x < left + box.width && slot.y > top && slot.y < top + box.height && !exploding_) {
                if (kind == 4) {
                    stop(bomb_fuse);
                    play(bomb_blast);
                    exploding_ = true;
                    explosion_x_ = catcher_x_ - 55;
                    explosion_age_ = 0;
                    direction_ = 0;
                    pose_ = 4;
                    done_ = 1;
                } else
                    ++caught_[std::size_t(kind)];
                slot = {};
            }
        }
        if (slot.x) {
            scene_.push_back({'p', static_cast<std::uint16_t>(95 + kind),
                              static_cast<std::uint16_t>((slot.bits & 0xf0U) >> 4), x, slot.y, scale});
            slot.bits = static_cast<std::uint16_t>((slot.bits & 0xff00U) | ((slot.bits + 0x10U) & 0x7fU));
            if (slot.y < 130) {
                slot.vy = static_cast<std::int16_t>(std::min(16, slot.vy + 2));
                slot.y = static_cast<std::int16_t>(slot.y + slot.vy);
            } else
                slot.y = static_cast<std::int16_t>(slot.y + fall_speed[std::size_t(kind)]);
            if (slot.y > 380) {
                if (kind == 4 && --bombs_ == 0)
                    stop(bomb_fuse);
                slot = {};
            }
        }
        if (slot.x)
            any = true;
    }
    if (goblin_ != -1)
        scene_.push_back({'p', 94, static_cast<std::uint16_t>(goblin_), goblin_x_, 125, 0});
    scene_.push_back(
        {'p', 93, god_frames.at(std::size_t(god_state_ * 6 + god_step_)), god_x_, 126, 0});
    if (done_ != 2 && !exploding_) { // follow the pointer x, 10 px per tick outside an 8 px dead zone
        const int distance = catcher_x_ - target_;
        if (std::abs(distance) > 8) {
            direction_ = distance > 0 ? 1 : 2;
            catcher_x_ += distance > 0 ? -10 : 10;
            if (++walk_ == per_direction_)
                walk_ = 0;
        }
    }
    scene_.push_back({'p', catcher_entry_, static_cast<std::uint16_t>(catcher_frame()), catcher_x_, 380, 0});
    // Goblin with a bomb.
    if (goblin_ == -1) {
        if ((god_state_ < 2 && god_x_ > 320) || (god_state_ > 3 && god_x_ < 320))
            if (random_.next() % 10 >= 7 && done_ == 0) {
                const int r = random_.next() % 140;
                goblin_ = 0;
                goblin_x_ = r + (god_x_ > 320 ? 160 : 360);
            }
    } else {
        if (++goblin_ == 8 && !exploding_) {
            play(goblin_throw);
            loop(bomb_fuse);
            spawn(goblin_x_, true);
        }
        if (goblin_ == 12)
            goblin_ = -1;
    }
    // 財神 walk (jump table ref_00413234).
    if (done_ == 1) {
        god_state_ = 2;
        god_step_ = 2; // faces front (frame 9) and stops throwing
    } else
        switch (god_state_) {
        case 0:
        case 4: {
            const bool right = god_state_ == 0;
            if (god_step_ < 5) {
                if (god_step_ == throw_at_)
                    spawn(god_x_, false);
                ++god_step_;
                god_x_ += right ? 12 : -12;
                break;
            }
            bool turn = false;
            if (right ? god_x_ > 320 : god_x_ < 320)
                turn = random_.next() % 4 == 0;
            if (!turn)
                turn = god_x_ == (right ? 530 : 110);
            if (turn)
                god_state_ = right ? 2 : 3;
            else {
                throw_at_ = random_.next() % 5;
                god_x_ += right ? 12 : -12;
            }
            god_step_ = 0;
            break;
        }
        case 2:
        case 3:
            if (++god_step_ == 5) {
                god_state_ = god_state_ == 2 ? 4 : 0;
                god_step_ = 0;
            }
            break;
        default:
            break;
        }
    if (!any && done_ == 1)
        done_ = 2;
}

} // namespace rich4::minigames
