// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace rich4::audio {

struct Statistics {
    std::uint64_t rendered_frames{};
    std::uint64_t nonzero_samples{};
    std::uint64_t effects_started{};
    std::uint64_t voices_started{};
    std::uint64_t clips_completed{};
    std::uint64_t music_frames{};
};

// One main-thread mixer. PCM and software MIDI share a single SDL3 stream, so
// SDL_AUDIODRIVER=dummy silences every output path while preserving progression.
class Audio {
  public:
    // Optional bank: SF2 or DLS supported by the installed FluidSynth. Otherwise
    // consult RICH4_SOUNDFONT, then the macOS system General MIDI bank. Never
    // copies or redistributes the platform's soundbank.
    Audio(const std::filesystem::path &asset_directory, bool enabled = true,
          const std::filesystem::path &soundbank = {});
    ~Audio();
    Audio(Audio &&) noexcept;
    Audio &operator=(Audio &&) noexcept;
    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;

    void play_sound(std::size_t effect_mkf_index);
    // Original table: SPEAKING.MKF[1050 + character * 27 + category]. The caller
    // chooses category [0,26]; audio never consumes gameplay random numbers.
    // A new voice interrupts the preceding voice, as in the original game.
    void play_voice(std::uint8_t character, std::uint16_t category);
    // Any Speaking.mkf clip (news/fortune announcer, event lines). Returns the clip length in
    // milliseconds (0 when audio is disabled) so speech bubbles can be held until it ends.
    std::uint32_t play_speech(std::size_t speaking_index);
    void stop_voice();
    [[nodiscard]] bool voice_active() const noexcept;
    void set_music(bool enabled);
    // 1..8 selects Rich08, Rich16..Rich22. Zero advances to the next track.
    // Normal completion advances through the same eight-track original playlist.
    void select_music(unsigned track);
    void update(); // Call once per application frame, including modal dialogs.

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool music_available() const noexcept;
    [[nodiscard]] bool music_enabled() const noexcept;
    [[nodiscard]] unsigned music_track() const noexcept;
    [[nodiscard]] Statistics statistics() const noexcept;
    [[nodiscard]] int queued_bytes() const;
    [[nodiscard]] const std::string &music_diagnostic() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rich4::audio
