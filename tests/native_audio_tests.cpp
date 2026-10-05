// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_audio.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc > 3) {
        std::cerr
            << "usage: native_audio_tests [original-asset-directory [soundbank.sf2-or-dls]]\n";
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
        if (argc == 1) {
            std::cout
                << "disabled audio lifecycle passed; supply original assets for PCM/MIDI tests\n";
            return 0;
        }
        rich4::audio::Audio mixer(argv[1], true, argc == 3 ? argv[2] : "");
        if (!mixer.enabled() || !mixer.music_available()) {
            throw std::runtime_error("native MIDI bank failed: " + mixer.music_diagnostic());
        }
        if (std::string(SDL_GetCurrentAudioDriver()) != "dummy") {
            throw std::runtime_error("audio driver is not dummy");
        }
        const auto start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 3000) {
            mixer.update();
            SDL_Delay(5);
        }
        auto midi = mixer.statistics();
        std::cout << "initial MIDI frames=" << midi.music_frames
                  << " nonzero=" << midi.nonzero_samples << std::endl;
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
        SDL_Delay(200); // no refill: the dummy device must actually drain the queued samples
        if (mixer.queued_bytes() != 0) {
            throw std::runtime_error("dummy audio queue did not drain");
        }
        mixer.select_music(8);
        mixer.select_music(0);
        if (mixer.music_track() != 1) {
            throw std::runtime_error("playlist did not wrap");
        }
        mixer.set_music(true);
        mixer.update();
        bool invalid = false;
        try {
            mixer.play_voice(12, 0);
        } catch (const std::invalid_argument&) {
            invalid = true;
        }
        if (!invalid) {
            throw std::runtime_error("invalid character accepted");
        }
        std::cout << "driver=" << SDL_GetCurrentAudioDriver()
                  << " rendered_frames=" << stats.rendered_frames
                  << " nonzero_samples=" << stats.nonzero_samples
                  << " music_frames=" << stats.music_frames << " effects=" << stats.effects_started
                  << " voices=" << stats.voices_started << " completed=" << stats.clips_completed
                  << " drained=yes playlist_wrap=yes\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
