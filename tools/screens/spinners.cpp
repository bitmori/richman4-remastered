// SPDX-License-Identifier: GPL-3.0-or-later
// Layout, timing, texts and sounds from the original asm (rich4.asm):
//   fcn_00440706  slot machine setup: box Data 517:5 at (220,140) + text mode 4, machine
//                 Panel 67:v opaque at (220,320) (v = 1 for the 3-reel 小財神/小窮神, 0 otherwise),
//                 lever 67:2 at (lever_x[v],240), loops sound preset 0x475d3c (Effect 51).
//   fcn_0043f23e  30 ms frames; reels Panel 67:(4+pos) opaque at (reel_x[v][r],320); STOP pulls the
//                 lever (67:3, Effect 1), phase 3 frame 4 releases it; after the last reel the
//                 rattle stops and the box shows "%d元" for 40 frames.
//   fcn_0044090e  cupid wheel setup: box + text, wheel Panel (68+type):2 at (220,320), cupid
//                 (68+type):0 at (265,230), loops preset 0x475d4c (Effect 52).
//   fcn_0043f7c6  36 ms frames, one sector per frame; STOP (or frame 40 for computers) shoots the
//                 arrow (cupid frame 1, Effect 1); slows down 3 sectors each at periods 1..4 frames,
//                 then stops on the first non-empty sector (table 0x475d0c); holds 40 frames
//                 (a human click ends the hold).
#include "spinners.hpp"

#include "../native_stock_names.hpp"
#include "messages.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace rich4::native::spinners {
namespace {

constexpr auto near_black = ui::color(16, 16, 16);
constexpr auto near_white = ui::color(240, 240, 240);
// create_font(0x10, 0xf0f0f0, 0x101010, 3, 1) in both spinner setups: the message-box font.
constexpr ui::TextStyle box_style{16, near_white, true, true, false, near_black};

constexpr unsigned machine_frame_ms = 30; // fcn_0043f23e: cmp ebx, 0x1e
constexpr unsigned wheel_frame_ms = 36;   // fcn_0043f7c6: cmp esi, 0x24
constexpr std::size_t slot_rattle = 51, wheel_rattle = 52, stop_sound = 1; // presets 0x33, 0x34, 0x482322

constexpr std::array<int, 2> lever_x{317, 298};                              // ref_00475ce0
constexpr std::array<std::array<int, 4>, 2> reel_x{{{145, 182, 219, 256},    // ref_00475ce8
                                                    {0, 163, 200, 237}}};
// ref_00475d0c: value of each of the 12 wheel sectors; -1 (0xff) = empty sector.
constexpr int wheel_values[4][12] = {{1, -1, 0, -1, 1, -1, 2, -1, 3, -1, 2, -1},
                                     {-1, -1, 1, -1, -1, 4, -1, 3, -1, -1, 2, -1},
                                     {1, -1, 6, -1, 5, -1, 4, -1, 3, -1, 2, -1},
                                     {5, -1, 3, -1, 30, -1, 20, -1, 15, -1, 10, -1}};

// _rich4_god_names (0x47ed7a) and the slot-machine prompts (0x4652a9.., indexed by kind - 1).
std::string_view god_name(unsigned kind) {
    static constexpr std::array<std::string_view, 6> names{"小財神", "大財神", "小福神",
                                                           "大福神", "小窮神", "大窮神"};
    return kind >= 1 && kind <= 6 ? names[kind - 1] : std::string_view{};
}
std::string_view machine_prompt(unsigned kind) {
    switch (kind) {
    case 1:
        return "附身\n\n向所有對手收...";
    case 2:
        return "附身\n\n送您...";
    case 5:
        return "附身\n\n付給每個人...";
    default:
        return "附身\n\n損失...";
    }
}
std::string strip_spaces(std::string_view text) {
    std::string result;
    for (const char c : text)
        if (c != ' ')
            result += c;
    return result;
}
// Same company naming as the toll boxes (messages.cpp): the commercial's stock name.
std::string company_name(const World &world, std::int32_t company) {
    if (company <= 0 || std::size_t(company) >= world.commercials.size())
        return "";
    const auto index = world.commercials[std::size_t(company)].stock_index;
    const auto offset = std::size_t(world.header.stage_id * 4 + world.header.map_id) * 12;
    if (index >= 12 || offset + index >= native_stock_names.size())
        return "";
    return strip_spaces(native_stock_names[offset + index]);
}
// ref_00475cf8 texts (0x465210..), %s = company (travel/insurance) or owner name (hotel/mall).
std::string wheel_text(const World &world, const SpinnerState &state) {
    std::string name;
    if (state.type == 1 || state.type == 2) {
        if (state.subject >= 0 && std::size_t(state.subject) < world.players.size())
            name = names.at(world.players[std::size_t(state.subject)].state.character % 12);
    } else
        name = company_name(world, state.subject);
    switch (state.type) {
    case 0:
        return name + "\n\n送您出國旅遊...";
    case 1:
        return name + "的旅館\n\n請進來休息...";
    case 2:
        return name + "的購物中心\n\n您的消費倍數為...";
    default:
        return name + "\n\n您的投保天數為...";
    }
}

unsigned variant(const SpinnerState &state) {
    return ((state.type - 1U) & 1U) ^ 1U; // fcn_00440706: (arg & 1) ^ 1 with arg = kind - 1
}
std::int32_t machine_amount(const SpinnerState &state) {
    const auto &d = state.digits; // fcn_0043f23e loc_0043f5dd
    std::int32_t amount = d[1] / 2 * 100 + d[2] / 2 * 10 + d[3] / 2;
    if (variant(state) == 0)
        amount += d[0] / 2 * 1000;
    return amount;
}

void draw_box(Frame &frame, std::string_view text) {
    graphics::blit(frame.canvas, frame.artwork.frame('d', 517, 5), 220, 140);
    messages::draw_text(frame, text, 220, 140, 4, box_style);
}

void draw_machine(Frame &frame, const SpinnerState &state) {
    const auto v = variant(state);
    draw_box(frame, state.phase >= 9 ? std::to_string(machine_amount(state)) + "元"
                                     : std::string(god_name(state.type)) + std::string(machine_prompt(state.type)));
    graphics::blit(frame.canvas, frame.artwork.frame('p', 67, v), 220, 320, graphics::Transparency::opaque);
    const bool pulled = state.phase == 2 || state.phase == 3;
    graphics::blit(frame.canvas, frame.artwork.frame('p', 67, pulled ? 3 : 2), lever_x[v], 240);
    if (state.phase == 0)
        return; // not spun yet: the machine art shows its painted 6666 / 666
    for (unsigned reel = v; reel < 4; ++reel)
        graphics::blit(frame.canvas, frame.artwork.frame('p', 67, 4U + state.digits[reel] % 20U), reel_x[v][reel],
                       320, graphics::Transparency::opaque);
}

void draw_wheel(Frame &frame, const World &world, const SpinnerState &state, bool shot) {
    const std::size_t entry = 68U + (state.type & 3U);
    draw_box(frame, wheel_text(world, state));
    const auto &wheel = frame.artwork.frame('p', entry, 2U + state.cursor % 12U);
    if (!shot) { // spinning: the wheel is redrawn over the idle cupid every frame
        graphics::blit(frame.canvas, frame.artwork.frame('p', entry, 0), 265, 230);
        graphics::blit(frame.canvas, wheel, 220, 320);
    } else { // after STOP: the cupid has shot its arrow and stays on top
        graphics::blit(frame.canvas, wheel, 220, 320);
        graphics::blit(frame.canvas, frame.artwork.frame('p', entry, 1), 265, 230);
    }
}

} // namespace

bool handles(const GameDecision &decision) {
    return decision.spinner &&
           (decision.kind == DecisionKind::money_spinner || decision.kind == DecisionKind::roulette);
}
bool handles(const EffectEvent &effect) {
    return effect.spinner && (effect.kind == EffectKind::money_spinner || effect.kind == EffectKind::roulette);
}

void Spinners::draw_decision(Frame &frame, const World &world, const GameDecision &decision, int stop_id,
                             const std::function<void()> &stop) {
    const auto &state = *decision.spinner;
    if (decision.kind == DecisionKind::money_spinner) {
        if (decision_phase_ <= 1 && state.phase >= 2 && state.phase < 9 && sound_.play)
            sound_.play(stop_sound); // the lever pull (fcn_0043f23e state 2)
        decision_phase_ = state.phase;
        draw_machine(frame, state);
        if (state.phase <= 8)
            wanted_loop_ = slot_rattle;
    } else {
        draw_wheel(frame, world, state, false);
        wanted_loop_ = wheel_rattle;
    }
    // Any click/key stops the original spinner; the whole screen is the (finger-sized) target.
    // click_sound false: the stop plays the original Effect 1, not the generic UI click.
    frame.buttons.push_back({stop_id, {0, 0, 640, 480}, true, stop, false});
}

void Spinners::enqueue(EffectEvent effect) {
    if (skip_ || !effect.spinner)
        return;
    queue_.push_back(std::move(effect));
}

void Spinners::clear() {
    queue_.clear();
    current_.reset();
}

// One fcn_0043f7c6 loop iteration. Returns false once the wheel is finished.
bool Spinners::step_wheel(Playing &p) {
    ++p.frames;
    if (p.wheel_state == 1 && p.frames >= 40)
        p.wheel_state = 2; // computers stop themselves at frame 40
    switch (p.wheel_state) {
    case 1:
        p.state.cursor = static_cast<std::uint8_t>((p.state.cursor + 1) % 12);
        break;
    case 2:
        if (sound_.play)
            sound_.play(stop_sound);
        p.wheel_state = 3;
        p.count = 3;
        p.period = 1;
        p.last = p.frames;
        break;
    case 3:
        if (p.frames - p.last != p.period)
            break;
        p.state.cursor = static_cast<std::uint8_t>((p.state.cursor + 1) % 12);
        p.last = p.frames;
        if (--p.count == 0) {
            p.count = 3;
            ++p.period;
        }
        if (p.period >= 5 && wheel_values[p.state.type & 3U][p.state.cursor % 12U] >= 0) {
            p.wheel_state = 5;
            p.frames = 0;
        }
        break;
    case 5:
        if (p.frames >= 40 || p.skip)
            return false;
        break;
    default:
        return false;
    }
    return true;
}

void Spinners::draw_effect(Frame &frame, const World &world, unsigned elapsed_ms, int skip_id) {
    if (!current_) {
        if (queue_.empty())
            return;
        Playing playing;
        playing.event = std::move(queue_.front());
        queue_.pop_front();
        playing.state = *playing.event.spinner;
        if (playing.event.kind == EffectKind::money_spinner) {
            playing.machine.emplace(playing.state.type);
            playing.random.seed(playing.state.seed);
            playing.state.phase = 0;
        } else
            playing.wheel_state = playing.state.phase == 2 ? 2 : 1;
        current_ = std::move(playing);
    }
    auto &p = *current_;
    p.elapsed += std::min(elapsed_ms, 100U);
    bool running = true;
    if (p.machine) {
        // Same frames as the computer branch of fcn_0043f23e, replayed from the rule's random
        // state on a private copy (the rule already consumed those draws).
        while (running && p.elapsed >= machine_frame_ms) {
            p.elapsed -= machine_frame_ms;
            const auto before = p.machine->phase();
            p.machine->tick(p.random, true);
            if (before == 1 && p.machine->phase() >= 2 && sound_.play)
                sound_.play(stop_sound);
            running = !p.machine->finished();
        }
        p.state.digits = p.machine->digits();
        p.state.phase = static_cast<std::uint8_t>(p.machine->phase());
    } else
        while (running && p.elapsed >= wheel_frame_ms) {
            p.elapsed -= wheel_frame_ms;
            running = step_wheel(p);
        }
    if (!running) {
        current_.reset();
        return;
    }
    frame.buttons.clear(); // the original blocks the game while the spinner runs
    if (p.machine) {
        draw_machine(frame, p.state);
        if (p.state.phase <= 8)
            wanted_loop_ = slot_rattle;
    } else {
        draw_wheel(frame, world, p.state, p.wheel_state >= 3);
        if (p.wheel_state < 5)
            wanted_loop_ = wheel_rattle;
        else if (p.event.spinner->phase == 2) // human-stopped wheel: a click ends the result hold
            frame.buttons.push_back({skip_id, {0, 0, 640, 480}, true, [this] {
                                         if (current_)
                                             current_->skip = true;
                                     },
                                     false});
    }
}

void Spinners::finish_frame() {
    if (wanted_loop_ != playing_loop_) {
        if (wanted_loop_ && sound_.loop)
            sound_.loop(wanted_loop_);
        else if (!wanted_loop_ && sound_.stop_loop)
            sound_.stop_loop();
        playing_loop_ = wanted_loop_;
    }
    wanted_loop_ = 0;
}

} // namespace rich4::native::spinners
