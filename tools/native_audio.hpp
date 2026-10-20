// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rich4::audio {

struct Statistics {
    std::uint64_t rendered_frames{};
    std::uint64_t nonzero_samples{};
    std::uint64_t effects_started{};
    std::uint64_t voices_started{};
    std::uint64_t clips_completed{};
    std::uint64_t music_frames{};
    // Glitch diagnostics (also in the headless report.json as audio_*).
    std::uint64_t underruns{};         // mixing passes that ran slower than real time (device could starve)
    std::uint64_t starved_ms{};        // total time those passes overran the audio they produced
    std::uint64_t clipped_samples{};   // mixed samples beyond full scale before limiting (old: hard-clamped)
    std::uint64_t limited_samples{};   // samples whose limiter gain was below 1
    float peak{};                      // largest pre-limiter |sample|
    std::uint64_t max_update_gap_ms{}; // longest gap between two main-thread update() calls
    std::uint64_t max_render_us{};     // longest single audio-thread callback
};

// Looping-effect channels (the original keeps one sound-preset slot per purpose, e.g. the walk
// slot 0x4749d4 and the time-bomb slot 0x482362, and starts/stops each independently).
// Each channel holds at most one looping Effect.mkf clip; channels mix together.
enum class Loop : std::uint8_t {
    spinner, // slot machine / cupid wheel rattle 51/52 (tools/screens/spinners.cpp)
    walk,    // walk/engine loop 44/45/46/53, 47 on water, robot doll 38 (board preset 0x48234a)
    bomb,    // time-bomb tick 32 while its carrier walks (preset 0x482362)
    count
};

// Context MIDIs of the original (table ref_0047e793 in rich4_media_music.asm). The enumerator
// value is the original fcn_004549cf index + 1; music_file() names the file.
enum class Music : std::uint8_t {
    silence = 0,       // no music (the new-game intro stops the MIDI, rich4.asm:7236)
    menu = 1,          // MIDI01 main menu
    setup = 2,         // MIDI02 new-game setup
    bankrupt = 3,      // MIDI03 bankruptcy movie
    unused_04 = 4,     // MIDI04 has no caller in the original
    bank = 5,          // MIDI05 bank counter / loan foreclosure
    auction = 6,       // MIDI06 auction (also bankruptcy liquidation)
    shop = 7,          // MIDI07 point shop / lottery booth / victory sequence
    magic_house = 8,   // MIDI08 magic house / god-of-death summon
    lottery_draw = 9,  // MIDI09 monthly lottery draw (15th)
    bank_report = 10,  // MIDI10 monthly bank report
    gift_game = 11,    // MIDI11 喜從天降 mini-game
    balloon_game = 12, // MIDI12 balloon mini-game
    penguin_game = 13, // MIDI13 penguin mini-game
    christmas = 14,    // MIDI14-1 Christmas (stage-0 maps, 12/25)
    new_year = 15,     // MIDI14-2 Lunar New Year (Taiwan/China maps, lunar 1/1-1/3)
    prison = 16,       // MIDI15 prison visit
    hospital = 17,     // MIDI16 hospital visit
};
[[nodiscard]] std::string_view music_file(Music music) noexcept;

// PCM and software MIDI share a single SDL3 device stream, so SDL_AUDIODRIVER=dummy silences
// every output path while preserving progression. Mixing runs on SDL's audio thread (the device
// pulls exactly what it needs), so main-thread stalls cannot starve the output; a smooth limiter
// replaces hard clipping. Every method below is still main-thread only: decoding and file IO
// happen in the calling thread, and only small state swaps are shared with the audio thread.
// On Android/iOS the device pauses on SDL_EVENT_WILL_ENTER_BACKGROUND and resumes, from the same
// music position, on SDL_EVENT_DID_ENTER_FOREGROUND.
class Audio {
  public:
    // Optional General MIDI SF2 bank for TinySoundFont. Otherwise consult RICH4_SOUNDFONT,
    // then TimGM6mb.sf2 next to the program (the macOS app's Resources), then the build's copy.
    Audio(const std::filesystem::path &asset_directory, bool enabled = true,
          const std::filesystem::path &soundbank = {});
    ~Audio();
    Audio(Audio &&) noexcept;
    Audio &operator=(Audio &&) noexcept;
    Audio(const Audio &) = delete;
    Audio &operator=(const Audio &) = delete;

    void play_sound(std::size_t effect_mkf_index);
    // Looping effect on one channel (the original's looped sound presets). Replaces that
    // channel's previous loop; asking for the clip already looping there keeps it going
    // seamlessly. Other channels are untouched. Logical state is kept even when disabled.
    void play_loop(std::size_t effect_mkf_index, Loop channel = Loop::spinner);
    void stop_loop(Loop channel = Loop::spinner);
    void stop_all_loops();
    [[nodiscard]] std::optional<std::size_t> looping(Loop channel) const noexcept;
    // Original table: SPEAKING.MKF[1050 + character * 27 + category]. The caller
    // chooses category [0,26]; audio never consumes gameplay random numbers.
    // A new voice interrupts the preceding voice, as in the original game.
    void play_voice(std::uint8_t character, std::uint16_t category);
    // Any Speaking.mkf clip (news/fortune announcer, event lines). Returns the clip length in
    // milliseconds (0 when audio is disabled) so speech bubbles can be held until it ends.
    std::uint32_t play_speech(std::size_t speaking_index);
    // Speaks after the current voice (and earlier queued clips) end, e.g. the auction
    // announcer's 131 then 132. play_voice/play_speech/stop_voice drop the queue.
    void queue_speech(std::size_t speaking_index);
    void stop_voice();
    [[nodiscard]] bool voice_active() const noexcept;
    void set_music(bool enabled);
    // 1..8 selects Rich08, Rich16..Rich22. Zero advances to the next track.
    // Normal completion advances through the same eight-track original playlist.
    // Selecting a playlist track drops every pushed context (it is an explicit choice).
    void select_music(unsigned track);

    // ---- Context music (rich4_media_music.asm) ----
    // push_music (fcn_004549cf): remember what is playing now (board playlist track or an
    // outer context) together with its position, then loop `music` until popped.
    // pop_music (fcn_00454bcc): drop the top context and resume what push_music saved, from
    // the saved position. No-op when nothing is pushed. Each push needs exactly one pop.
    // replace_music (the original's 0x8000 flag): swap the top context without saving it;
    // with nothing pushed it behaves like push_music.
    // All three respect set_music(false): the stack is kept but nothing sounds, and the right
    // track resumes when music is switched back on. Usable while audio is disabled (headless
    // logic only). Typical screen use:
    //     audio.push_music(rich4::audio::Music::lottery_draw);   // screen opens
    //     ...                                                    // screen runs
    //     audio.pop_music();                                     // screen closes
    void push_music(Music music);
    void pop_music();
    void replace_music(Music music);
    [[nodiscard]] std::size_t music_depth() const noexcept;   // number of pushed contexts
    [[nodiscard]] std::optional<Music> music_context() const noexcept; // top context, if any
    [[nodiscard]] std::string music_name() const;             // e.g. "rich08.mid", "midi05.mid", "silence"

    // Optional trace of every music/loop/speech change ("music", "loop", "speech", ...), for
    // the headless audio log. Called synchronously on the main thread.
    using Trace = std::function<void(std::string_view what, std::string_view detail)>;
    void set_trace(Trace trace);
    // Call once per application frame, including modal dialogs: delivers trace lines produced on
    // the audio thread (playlist advance, queued speech) and records main-thread gaps. Audio keeps
    // playing without it.
    void update();

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
