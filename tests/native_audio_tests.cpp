// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_limiter.hpp"
#include "board_audio.hpp"
#include "native_audio.hpp"
#include "screens/voices.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using rich4::audio::Audio;
using rich4::audio::Loop;
using rich4::audio::Music;

void expect(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Loop channels and the context-music stack are pure bookkeeping, identical with or without a
// device, so they are checked on every build (ctest runs without original assets).
void check_logic(Audio &audio) {
    std::vector<std::string> trace;
    audio.set_trace([&trace](std::string_view what, std::string_view detail) {
        trace.push_back(std::string(what) + " " + std::string(detail));
    });
    // Keyed loops: the walk loop coexists with the spinner rattle; same clip = no restart.
    audio.play_loop(51);
    audio.play_loop(44, Loop::walk);
    audio.play_loop(44, Loop::walk);
    audio.play_loop(32, Loop::bomb);
    expect(audio.looping(Loop::spinner) == 51 && audio.looping(Loop::walk) == 44 && audio.looping(Loop::bomb) == 32,
           "loop channels are not independent");
    audio.play_loop(47, Loop::walk); // boarding water swaps the walk clip only
    audio.stop_loop();               // spinner (default channel) stops, walk keeps going
    expect(!audio.looping(Loop::spinner) && audio.looping(Loop::walk) == 47, "stop_loop touched another channel");
    audio.stop_all_loops();
    expect(!audio.looping(Loop::walk) && !audio.looping(Loop::bomb), "stop_all_loops left a loop");
    // Context stack: push saves the playlist, nested contexts pop in order, extra pops are no-ops.
    expect(audio.music_depth() == 0 && audio.music_name() == "rich08.mid", "unexpected initial music");
    audio.push_music(Music::menu);
    audio.replace_music(Music::setup); // 0x8000: swaps without saving MIDI01
    expect(audio.music_depth() == 1 && audio.music_name() == "midi02.mid", "replace_music pushed");
    audio.pop_music();
    expect(audio.music_depth() == 0 && audio.music_name() == "rich08.mid" && !audio.music_context(),
           "pop did not resume the playlist");
    audio.push_music(Music::bankrupt);
    audio.push_music(Music::auction); // liquidation auction over the bankruptcy music
    expect(audio.music_depth() == 2 && audio.music_context() == Music::auction, "nested push failed");
    audio.pop_music();
    expect(audio.music_context() == Music::bankrupt && audio.music_name() == "midi03.mid", "nested pop failed");
    audio.pop_music();
    audio.pop_music(); // unbalanced pop: harmless
    expect(audio.music_depth() == 0 && audio.music_name() == "rich08.mid", "unbalanced pop changed music");
    audio.push_music(Music::prison);
    audio.select_music(3); // an explicit Options song pick drops every context
    expect(audio.music_depth() == 0 && audio.music_name() == "rich17.mid", "select_music kept contexts");
    audio.select_music(1);
    expect(rich4::audio::music_file(Music::christmas) == "midi14-1.mid" &&
               rich4::audio::music_file(Music::hospital) == "midi16.mid",
           "context file table");
    const std::vector<std::string> expected{
        "loop_start spinner 51", "loop_start walk 44",    "loop_start bomb 32",    "loop_stop walk 44",
        "loop_start walk 47",    "loop_stop spinner 51",  "loop_stop walk 47",     "loop_stop bomb 32",
        "music_push midi01.mid", "music_replace midi02.mid", "music_pop rich08.mid", "music_push midi03.mid",
        "music_push midi06.mid", "music_pop midi03.mid",  "music_pop rich08.mid",  "music_push midi15.mid",
        "music_select rich17.mid", "music_select rich08.mid"};
    expect(trace == expected, "audio trace sequence differs");
    audio.set_trace({});
}

// The app-side director: keeps Audio's stack equal to the wanted contexts with balanced
// push/pop, swaps menu -> setup with replace_music, and holds pops briefly.
void check_director(Audio &audio) {
    rich4::native::MusicDirector director;
    std::uint64_t now = 1000;
    director.update(audio, {Music::menu}, now);
    director.update(audio, {Music::setup}, now += 16);
    expect(audio.music_depth() == 1 && audio.music_context() == Music::setup, "menu -> setup should replace");
    audio.select_music(1); // a new game restarts the playlist and clears the stack
    director.forget();
    director.update(audio, {}, now += 16);
    director.update(audio, {Music::christmas}, now += 16);
    director.update(audio, {Music::christmas, Music::bank}, now += 16);
    expect(audio.music_depth() == 2 && audio.music_context() == Music::bank, "bank over the holiday music");
    director.update(audio, {Music::christmas}, now += 16); // bank answered...
    expect(audio.music_context() == Music::bank, "pop must be held");
    director.update(audio, {Music::christmas, Music::bank}, now += 16); // ...next bank decision
    director.update(audio, {Music::christmas}, now += 16);
    director.update(audio, {Music::christmas}, now += rich4::native::MusicDirector::pop_hold_ms);
    expect(audio.music_depth() == 1 && audio.music_context() == Music::christmas, "held pop not applied");
    director.update(audio, {}, now += 16);
    director.update(audio, {}, now += rich4::native::MusicDirector::pop_hold_ms);
    expect(audio.music_depth() == 0 && !audio.music_context(), "director left a context pushed");
    // Walk-loop selection by traffic method / water / special slot, and the bomb tick.
    rich4::World world;
    world.players.resize(1);
    world.nodes.resize(3);
    world.nodes[2].flags = 0x80000000U;
    world.players[0].state.node_id = 1;
    for (std::uint8_t traffic = 0; traffic < 4; ++traffic) {
        world.players[0].state.traffic_method = traffic;
        expect(rich4::native::walk_loop_clip(world, 0) == std::array<std::size_t, 4>{44, 45, 46, 53}[traffic],
               "walk loop by traffic method");
    }
    world.players[0].state.node_id = 2;
    expect(rich4::native::walk_loop_clip(world, 0) == 47, "water loop");
    expect(!rich4::native::bomb_loop_clip(world, 0), "bomb tick without a bomb");
    world.players[0].status.attached_bomb = 3;
    expect(rich4::native::bomb_loop_clip(world, 0) == 32, "bomb tick");
    expect(rich4::native::walk_loop_clip(world, 8) == 38 && rich4::native::walk_loop_clip(world, 5) == 44,
           "robot doll / special actor loop");
    rich4::native::WalkSound walk;
    walk.update(audio, 44, 32, 5000);
    walk.update(audio, std::nullopt, std::nullopt, 5016); // between two steps: keep looping
    expect(audio.looping(Loop::walk) == 44 && audio.looping(Loop::bomb) == 32, "walk loop stopped between steps");
    walk.update(audio, std::nullopt, std::nullopt, 5000 + rich4::native::WalkSound::stop_after_ms);
    expect(!audio.looping(Loop::walk) && !audio.looping(Loop::bomb), "walk loop kept after arrival");
    // Clerk lines map to the original #NNNN clips (either tilde spelling).
    using rich4::native::voices::clerk_voice;
    expect(clerk_voice("歡迎光臨\n大富翁銀行！") == 75 && clerk_voice("董事長您別開玩笑了∼") == 90 &&
               clerk_voice("請圈選您的\n幸運號碼～") == 13 && clerk_voice("有什麼我能\n為你服務的嗎？") == 0 &&
               !clerk_voice("賣出換地卡"),
           "clerk voice table");
    expect(rich4::native::voices::rescued_npc_voice(6) == 123 && rich4::native::voices::auction_buyer_voice(10) == 146,
           "rescue / auction clip ids");
}

// The output limiter: nothing ever exceeds the ceiling, quiet material passes bit-exact, and the
// gain glides back to unity after a burst instead of staying ducked.
void check_limiter() {
    using rich4::audio::Limiter;
    constexpr float rate = 48000.0F;
    Limiter quiet(rate);
    std::vector<float> below(4800 * 2);
    for (std::size_t i = 0; i < below.size(); ++i)
        below[i] = 0.89F * std::sin(float(i) * 0.031F) * (i % 2 ? -1.0F : 1.0F);
    auto copy = below;
    expect(quiet.process(copy) == 0 && copy == below, "limiter altered a signal below its ceiling");

    Limiter loud(rate);
    std::vector<float> overlap(48000 * 2); // music + voice + three effects: peaks near 4x full scale
    std::uint32_t noise = 12345;
    for (std::size_t i = 0; i < overlap.size(); ++i) {
        noise = noise * 1664525U + 1013904223U;
        const float hiss = (float(noise >> 8) / float(1U << 24) - 0.5F) * 0.6F;
        overlap[i] = 3.2F * std::sin(float(i / 2) * 0.05F) + hiss;
    }
    overlap[1001] = 40.0F; // a single absurd spike
    overlap[2002] = std::nanf("");
    const auto limited = loud.process(overlap);
    expect(limited > 0, "limiter never engaged on an overloaded mix");
    for (const float sample : overlap)
        expect(std::isfinite(sample) && std::abs(sample) <= loud.ceiling(), "limiter let a sample over the ceiling");
    // Shape is kept (no squared-off waveform): a lone burst over a quiet tone ducks, then recovers.
    Limiter recover(rate);
    std::vector<float> tone(48000 * 2);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] = (i < 960 ? 2.0F : 0.25F) * std::sin(float(i / 2) * 0.02F);
    recover.process(tone);
    expect(recover.gain() == 1.0F, "limiter gain did not return to unity within one second");
    float late_peak = 0.0F;
    for (std::size_t i = 48000; i < tone.size(); ++i)
        late_peak = std::max(late_peak, std::abs(tone[i]));
    expect(late_peak > 0.249F, "limiter still attenuating long after the burst");
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 3) {
        std::cerr << "usage: native_audio_tests [original-asset-directory [soundbank.sf2]]\n";
        return 1;
    }
    // Force the device before constructing the mixer, even if the parent shell
    // selected a hardware driver. This test must never emit audible output.
    if (!SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE)) {
        std::cerr << "cannot force SDL dummy audio driver\n";
        return 1;
    }
    try {
        rich4::audio::Audio disabled("nonexistent", false);
        disabled.play_sound(0);
        disabled.play_voice(0, 0);
        disabled.update();
        if (disabled.enabled() || disabled.queued_bytes()) {
            throw std::runtime_error("disabled mode touched audio");
        }
        check_logic(disabled);
        check_director(disabled);
        check_limiter();
        if (argc == 1) {
            std::cout << "disabled audio lifecycle passed; supply original assets for PCM/MIDI tests\n";
            return 0;
        }
        rich4::audio::Audio mixer(argv[1], true, argc == 3 ? argv[2] : "");
        if (!mixer.enabled() || !mixer.music_available()) {
            throw std::runtime_error("native MIDI bank failed: " + mixer.music_diagnostic());
        }
        if (std::string(SDL_GetCurrentAudioDriver()) != "dummy") {
            throw std::runtime_error("audio driver is not dummy");
        }
        // Callback mode: the device thread mixes on its own. The main thread never calls update()
        // here (as during an archive open or a long frame) and the music still advances in real time.
        const auto start = SDL_GetTicks();
        SDL_Delay(1500);
        const auto unattended = mixer.statistics();
        std::cout << "callback mode: " << unattended.rendered_frames << " frames mixed without update()" << std::endl;
        expect(unattended.rendered_frames >= 48000, "audio thread did not mix while the main thread was busy");
        while (SDL_GetTicks() - start < 3000) {
            mixer.update();
            SDL_Delay(5);
        }
        auto midi = mixer.statistics();
        std::cout << "initial MIDI frames=" << midi.music_frames << " nonzero=" << midi.nonzero_samples
                  << std::endl;
        if (midi.nonzero_samples == 0 || midi.music_frames < 24000) {
            throw std::runtime_error("MIDI did not produce PCM");
        }
        mixer.set_music(false);
        mixer.play_sound(10);
        mixer.play_voice(10, 0);
        mixer.play_voice(10, 9); // newest speech replaces old speech
        const auto pcm_start = SDL_GetTicks();
        while (SDL_GetTicks() - pcm_start < 30000 && mixer.statistics().clips_completed < 2) {
            mixer.update();
            SDL_Delay(5);
        }
        const auto stats = mixer.statistics();
        if (stats.music_frames != midi.music_frames) {
            throw std::runtime_error("muted music sample clock advanced");
        }
        if (stats.effects_started != 1 || stats.voices_started != 2 || stats.clips_completed != 2) {
            throw std::runtime_error("PCM completion/replacement failed");
        }
        SDL_Delay(200); // the device pulls what it needs: at most one mixing block stays queued
        if (mixer.queued_bytes() >= 512 * 2 * int(sizeof(float))) {
            throw std::runtime_error("dummy audio queue holds more than one block");
        }
        // The limiter keeps the mixed output within full scale, whatever overlaps.
        mixer.play_sound(10);
        mixer.play_sound(10);
        mixer.play_sound(10);
        mixer.play_voice(10, 0);
        SDL_Delay(400);
        const auto loud = mixer.statistics();
        std::cout << "overlap: peak(pre-limiter)=" << loud.peak << " clipped=" << loud.clipped_samples
                  << " limited=" << loud.limited_samples << std::endl;
        expect(loud.limited_samples > 0, "stacked effects never reached the limiter");
        mixer.stop_voice();
        mixer.select_music(8);
        mixer.select_music(0);
        if (mixer.music_track() != 1) {
            throw std::runtime_error("playlist did not wrap");
        }
        mixer.set_music(true);
        mixer.update();
        // Real MIDI/PCM: a looped context renders, two loop channels mix, popping resumes the
        // playlist, queued speech plays back to back.
        check_logic(mixer);
        const auto run = [&mixer](Uint64 ms) {
            const auto begin = SDL_GetTicks();
            while (SDL_GetTicks() - begin < ms) {
                mixer.update();
                SDL_Delay(5);
            }
        };
        const auto before_context = mixer.statistics();
        mixer.push_music(Music::bank);
        mixer.play_loop(44, Loop::walk);
        mixer.play_loop(51);
        run(1200);
        const auto in_context = mixer.statistics();
        expect(in_context.music_frames > before_context.music_frames + 24000, "context MIDI did not render");
        expect(in_context.effects_started == before_context.effects_started + 2, "two loops did not both start");
        mixer.stop_all_loops();
        mixer.pop_music();
        expect(mixer.music_name() == "rich08.mid", "context pop did not resume the playlist");
        mixer.set_music(false); // muted push/pop keeps the stack but renders nothing
        mixer.push_music(Music::shop);
        run(200);
        mixer.pop_music();
        mixer.set_music(true);
        const auto voices = mixer.statistics().voices_started;
        mixer.queue_speech(131);
        mixer.queue_speech(132);
        const auto speech_start = SDL_GetTicks();
        while (SDL_GetTicks() - speech_start < 15000 && mixer.statistics().voices_started < voices + 2) {
            mixer.update();
            SDL_Delay(5);
        }
        expect(mixer.statistics().voices_started == voices + 2, "queued speech did not play in turn");
        mixer.stop_voice();
        bool invalid = false;
        try {
            mixer.play_voice(12, 0);
        } catch (const std::invalid_argument &) {
            invalid = true;
        }
        if (!invalid) {
            throw std::runtime_error("invalid character accepted");
        }
        std::cout << "driver=" << SDL_GetCurrentAudioDriver() << " rendered_frames=" << stats.rendered_frames
                  << " nonzero_samples=" << stats.nonzero_samples << " music_frames=" << stats.music_frames
                  << " effects=" << stats.effects_started << " voices=" << stats.voices_started
                  << " completed=" << stats.clips_completed << " drained=yes playlist_wrap=yes\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
