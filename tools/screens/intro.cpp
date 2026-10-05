// SPDX-License-Identifier: GPL-3.0-or-later
// See intro.hpp. Values are from fcn_00415872 (rich4.asm:7166-7710) and fcn_0045144f.
#include "intro.hpp"

#include "draw.hpp"

#include <algorithm>
#include <iostream>

namespace rich4::native {
namespace {

constexpr int jump_sprites = 45;    // 0x2d: 0 plane, 1 sky, 2 door frame, 3+character waiting
constexpr int door_movie = 46;      // 0x2e: door opens, 220x240 at (180,60)
constexpr int jump_movie = 47;      // 0x2f + character
constexpr int parachute_movie = 59; // 0x2f + 12 + character
constexpr int door_sound = 25;      // ref_004750f8: played just before the door FLC
constexpr int jump_sound = 26;      // ref_00475100: preset for frame 20 (flags 0x14000003)
constexpr int jump_sound_frame = 20;
constexpr int waiting_brightness = -20; // fcn_004552b7(..., -20) on the waiting player's sprite
// _rich4_show_loading_scene shows Data 601 for as long as the map takes to load; the native
// load is instant, so hold it briefly instead (UNVERIFIED duration).
constexpr unsigned loading_hold_ms = 400;

} // namespace

void IntroPlayer::start(const std::vector<std::uint8_t> &characters) {
    shots_.clear();
    index_ = 0;
    movie_.reset();
    started_ = false;
    try {
        auto loading = std::make_shared<graphics::Image>(screens::raw_picture(artwork_, 'd', 601, 640, 480));
        shots_.push_back({loading, -1, 0, 0, -1, -1, 0, loading_hold_ms});

        const auto &plane = artwork_.frame('j', jump_sprites, 0);
        const auto backdrop = [&] {
            auto image = std::make_shared<graphics::Image>(graphics::make_canvas(640, 480));
            graphics::blit(*image, plane, 0, 0, graphics::Transparency::opaque);
            return image;
        };
        std::shared_ptr<const graphics::Image> open_door = backdrop();
        shots_.push_back({open_door, door_movie, 180, 60, door_sound, -1, 0, 0});

        // Iteration i shows player i waiting in the doorway while player i-1 jumps; after the
        // last jump the doorway is empty. Iteration 0 has no movie, so it takes no time.
        for (std::size_t i = 1; i <= characters.size(); ++i) {
            std::shared_ptr<const graphics::Image> still = open_door;
            if (i < characters.size()) {
                auto image = backdrop();
                auto waiting = artwork_.frame('j', jump_sprites, 3U + characters[i] % 12U);
                screens::darken(waiting, {0, 0, waiting.width, waiting.height}, waiting_brightness);
                graphics::blit(*image, waiting, 270, 220);
                graphics::blit(*image, artwork_.frame('j', jump_sprites, 2), 150, 60);
                still = image;
            }
            shots_.push_back({still, jump_movie + characters[i - 1] % 12, 0, 0, -1, jump_sound, jump_sound_frame, 0});
        }

        auto sky = std::make_shared<graphics::Image>(graphics::make_canvas(640, 480));
        graphics::blit(*sky, artwork_.frame('j', jump_sprites, 1), 0, 0, graphics::Transparency::opaque);
        for (const auto character : characters)
            shots_.push_back({sky, parachute_movie + character % 12, 0, 0, -1, -1, 0, 0});
    } catch (const std::exception &error) { // A broken asset must never stop the game.
        std::cerr << "Intro skipped: " << error.what() << '\n';
        shots_.clear();
    }
}

void IntroPlayer::skip() noexcept {
    index_ = shots_.size();
    movie_.reset();
}

void IntroPlayer::begin_shot() {
    const auto &shot = shots_[index_];
    movie_.reset();
    elapsed_ = 0;
    sounded_ = false;
    started_ = true;
    if (shot.movie >= 0) {
        movie_ = std::make_unique<flc::Decoder>(artwork_.archive('j').decode(static_cast<std::size_t>(shot.movie)));
        delay_ = movie_->delay_ms() ? movie_->delay_ms() : 100;
        if (movie_->next())
            frame_ = movie_->image();
    }
}

void IntroPlayer::draw(graphics::Image &canvas, unsigned elapsed_ms, const SoundFunction &play_sound) {
    if (!active())
        return;
    const auto sound = [&](int index) {
        if (index >= 0 && play_sound)
            play_sound(static_cast<std::size_t>(index));
    };
    try {
        elapsed_ms = std::min(elapsed_ms, 100U);
        if (!started_) {
            begin_shot();
            sound(shots_[index_].start_sound);
            elapsed_ms = 0; // the first frame is shown in full
        }
        elapsed_ += elapsed_ms;
        // Finished shots hand their overshoot to the next one, so a slow frame never stretches
        // the sequence (fcn_0045144f waits per frame against the wall clock).
        for (;;) {
            const auto &shot = shots_[index_];
            const unsigned length =
                movie_ ? static_cast<unsigned>(std::max<std::size_t>(1, movie_->frame_count())) * delay_
                       : shot.hold_ms;
            if (elapsed_ < length)
                break;
            const auto carry = elapsed_ - length;
            if (++index_ >= shots_.size()) {
                movie_.reset();
                return; // the caller draws the board this frame
            }
            begin_shot();
            sound(shots_[index_].start_sound);
            elapsed_ = carry;
        }
        const auto &shot = shots_[index_];
        if (movie_) {
            const auto frame_index = elapsed_ / delay_;
            while (movie_->decoded_frames() <= frame_index && movie_->next())
                frame_ = movie_->image();
            if (!sounded_ && shot.frame_sound >= 0 && frame_index >= unsigned(shot.sound_frame)) {
                sound(shot.frame_sound);
                sounded_ = true;
            }
        }
        graphics::blit(canvas, *shot.still, 0, 0, graphics::Transparency::opaque);
        if (movie_) // overlay (flags bit 0): FLC index 0 shows the still underneath
            graphics::blit(canvas, frame_, shot.x, shot.y);
    } catch (const std::exception &error) {
        std::cerr << "Intro skipped: " << error.what() << '\n';
        skip();
    }
}

} // namespace rich4::native
