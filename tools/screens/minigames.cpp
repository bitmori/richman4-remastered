// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing and touch input of the three original mini-games (docs/ui/mini-games.md). Layout from
// rich4_small_games.asm: HUD digits Panel 79:0..9 at y 421 (fcn_00413a4a / fcn_00413f07 /
// fcn_0041417e), the big score Panel 79:10..19 at y 150 centred on x 353 (fcn_00414789), the
// READY/GO overlay Panel 78 at (0,0) (message 0x405), the bomb blast Data 526 at (x-55, 295).
#include "minigames.hpp"

#include "draw.hpp"
#include "rich4/graphics.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace rich4::native::minigames {
namespace {

constexpr graphics::Rect field{0, 0, 640, 387}; // the play area above the HUD bar
constexpr int hud_y = 421;
constexpr std::array<const char *, 3> game_names{"penguin", "balloon", "catch"};

// fcn_004568c2: the sprite's unscaled anchor offset, then a nearest-neighbour scaled copy.
void blit_scaled(graphics::Image &canvas, const graphics::Image &image, int x, int y, std::uint32_t scale) {
    if (!scale || !image.width || !image.height)
        return;
    const int width = int((std::uint64_t(image.width) * scale) >> 16);
    const int height = int((std::uint64_t(image.height) * scale) >> 16);
    const int left = x - image.origin_x, top = y - image.origin_y;
    for (int row = std::max(0, top); row < std::min(field.bottom, top + height); ++row) {
        const int source_row = int((std::uint64_t(row - top) << 16) / scale);
        if (source_row >= image.height)
            continue;
        for (int column = std::max(0, left); column < std::min(field.right, left + width); ++column) {
            const int source_column = int((std::uint64_t(column - left) << 16) / scale);
            if (source_column >= image.width)
                continue;
            const auto index = std::size_t(source_row * image.width + source_column);
            const bool visible = image.opaque_pixels.empty() ? image.pixels[index] != 0 : image.opaque_pixels[index] != 0;
            if (visible)
                canvas.pixels[std::size_t(row * canvas.width + column)] = image.pixels[index];
        }
    }
}

std::string format(const char *pattern, long value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), pattern, value);
    return buffer.data();
}

// Panel 79 LCD digits (15x28), one character per x position, like the original's sprintf loops.
void lcd(Frame &frame, const std::string &text, std::initializer_list<int> xs) {
    std::size_t i = 0;
    for (const int x : xs) {
        if (i >= text.size())
            break;
        const char c = text[i++];
        if (c >= '0' && c <= '9')
            graphics::blit(frame.canvas, frame.artwork.frame('p', 79, std::size_t(c - '0')), x, hud_y,
                           graphics::Transparency::opaque);
    }
}

// fcn_00414789: the big balloon-digit score.
void big_score(graphics::Image &target, Artwork &artwork, std::int32_t score) {
    const auto text = std::to_string(score);
    int x = 0x161 - int(text.size()) * 33;
    for (const char c : text) {
        if (c >= '0' && c <= '9')
            graphics::blit(target, artwork.frame('p', 79, std::size_t(10 + c - '0')), x, 150);
        x += 0x42;
    }
}

} // namespace

struct MiniGames::Playing {
    core::Game game = core::Game::penguin;
    std::uint32_t seed = 0;
    std::uint8_t character = 0;
    std::variant<std::monostate, core::Penguin, core::Balloon, core::Catcher> logic;
    graphics::Image background;
    std::size_t holes_stamped = 0;
    bool score_stamped = false;
    std::vector<std::uint8_t> hit_map; // Panel 81
    std::unique_ptr<flc::Decoder> ready, blast;
    unsigned ready_elapsed = 0;
    int blast_shown = -1;
    std::uint64_t last_ticks = 0;
    unsigned carry = 0;
    std::size_t loop = 0;
    bool bot = false;
    int bot_wait = 0;
    std::filesystem::path captures;
    std::set<std::string> captured;
    unsigned frames = 0;
    std::optional<Result> result;

    core::Base &base() {
        return std::visit(
            [](auto &value) -> core::Base & {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::monostate>)
                    throw std::logic_error("mini-game not started");
                else
                    return value;
            },
            logic);
    }
    std::int32_t score() {
        return std::visit(
            [](auto &value) -> std::int32_t {
                if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::monostate>)
                    return 0;
                else
                    return value.score();
            },
            logic);
    }
};

MiniGames::MiniGames(Sound sound) : sound_(std::move(sound)) {}
MiniGames::~MiniGames() = default;

bool handles(const GameDecision &decision) {
    return decision.kind == DecisionKind::mini_game && decision.mini_game.has_value();
}

void MiniGames::clear() {
    if (playing_ && playing_->loop && sound_.stop_loop)
        sound_.stop_loop();
    playing_.reset();
}

void MiniGames::begin(core::Game game, std::uint32_t seed, std::uint8_t character, Frame &frame) {
    clear();
    auto playing = std::make_unique<Playing>();
    playing->game = game;
    playing->seed = seed;
    playing->character = static_cast<std::uint8_t>(character % 12);
    switch (game) {
    case core::Game::penguin:
        playing->logic.emplace<core::Penguin>(seed);
        playing->background = frame.artwork.frame('p', 80, 0);
        playing->hit_map = frame.artwork.archive('p').decode(81);
        break;
    case core::Game::balloon:
        playing->logic.emplace<core::Balloon>(seed);
        playing->background = frame.artwork.frame('p', 91, 0);
        break;
    case core::Game::catcher: {
        std::vector<core::Box> boxes;
        const std::size_t entry = 100U + playing->character;
        for (std::size_t i = 0; i < frame.artwork.frame_count('p', entry); ++i) {
            const auto &image = frame.artwork.frame('p', entry, i);
            boxes.push_back({image.width, image.height, image.origin_x, image.origin_y});
        }
        playing->blast = std::make_unique<flc::Decoder>(frame.artwork.archive('d').decode(526));
        const auto blast_ms = playing->blast->frame_count() * playing->blast->delay_ms();
        playing->logic.emplace<core::Catcher>(seed, playing->character, std::move(boxes),
                                              int((blast_ms + 49) / 50));
        playing->background = screens::raw_picture(frame.artwork, 'p', 92, 640, 480);
        break;
    }
    }
    playing->last_ticks = frame.ticks;
    playing_ = std::move(playing);
    // MIDI 13 / 12 / 11 (fcn_004549cf(0xc / 0xb / 0xa)) is selected by the app's music controller
    // while an in-game mini-game is up.
}

void MiniGames::start(core::Game game, std::uint32_t seed, std::uint8_t character, Frame &frame, bool autoplay,
                      std::filesystem::path capture_directory) {
    begin(game, seed, character, frame);
    playing_->bot = autoplay;
    playing_->captures = std::move(capture_directory);
}

std::optional<Result> MiniGames::draw_decision(Frame &frame, const GameDecision &decision) {
    const auto &state = *decision.mini_game;
    if (!playing_ || playing_->seed != state.seed || playing_->game != core::Game(state.game))
        begin(core::Game(state.game % 3), state.seed, state.character, frame);
    auto result = run(frame);
    if (result)
        clear(); // the caller submits it; the next mini-game starts fresh even with the same seed
    return result;
}

std::optional<Result> MiniGames::draw(Frame &frame) {
    if (!playing_)
        return std::nullopt;
    return run(frame);
}

std::optional<Result> MiniGames::run(Frame &frame) {
    auto &p = *playing_;
    auto &logic = p.base();
    const unsigned elapsed = fixed_step_ ? fixed_step_
                                         : unsigned(std::min<std::uint64_t>(frame.ticks - p.last_ticks, 100));
    p.last_ticks = frame.ticks;
    ++p.frames;
    if (logic.stage() == core::Stage::ready) {
        // Message 0x405: the READY/GO movie, 114 ms a frame, blocks the game until it ends.
        if (!p.ready) {
            p.ready = std::make_unique<flc::Decoder>(frame.artwork.archive('p').decode(78));
            p.ready->next();
            p.ready_elapsed = 0;
        } else {
            p.ready_elapsed += elapsed;
            while (p.ready_elapsed >= p.ready->delay_ms()) {
                p.ready_elapsed -= p.ready->delay_ms();
                if (!p.ready->next()) {
                    std::visit(
                        [](auto &value) {
                            if constexpr (!std::is_same_v<std::decay_t<decltype(value)>, std::monostate>)
                                value.begin_play();
                        },
                        p.logic);
                    p.ready.reset();
                    p.carry = 0;
                    break;
                }
            }
        }
    } else if (logic.stage() != core::Stage::done) {
        p.carry += elapsed;
        const unsigned tick = core::tick_ms(p.game);
        while (p.carry >= tick && logic.stage() != core::Stage::ready && logic.stage() != core::Stage::done) {
            p.carry -= tick;
            std::visit(
                [](auto &value) {
                    if constexpr (!std::is_same_v<std::decay_t<decltype(value)>, std::monostate>)
                        value.tick();
                },
                p.logic);
            for (const auto &cue : logic.drain_cues()) {
                if (cue.op == core::Cue::Op::play && sound_.play)
                    sound_.play(cue.effect);
                else if (cue.op == core::Cue::Op::loop) {
                    if (sound_.loop)
                        sound_.loop(cue.effect);
                    p.loop = cue.effect;
                } else if (cue.op == core::Cue::Op::stop && p.loop == cue.effect) {
                    if (sound_.stop_loop)
                        sound_.stop_loop();
                    p.loop = 0;
                }
            }
        }
    }
    if (p.bot)
        autoplay(frame);
    else
        input(frame);
    paint(frame);
    if (logic.stage() == core::Stage::done && !p.result) {
        if (p.loop && sound_.stop_loop)
            sound_.stop_loop();
        p.loop = 0;
        p.result = Result{p.score(), logic.draws()};
        return p.result;
    }
    return std::nullopt;
}

void MiniGames::input(Frame &frame) {
    auto &p = *playing_;
    frame.buttons.clear(); // the original window is modal
    auto *frame_pointer = &frame;
    frame.buttons.push_back({tap_button, {0, 0, 640, 480}, true,
                             [this, frame_pointer] {
                                 if (!playing_)
                                     return;
                                 auto &q = *playing_;
                                 const int x = frame_pointer->pointer_x, y = frame_pointer->pointer_y;
                                 if (auto *penguin = std::get_if<core::Penguin>(&q.logic)) {
                                     if (penguin->stage() == core::Stage::ending) {
                                         penguin->tap_anywhere();
                                         return;
                                     }
                                     // Panel 81 hit map; off the map snap to the nearest cell (touch).
                                     int col = -1, row = -1;
                                     if (x >= 0 && y >= 0 && x < 640 && y < 480 &&
                                         std::size_t(y * 640 + x) < q.hit_map.size()) {
                                         const int value = q.hit_map[std::size_t(y * 640 + x)];
                                         if (value && core::Penguin::valid(value % 9, value / 9))
                                             col = value % 9, row = value / 9;
                                     }
                                     if (col < 0) {
                                         int best = 40 * 40 + 1;
                                         for (int r = 0; r < 9; ++r)
                                             for (int c = 0; c < 9; ++c)
                                                 if (core::Penguin::valid(c, r)) {
                                                     const auto &cell = penguin->cell(c, r);
                                                     const int dx = cell.x - x, dy = (cell.y - 8 - y) * 2;
                                                     if (dx * dx + dy * dy < best)
                                                         best = dx * dx + dy * dy, col = c, row = r;
                                                 }
                                     }
                                     if (col >= 0)
                                         penguin->tap(col, row);
                                 } else if (auto *balloon = std::get_if<core::Balloon>(&q.logic)) {
                                     balloon->tap(x, y);
                                 } else if (auto *catcher = std::get_if<core::Catcher>(&q.logic)) {
                                     catcher->set_target(x);
                                 }
                             },
                             false});
    // 喜從天降: the catcher follows the finger while it is down (the original follows the cursor).
    if (auto *catcher = std::get_if<core::Catcher>(&p.logic); catcher && frame.pointer_down && frame.pointer_x >= 0)
        catcher->set_target(frame.pointer_x);
}

// Dev bot for headless captures: plays like a player who remembers the treasures.
void MiniGames::autoplay(Frame &frame) {
    auto &p = *playing_;
    frame.buttons.clear();
    if (p.bot_wait-- > 0)
        return;
    if (auto *penguin = std::get_if<core::Penguin>(&p.logic)) {
        if (!penguin->idle())
            return;
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c)
                if ((penguin->cell(c, r).flags & 0xfU) > 1 && penguin->tap(c, r)) {
                    p.bot_wait = 3;
                    return;
                }
    } else if (auto *balloon = std::get_if<core::Balloon>(&p.logic)) {
        const core::Balloon::Slot *best = nullptr;
        for (const auto &slot : balloon->slots())
            if (slot.x && !(slot.kind & 0xf0U) && slot.y < 330 && (!best || slot.y > best->y))
                best = &slot;
        if (best && balloon->tap(best->x, best->y))
            p.bot_wait = 6;
    } else if (auto *catcher = std::get_if<core::Catcher>(&p.logic)) {
        const core::Catcher::Slot *best = nullptr;
        for (const auto &slot : catcher->slots())
            if (slot.x && (slot.bits & 0xf) != 4 && (!best || slot.y > best->y))
                best = &slot;
        if (best)
            catcher->set_target(best->x);
    }
}

void MiniGames::capture(Frame &frame, const std::string &tag) {
    auto &p = *playing_;
    if (p.captures.empty() || !p.captured.insert(tag).second)
        return;
    std::filesystem::create_directories(p.captures);
    graphics::write_ppm(frame.canvas,
                        p.captures / ("minigame-" + std::string(game_names[std::size_t(p.game)]) + "-" + tag + ".ppm"));
}

void MiniGames::paint(Frame &frame) {
    auto &p = *playing_;
    auto &logic = p.base();
    auto &canvas = frame.canvas;
    if (auto *penguin = std::get_if<core::Penguin>(&p.logic)) {
        // Holes and the final score are stamped into the background like the original.
        for (; p.holes_stamped < penguin->holes().size(); ++p.holes_stamped) {
            const auto [x, y] = penguin->holes()[p.holes_stamped];
            graphics::blit(p.background, frame.artwork.frame('p', 80, 9), x, y);
        }
        if (penguin->score_shown() && !p.score_stamped) {
            big_score(p.background, frame.artwork, penguin->score());
            p.score_stamped = true;
        }
    }
    graphics::blit(canvas, p.background, 0, 0, graphics::Transparency::opaque);
    for (const auto &sprite : logic.scene()) {
        const auto &image = frame.artwork.frame(sprite.archive, sprite.entry, sprite.frame);
        if (sprite.scale)
            blit_scaled(canvas, image, sprite.x, sprite.y, sprite.scale);
        else
            graphics::blit(canvas, image, sprite.x, sprite.y, field);
    }
    std::int32_t score = p.score();
    if (auto *catcher = std::get_if<core::Catcher>(&p.logic)) {
        if (const auto blast = catcher->explosion()) { // Data 526 overlay at (x-55, 295)
            const int wanted = std::min<int>(int(p.blast->frame_count()) - 1,
                                             int(unsigned(blast->second) * 50U / std::max(1U, p.blast->delay_ms())));
            if (p.blast_shown < 0) {
                p.blast->rewind();
                p.blast->next();
                p.blast_shown = 0;
            }
            while (p.blast_shown < wanted && p.blast->next())
                ++p.blast_shown;
            graphics::blit(canvas, p.blast->image(), blast->first, 295);
        }
        const auto &caught = catcher->caught();
        lcd(frame, format("%03d", catcher->ticks_left() / 2) + "0", {49, 69, 94, 114});
        lcd(frame, format("%02d", caught[0]), {185, 205});
        lcd(frame, format("%02d", caught[1]), {276, 296});
        lcd(frame, format("%02d", caught[2]), {367, 387});
        lcd(frame, format("%02d", caught[3]), {458, 478});
        lcd(frame, format("%03d", score), {549, 569, 589});
    } else if (auto *penguin = std::get_if<core::Penguin>(&p.logic)) {
        const auto &found = penguin->found();
        lcd(frame, format("%03d", penguin->ticks_left()) + "0", {49, 69, 94, 114});
        lcd(frame, format("%02d", found[5]), {185, 205});
        lcd(frame, format("%02d", found[3]), {276, 296});
        lcd(frame, format("%02d", found[4]), {367, 387});
        lcd(frame, format("%02d", found[2]), {458, 478});
        lcd(frame, format("%03d", score), {549, 569, 589});
    } else if (auto *balloon = std::get_if<core::Balloon>(&p.logic)) {
        lcd(frame, format("%03d", balloon->ticks_left()) + "0", {49, 69, 94, 114});
        lcd(frame, format("%04d", score), {529, 549, 569, 589});
    }
    if (logic.stage() == core::Stage::ending && p.game != core::Game::penguin)
        big_score(canvas, frame.artwork, score);
    if (p.ready)
        graphics::blit(canvas, p.ready->image(), 0, 0);
    // Dev captures: start, READY/GO, mid-play, late play and the result.
    if (logic.stage() == core::Stage::intro)
        capture(frame, "1-start");
    if (p.ready && p.ready->decoded_frames() >= 12)
        capture(frame, "2-ready");
    const int total = p.game == core::Game::catcher ? 360 : 150;
    if (logic.stage() == core::Stage::play && logic.ticks_left() <= total / 2)
        capture(frame, "3-mid");
    if (logic.stage() == core::Stage::play && logic.ticks_left() <= total / 6)
        capture(frame, "4-late");
    if (logic.stage() == core::Stage::ending)
        capture(frame, "5-result");
}

} // namespace rich4::native::minigames
