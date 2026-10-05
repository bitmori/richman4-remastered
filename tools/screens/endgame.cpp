// SPDX-License-Identifier: GPL-3.0-or-later
// End-of-game screens, see endgame.hpp. Asm references are rich4_new_game.asm unless noted.
#include "endgame.hpp"

#include <algorithm>
#include <exception>

namespace rich4::native::screens {
namespace {

constexpr unsigned timer_ms = 50;           // SetTimer(..., 0x32) of fcn_004060e9
constexpr int panel_rest_y = 360;           // 0x168
constexpr std::array<std::array<std::size_t, 6>, 2> god_frames{{{0, 1, 2, 3, 5, 6}, {12, 13, 14, 15, 17, 18}}};
constexpr int movie_entry = 556;            // Data.mkf "倒閉" letters crumbling
constexpr int movie_sound = 101;            // fcn_0045144f 5th argument 0x65

} // namespace

void EndgameScreen::open_play_again() {
    close();
    kind_ = Kind::play_again;
    step_ = Step::countdown;
}

void EndgameScreen::open_stage_clear(const StageClear &info) {
    close();
    kind_ = Kind::stage_clear;
    info_ = info;
    random_.seed(info.seed ? info.seed : 1);
}

void EndgameScreen::close() {
    kind_ = Kind::none;
    started_ = false;
    countdown_ms_ = movie_ms_ = tick_ms_ = 0;
    movie_.reset();
    scroll_ = run_x_ = run_y_ = 0;
    run_left_ = false;
    winner_step_ = god_step_ = 0;
    panel_y_ = -80;
    panel_speed_ = 30;
    picked_ = -1;
    picked_ticks_ = 0;
    leave_ = false;
    backdrop_.reset();
    panel_.reset();
}

int EndgameScreen::exit_button() const noexcept {
    if (kind_ == Kind::play_again)
        return step_ == Step::countdown ? ids::endgame_play_again : -1;
    if (kind_ != Kind::stage_clear || !started_ || picked_ >= 0)
        return -1;
    const bool panel = info_.single_human && info_.cleared != 0x0f;
    if (!panel)
        return ids::endgame_continue;
    if (panel_y_ < panel_rest_y)
        return -1;
    for (unsigned map = 0; map < 4; ++map)
        if (!(info_.cleared & (1U << map)))
            return ids::endgame_plate + int(map);
    return -1;
}

void EndgameScreen::draw(Host &host, bool hold, const EndgameActions &actions) {
    if (!active())
        return;
    const auto now = host.frame.ticks;
    const unsigned elapsed = last_ticks_ && now > last_ticks_ ? unsigned(std::min<std::uint64_t>(now - last_ticks_, 100)) : 0;
    last_ticks_ = now;
    if (kind_ == Kind::play_again)
        draw_play_again(host, elapsed, hold, actions);
    else
        draw_stage_clear(host, elapsed, hold, actions);
}

// fcn_00407842 / window proc fcn_00406b14.
void EndgameScreen::draw_play_again(Host &host, unsigned elapsed, bool hold, const EndgameActions &actions) {
    auto &frame = host.frame;
    switch (step_) {
    case Step::countdown: {
        if (!started_) { // the prompt appears once the bankruptcy movie and lines have played
            if (hold)
                return;
            started_ = true;
            elapsed = 0;
        }
        countdown_ms_ += elapsed;
        const int counter = 10 - int(countdown_ms_ / 1000); // 0x48a440, decremented by a 1 s timer
        if (counter <= 0) { // timeout posts 0x205 with result 0
            step_ = Step::done;
            kind_ = Kind::none;
            if (actions.play_again_done)
                actions.play_again_done(false);
            return;
        }
        graphics::blit(frame.canvas, frame.artwork.frame('p', 112, 0), 320, 120);
        graphics::blit(frame.canvas, frame.artwork.frame('p', 112, std::size_t(counter)), 320, 350); // digit counter-1
        hit(host, ids::endgame_play_again, {0, 0, 640, 480}, no_sound, [this, actions] {
            step_ = Step::line; // button-up / hotkey: result 1
            if (actions.agree)
                actions.agree();
        });
        return;
    }
    case Step::line: // _rich4_player_say(player | 0x8000, 3, event slot 26) blocks until it ends
        if (hold)
            return;
        step_ = Step::movie;
        movie_ms_ = 0;
        if (!host.instant) {
            try {
                movie_ = std::make_unique<flc::Decoder>(frame.artwork.archive('d').decode(movie_entry));
                if (movie_->next())
                    movie_frame_ = movie_->image();
                else
                    movie_.reset();
            } catch (const std::exception &) {
                movie_.reset();
            }
            if (movie_)
                host.sound(movie_sound);
        }
        [[fallthrough]];
    case Step::movie: {
        if (movie_) {
            movie_ms_ += elapsed;
            const unsigned delay = std::max(1U, unsigned(movie_->delay_ms()));
            while (movie_ && movie_ms_ >= delay) {
                movie_ms_ -= delay;
                if (movie_->next())
                    movie_frame_ = movie_->image();
                else
                    movie_.reset();
            }
        }
        if (movie_) { // fcn_0045144f(flc, 0, 0x28, 1, 101): overlay over the board view
            graphics::blit(frame.canvas, movie_frame_, 0, 40, graphics::Transparency::source_key);
            frame.buttons.clear(); // the movie blocks input
            return;
        }
        step_ = Step::done;
        kind_ = Kind::none;
        if (actions.play_again_done)
            actions.play_again_done(true);
        return;
    }
    case Step::done:
        return;
    }
}

// fcn_004060e9 0x113 (WM_TIMER), one 50 ms step.
void EndgameScreen::stage_tick() {
    scroll_ += 2; // 4 bytes = 2 RGB555 pixels, wrapping at 0x500 bytes
    if (scroll_ >= 640)
        scroll_ = 0;
    if (run_y_ == 0) {
        run_y_ = int(random_() % 360U) + 100;
        run_left_ = !run_left_;
        run_x_ = run_left_ ? 740 : -100;
    } else if (!run_left_) {
        run_x_ += 10;
        if (run_x_ >= 740)
            run_y_ = 0;
    } else {
        run_x_ -= 10;
        if (run_x_ <= -100)
            run_y_ = 0;
    }
    ++winner_step_;
    god_step_ = (god_step_ + 1) % 6;
    if (panel_y_ < panel_rest_y) { // the map panel drops in, decelerating
        panel_y_ += panel_speed_;
        --panel_speed_;
        if (panel_y_ > panel_rest_y)
            panel_y_ = panel_rest_y;
    }
    if (picked_ >= 0 && picked_ticks_ && --picked_ticks_ == 0)
        leave_ = true; // 0x48a43b reached 0: post 0x202
}

// fcn_004075c1 + fcn_004060e9.
void EndgameScreen::draw_stage_clear(Host &host, unsigned elapsed, bool hold, const EndgameActions &actions) {
    auto &frame = host.frame;
    auto &artwork = frame.artwork;
    if (!started_) {
        if (hold) // the winner's line (event slot 24) plays over the board first
            return;
        started_ = true;
        elapsed = 0;
        // TODO(audio): Audio::push_music(MIDI07) here (fcn_004549cf(0x8006)) and pop it when leaving.
        backdrop_ = raw_picture(artwork, 'j', info_.stage * 4 + info_.map, 640, 480);
        // Cleared maps get the star (jump 8:10) on their panel row and plate (fcn_004562a5).
        const std::size_t panel_frame = info_.stage ? 20 : 15, plate_base = info_.stage ? 16 : 11;
        panel_ = artwork.frame('j', 8, panel_frame);
        for (unsigned map = 0; map < 4; ++map) {
            plates_[map] = artwork.frame('j', 8, plate_base + map);
            if (info_.cleared & (1U << map)) {
                graphics::blit(plates_[map], artwork.frame('j', 8, 10), 32, 22);
                graphics::blit(*panel_, artwork.frame('j', 8, 10), 220, 28 + 40 * int(map));
            }
        }
    }
    tick_ms_ += elapsed;
    if (host.instant)
        panel_y_ = panel_rest_y;
    while (tick_ms_ >= timer_ms) {
        tick_ms_ -= timer_ms;
        stage_tick();
    }
    if (picked_ >= 0 && (leave_ || host.instant)) { // 10 ticks after the pick: exit code 2
        const auto map = unsigned(picked_);
        kind_ = Kind::none;
        if (actions.next_map)
            actions.next_map(map);
        return;
    }
    // Backdrop scrolled left by scroll_ pixels (fcn_00456180).
    graphics::blit(frame.canvas, *backdrop_, -scroll_, 0, graphics::Transparency::opaque);
    graphics::blit(frame.canvas, *backdrop_, 640 - scroll_, 0, graphics::Transparency::opaque);
    // The winner chases 財神 (fcn_0045663e, anchored on the sprites' origins).
    if (run_y_) {
        const std::size_t winner_entry = 100U + info_.character;
        const auto count = artwork.frame_count('p', winner_entry);
        const unsigned steps = count > 4 ? unsigned(count - 4) / 2 : 1;
        winner_step_ %= steps;
        const auto winner_frame =
            std::min<std::size_t>((run_left_ ? 0 : steps) + winner_step_ + 5, count ? count - 1 : 0);
        graphics::blit(frame.canvas, artwork.frame('p', winner_entry, winner_frame), run_left_ ? run_x_ + 90 : run_x_ - 90,
                       run_y_);
        graphics::blit(frame.canvas, artwork.frame('p', 93, god_frames[run_left_ ? 1 : 0][god_step_]),
                       run_left_ ? run_x_ - 90 : run_x_ + 90, run_y_);
    }
    const bool panel = info_.single_human && info_.cleared != 0x0f;
    if (!panel) {
        // Multi-human (and, natively, a fully cleared stage whose END*.AVI the port cannot play).
        text(frame, "恭喜勝利過關！！", 320, 240, 2, {48, 0xf0f0f0, 0x101010, shadow | bold});
        hit(host, ids::endgame_continue, {0, 0, 640, 480}, exit_sound, [this, actions] {
            kind_ = Kind::none; // click: state 1, then 0x202 ends with exit code 3
            if (actions.to_menu)
                actions.to_menu();
        });
        return;
    }
    graphics::blit(frame.canvas, *panel_, 320, panel_y_);
    if (panel_y_ < panel_rest_y)
        return; // input is enabled once the panel has landed (fcn_00402460(1))
    if (picked_ >= 0) {
        graphics::blit(frame.canvas, plates_[std::size_t(picked_)], 320, 300 + 40 * picked_);
        frame.buttons.clear();
        return;
    }
    for (unsigned map = 0; map < 4; ++map) {
        const graphics::Rect row{192, 280 + 40 * int(map), 448, 320 + 40 * int(map)}; // x 0xc8..0x1b8
        const bool cleared = info_.cleared & (1U << map);
        if (!cleared && frame.pointer_in(row)) // hover highlight (0x200), not for cleared maps
            graphics::blit(frame.canvas, plates_[map], 320, 300 + 40 * int(map));
        hit(host, ids::endgame_plate + int(map), row, exit_sound, // 0x201: ref_0048232a
            [this, map, &artwork] {
                picked_ = int(map);
                picked_ticks_ = 10; // 0x48a43b
                graphics::blit(plates_[map], artwork.frame('j', 8, 8), 214, 8); // check mark
            },
            !cleared);
    }
}

} // namespace rich4::native::screens
