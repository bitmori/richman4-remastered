// SPDX-License-Identifier: GPL-3.0-or-later
// Resource selection follows Iru Cai's Rich4 reverse engineering:
// rich4_sound_effect.asm, rich4_media_music.asm, rich4.asm event strings.
#include "native_audio.hpp"

#include "audio_limiter.hpp"

#include "rich4/assets.hpp"

#include <SDL3/SDL.h>
// TinySoundFont (third_party/TinySoundFont): SF2 synthesizer and MIDI loader, implemented here.
#define TSF_IMPLEMENTATION
#include <tsf.h>
#define TML_IMPLEMENTATION
#include <tml.h>
// stb_vorbis (third_party/stb): decodes the CD audio tracks of the Steam release, implemented here.
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include <stb_vorbis.c>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace rich4::audio {
namespace {

constexpr int sample_rate = 48000;
constexpr int channels = 2;
constexpr int block_frames = 512;
constexpr int event_frames = 64; // MIDI events are applied at this granularity inside a block
constexpr int bytes_per_frame = channels * int(sizeof(float));
// Mix levels. Every Effect.mkf / Speaking.mkf clip is normalised to ~0 dBFS (median peak 0.83 /
// 0.99, RMS about -16 dBFS) while the General MIDI render at the old synth gain 0.4 peaks around
// 0.5-0.66 (RMS about -26 dBFS). Summed at unity a voice over music or an effect over a voice
// clipped constantly; these gains keep a single clip plus music under the limiter ceiling and
// keep music within ~1 dB of its old balance against effects and voices.
constexpr float clip_gain = 0.6F;   // effects and voices (-4.4 dB)
constexpr float music_gain_db = -12.5F; // TinySoundFont gain: matches the old FluidSynth + macOS DLS music level
// The CD tracks are mastered near full scale (RMS about -16.5 dBFS); -10 dB brings them to the
// MIDI render's level.
constexpr float cd_gain = 0.316F;
constexpr std::size_t vorbis_arena_bytes = 512 * 1024; // decoder state: the audio thread never allocates
constexpr std::string_view bundled_soundfont = "TimGM6mb.sf2";
constexpr SDL_AudioSpec mixer_format{SDL_AUDIO_F32, channels, sample_rate};
constexpr std::array<std::string_view, 8> music_files{"rich08.mid", "rich16.mid", "rich17.mid", "rich18.mid",
                                                      "rich19.mid", "rich20.mid", "rich21.mid", "rich22.mid"};
// ref_0047e793, indexed by Music (silence first).
constexpr std::array<std::string_view, 18> context_files{
    "silence",    "midi01.mid", "midi02.mid", "midi03.mid",   "midi04.mid",   "midi05.mid",
    "midi06.mid", "midi07.mid", "midi08.mid", "midi09.mid",   "midi10.mid",   "midi11.mid",
    "midi12.mid", "midi13.mid", "midi14-1.mid", "midi14-2.mid", "midi15.mid", "midi16.mid"};
constexpr std::array<std::string_view, std::size_t(Loop::count)> loop_names{"spinner", "walk", "bomb"};

// Steam ships the original CD audio as Media/Music/trackNN.ogg (track01 is the data track).
// Midi.txt lists the same 25 songs in CD order -- RICH08, RICH16-22, MIDI01-16 -- so the song
// on line N is trackN+1; tracklen.nfo's lengths agree (long playlist songs, short contexts).
std::string cd_track_file(std::string_view midi) {
    unsigned track = 2;
    const auto find = [&](std::span<const std::string_view> files) {
        for (const auto file : files) {
            if (file == midi)
                return true;
            ++track;
        }
        return false;
    };
    if (!find(music_files))
        find(std::span(context_files).subspan(1)); // skip "silence"
    char name[16];
    std::snprintf(name, sizeof(name), "track%02u.ogg", track);
    return name;
}

std::optional<std::filesystem::path> find_asset(const std::filesystem::path &directory, std::string_view name) {
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        auto candidate = entry.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char value) { return char(std::tolower(value)); });
        if (candidate == name && entry.is_regular_file()) {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::filesystem::path asset_path(const std::filesystem::path &directory, std::string_view name) {
    if (auto found = find_asset(directory, name))
        return *found;
    throw assets::AssetError("missing audio asset: " + (directory / name).string());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path, std::streamoff limit = 16 * 1024 * 1024) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open music: " + path.string());
    }
    const auto length = input.tellg();
    if (length < 0 || length > limit) {
        throw std::runtime_error("invalid music size: " + path.string());
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(result.data()), length)) {
        throw std::runtime_error("truncated music: " + path.string());
    }
    return result;
}

std::runtime_error sdl_error(std::string_view operation) {
    return std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}

struct AudioSubsystem {
    AudioSubsystem() {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            throw sdl_error("initialize SDL audio");
        }
    }
    ~AudioSubsystem() {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

struct TsfClose {
    void operator()(tsf *synth) const {
        tsf_close(synth);
    }
};
struct TmlFree {
    void operator()(tml_message *song) const {
        tml_free(song);
    }
};
struct VorbisClose {
    void operator()(stb_vorbis *decoder) const {
        stb_vorbis_close(decoder);
    }
};

struct SdlFree {
    void operator()(void *pointer) const {
        SDL_free(pointer);
    }
};

std::vector<float> decode_wave(std::span<const std::uint8_t> bytes) {
    auto *input = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!input) {
        throw sdl_error("open WAV memory");
    }
    SDL_AudioSpec source_format{};
    Uint8 *wav_pointer = nullptr;
    Uint32 wav_length = 0;
    if (!SDL_LoadWAV_IO(input, true, &source_format, &wav_pointer, &wav_length)) {
        throw sdl_error("decode original WAV");
    }
    std::unique_ptr<Uint8, SdlFree> wav(wav_pointer);
    if (wav_length > std::uint32_t(std::numeric_limits<int>::max())) {
        throw assets::AssetError("WAV exceeds SDL conversion length range");
    }
    Uint8 *converted_pointer = nullptr;
    int converted_length = 0;
    if (!SDL_ConvertAudioSamples(&source_format, wav.get(), static_cast<int>(wav_length), &mixer_format,
                                 &converted_pointer, &converted_length)) {
        throw sdl_error("convert original WAV to mixer format");
    }
    std::unique_ptr<Uint8, SdlFree> converted(converted_pointer);
    if (converted_length <= 0 || converted_length % bytes_per_frame != 0) {
        throw assets::AssetError("WAV conversion returned an invalid stereo sample count");
    }
    std::vector<float> result(static_cast<std::size_t>(converted_length) / sizeof(float));
    std::memcpy(result.data(), converted.get(), static_cast<std::size_t>(converted_length));
    return result;
}

} // namespace

std::string_view music_file(Music music) noexcept {
    const auto index = static_cast<std::size_t>(music);
    return index < context_files.size() ? context_files[index] : "silence";
}

// Threading model (see native_audio.hpp):
//  * SDL's audio device thread calls feed() whenever the device needs samples and mixes exactly
//    that much, so a stalled main thread (archive open, FLC decode, map load, slow software
//    canvas) can no longer starve the device.
//  * Everything slow -- archive IO, WAV decode, MIDI parsing, soundbank loading -- happens on
//    the main thread (or the startup archive loader) *outside* `mutex`; the main thread then
//    takes `mutex` only to swap pointers / small structs.
//  * Fields marked [shared] are guarded by `mutex`. Fields marked [main] are only touched by the
//    main thread. [const] fields are fixed after construction.
//  * The audio thread never throws and never calls the trace; its log lines are queued and
//    handed to the trace by update() on the main thread.
struct Audio::Impl {
    // [const]
    bool enabled{};
    std::filesystem::path directory;
    // Every original MIDI, parsed at startup; the audio thread only walks these lists.
    std::map<std::string, std::unique_ptr<tml_message, TmlFree>, std::less<>> midi_cache;
    // CD audio track per MIDI name (Steam release). A song with a track plays it instead of its MIDI.
    std::map<std::string, std::filesystem::path, std::less<>> cd_tracks;
    // [main]
    Trace trace;
    std::array<std::optional<std::size_t>, std::size_t(Loop::count)> loops{};
    struct Saved {
        std::optional<Music> context; // nullopt = board playlist track
        unsigned track{1};
        unsigned position_ms{};
    };
    std::vector<Saved> music_stack; // pushed contexts save what was under them
    std::pair<std::string_view, unsigned> cd_loaded_for; // song and playlist track load_cd_tracks last saw
    std::optional<assets::Archive> effects;
    std::optional<assets::Archive> voices;
    std::future<void> archive_loader; // opens Effect.mkf/Speaking.mkf off the main thread at startup
    std::string archive_error;        // why the loader failed; every later clip request rethrows it
    // Decoded clips; std::map nodes never move, so the audio thread may keep `samples` pointers.
    std::map<std::pair<bool, std::size_t>, std::vector<float>> samples;
    std::uint64_t last_update_ns{};
    bool error_reported{};
    std::string diagnostic;

    // [shared] -- everything below is guarded by `mutex`
    mutable std::mutex mutex;
    bool music_on{true};
    unsigned track{1};
    std::optional<Music> context; // what the MIDI player plays: a context (looped) or playlist `track`
    bool player_stale{};          // music was off when the track changed: (re)start when on
    unsigned resume_ms{};         // position to seek to when a stale track is started
    Statistics stats;
    std::unique_ptr<tsf, TsfClose> synth;
    const tml_message *song{};       // first event of the playing MIDI; null = no music
    const tml_message *next_event{}; // next event to apply; null = the MIDI has ended
    double song_ms{};                // playback position, advanced by rendered samples
    // Compressed CD tracks (2-10 MB each): the main thread reads the playing, next playlist and
    // saved songs (load_cd_track) and drops the rest, so the audio thread never touches files.
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> cd_cache;
    std::vector<char> vorbis_arena = std::vector<char>(vorbis_arena_bytes);
    std::unique_ptr<stb_vorbis, VorbisClose> vorbis; // decoding the playing CD track; null = MIDI or none
    int vorbis_channels{};
    bool vorbis_ended{}; // decoded to the end: the resampler drains, then the playlist advances
    std::array<float, 4096> decoded{};
    struct Clip {
        const std::vector<float> *samples;
        std::size_t position{};
        bool voice{};
        bool loop{}; // play_loop(): restarts at the end until stop_loop()
        Loop channel{Loop::spinner};
    };
    std::vector<Clip> clips;
    struct Queued {
        std::size_t index;
        const std::vector<float> *samples; // decoded by queue_speech on the main thread
    };
    std::vector<Queued> speech_queue;
    std::vector<std::pair<std::string_view, std::string>> pending_log; // audio-thread trace lines
    std::string render_error;
    Limiter limiter{float(sample_rate)};
    // Dev: RICH4_AUDIO_CAPTURE=out.wav records up to 60 s of the device output, written on exit.
    std::vector<float> capture;
    std::filesystem::path capture_path;

    // Last member: destroyed first, so the device thread stops before anything it reads.
    std::unique_ptr<AudioSubsystem> subsystem;
    // [shared] CD track (44.1 kHz) -> mixer rate; after `subsystem`, whose shutdown frees all streams.
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> resampler{nullptr, SDL_DestroyAudioStream};
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr,
                                                                               SDL_DestroyAudioStream};

    Impl(const std::filesystem::path &assets, bool requested, const std::filesystem::path &bank)
        : enabled(requested), directory(assets) {
        if (!enabled) {
            return;
        }
        // Speaking.mkf alone is 57 MB: opening it on the first voice mid-game stalled the main
        // thread for a long time. Read both PCM archives in the background right away.
        archive_loader = std::async(std::launch::async, [this] {
            auto effect_archive = assets::Archive(asset_path(directory, "effect.mkf"));
            auto voice_archive = assets::Archive(asset_path(directory, "speaking.mkf"));
            effects.emplace(std::move(effect_archive));
            voices.emplace(std::move(voice_archive));
        });
        if (const auto *path = std::getenv("RICH4_AUDIO_CAPTURE"); path && *path) {
            capture_path = path;
            capture.reserve(std::size_t(sample_rate) * channels * 60);
        }
        subsystem = std::make_unique<AudioSubsystem>();
        initialize_music(bank);
        stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer_format, feed_callback,
                                               this));
        if (!stream) {
            throw sdl_error("open SDL audio stream");
        }
        if (!SDL_AddEventWatch(lifecycle_watch, this)) {
            throw sdl_error("watch application lifecycle for audio");
        }
        if (!SDL_ResumeAudioStreamDevice(stream.get())) {
            throw sdl_error("resume SDL audio stream");
        }
    }

    ~Impl() {
        // Stop the only hardware-facing object before releasing the software mixer and its bank.
        if (stream) {
            SDL_RemoveEventWatch(lifecycle_watch, this);
            stream.reset(); // after this the device thread no longer calls feed()
        }
        write_capture();
        if (archive_loader.valid())
            archive_loader.wait();
    }

    // Android/iOS: stop pulling samples while backgrounded and continue exactly where the music
    // was on return (MIDI time follows rendered samples). Desktop never sends these events.
    static bool SDLCALL lifecycle_watch(void *self, SDL_Event *event) {
        auto &impl = *static_cast<Impl *>(self);
        if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND)
            SDL_PauseAudioStreamDevice(impl.stream.get());
        else if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
            SDL_ResumeAudioStreamDevice(impl.stream.get());
        return true;
    }

    void initialize_music(std::filesystem::path bank) {
        // Next to the .mkf files, or where Steam installs them (Game/ and Media/Music/ are siblings).
        const std::array<std::filesystem::path, 2> track_directories{directory, directory / ".." / "Media" / "Music"};
        const auto find_track = [this, &track_directories](std::string_view file) {
            for (const auto &track_directory : track_directories) {
                if (auto path = find_asset(track_directory, cd_track_file(file))) {
                    cd_tracks.emplace(file, std::move(*path));
                    return;
                }
            }
        };
        for (const auto file : music_files)
            find_track(file);
        for (const auto file : std::span(context_files).subspan(1))
            find_track(file);
        if (!cd_tracks.empty()) {
            resampler.reset(SDL_CreateAudioStream(&mixer_format, &mixer_format));
            if (!resampler)
                throw sdl_error("create CD audio resampler");
            load_cd_tracks();
        }
        if (cd_tracks.size() == music_files.size() + context_files.size() - 1) {
            start_track_locked(); // every song has its CD track: no MIDI, no soundbank
            return;
        }
        initialize_midi(std::move(bank));
        start_track_locked(); // no device yet: no lock needed
    }

    void initialize_midi(std::filesystem::path bank) {
        if (bank.empty()) {
            if (const auto *environment = std::getenv("RICH4_SOUNDFONT")) {
                bank = environment;
            }
        }
        // The bank ships next to the program (macOS: the app's Resources folder).
        if (const auto *base = SDL_GetBasePath(); bank.empty() && base) {
            if (const auto bundled = std::filesystem::path(base) / bundled_soundfont;
                std::filesystem::is_regular_file(bundled))
                bank = bundled;
        }
#ifdef RICH4_DEFAULT_SOUNDFONT
        if (bank.empty() && std::filesystem::is_regular_file(RICH4_DEFAULT_SOUNDFONT)) {
            bank = RICH4_DEFAULT_SOUNDFONT; // the build tree's download (tests, unbundled tools)
        }
#endif
        if (bank.empty() || !std::filesystem::is_regular_file(bank)) {
            diagnostic = "MIDI soundbank unavailable; set RICH4_SOUNDFONT to a General MIDI SF2 file";
            return;
        }
        // All MIDIs are tiny (~0.5 MB together): parse them now so neither a context push nor the
        // playlist advancing on the audio thread ever touches the file system. The playlist files
        // stay mandatory unless their CD track exists; a missing context MIDI only keeps that
        // screen silent.
        const auto parse = [this](std::string_view file) {
            const auto bytes = read_bytes(asset_path(directory, file));
            std::unique_ptr<tml_message, TmlFree> parsed(tml_load_memory(bytes.data(), int(bytes.size())));
            if (!parsed) {
                throw std::runtime_error("cannot load original MIDI " + std::string(file));
            }
            midi_cache.emplace(file, std::move(parsed));
        };
        for (const auto file : music_files) {
            if (!cd_tracks.contains(file))
                parse(file);
        }
        for (const auto file : std::span(context_files).subspan(1)) {
            if (cd_tracks.contains(file))
                continue;
            try {
                parse(file);
            } catch (const std::exception &error) {
                diagnostic = error.what();
            }
        }
        synth.reset(tsf_load_filename(bank.string().c_str()));
        if (!synth) {
            diagnostic = "could not load MIDI soundbank: " + bank.string();
            return;
        }
        tsf_set_output(synth.get(), TSF_STEREO_INTERLEAVED, sample_rate, music_gain_db);
        tsf_set_max_voices(synth.get(), 128); // preallocated: note-ons never allocate on the audio thread
    }

    // Main thread: keep exactly the CD tracks that may start without the main thread -- the song
    // playing now, the next playlist song (the audio thread advances to it) and every song a pop
    // resumes -- plus `upcoming`, the song a caller is about to start. Files are read outside
    // `mutex`; only the map update is locked.
    void load_cd_tracks(std::string_view upcoming = {}) {
        if (cd_tracks.empty())
            return;
        std::vector<std::string_view> wanted;
        {
            const std::lock_guard lock(mutex);
            cd_loaded_for = {current_file_locked(), track};
            wanted.push_back(cd_loaded_for.first);
            wanted.push_back(music_files[track % 8]);
        }
        if (!upcoming.empty())
            wanted.push_back(upcoming);
        for (const auto &saved : music_stack)
            wanted.push_back(saved.context ? music_file(*saved.context) : music_files[saved.track - 1]);
        std::vector<std::pair<std::string_view, std::vector<std::uint8_t>>> loaded;
        for (const auto file : wanted) {
            const auto path = cd_tracks.find(file);
            if (path == cd_tracks.end() || std::any_of(loaded.begin(), loaded.end(), [file](const auto &entry) {
                    return entry.first == file;
                }))
                continue;
            {
                const std::lock_guard lock(mutex);
                if (cd_cache.contains(file))
                    continue;
            }
            try {
                loaded.emplace_back(file, read_bytes(path->second, 64 * 1024 * 1024));
            } catch (const std::exception &error) {
                diagnostic = error.what(); // that song falls back to its MIDI, else silence
            }
        }
        const std::lock_guard lock(mutex);
        std::erase_if(cd_cache, [&wanted](const auto &entry) {
            return std::find(wanted.begin(), wanted.end(), entry.first) == wanted.end();
        });
        for (auto &[file, bytes] : loaded)
            cd_cache.emplace(file, std::move(bytes));
    }

    void log(std::string_view what, std::string_view detail) const {
        if (trace)
            trace(what, detail);
    }
    std::string_view current_file_locked() const {
        return context ? music_file(*context) : music_files[track - 1];
    }
    std::string current_file() const {
        const std::lock_guard lock(mutex);
        return std::string(current_file_locked());
    }
    // (Re)starts whatever `context`/`track` select, from `position_ms`. With music switched off
    // the start is deferred (player_stale) so a muted game never renders MIDI. Cheap (no IO):
    // the MIDIs were parsed at startup, CD tracks are read ahead by load_cd_tracks and decoded in
    // a preallocated arena. Caller holds `mutex`.
    void start_track_locked(unsigned position_ms = 0) {
        if (!synth && cd_tracks.empty()) {
            return;
        }
        song = next_event = nullptr;
        vorbis.reset();
        song_ms = 0;
        if (synth) {
            tsf_reset(synth.get());
            tsf_channel_set_bank_preset(synth.get(), 9, 128, 0); // General MIDI: channel 10 is drums
        }
        if (!music_on) {
            player_stale = true;
            resume_ms = position_ms;
            return;
        }
        player_stale = false;
        if (context == Music::silence) {
            return; // no song: silence until popped
        }
        if (const auto track_bytes = cd_cache.find(current_file_locked()); track_bytes != cd_cache.end()) {
            start_cd_track_locked(track_bytes->second, position_ms);
            if (vorbis)
                return;
        }
        if (!synth) {
            return;
        }
        const auto cached = midi_cache.find(current_file_locked());
        if (cached == midi_cache.end()) {
            return; // missing context MIDI (reported at startup): silent, game keeps running
        }
        song = next_event = cached->second.get();
        // Resume where push_music left it: replay programs and controllers, skip the notes.
        for (; next_event && next_event->time < position_ms; next_event = next_event->next)
            if (next_event->type != TML_NOTE_ON && next_event->type != TML_NOTE_OFF)
                apply_event_locked(*next_event);
        song_ms = position_ms;
    }
    void start_cd_track_locked(const std::vector<std::uint8_t> &bytes, unsigned position_ms) {
        const stb_vorbis_alloc arena{vorbis_arena.data(), int(vorbis_arena.size())};
        int error = 0;
        vorbis.reset(stb_vorbis_open_memory(bytes.data(), int(bytes.size()), &error, &arena));
        if (!vorbis) {
            pending_log.emplace_back("music_error", std::string(current_file_locked()) + " CD track: stb_vorbis error " +
                                                        std::to_string(error));
            return; // falls back to the MIDI
        }
        const auto info = stb_vorbis_get_info(vorbis.get());
        vorbis_channels = info.channels;
        vorbis_ended = false;
        const SDL_AudioSpec source{SDL_AUDIO_F32, info.channels, int(info.sample_rate)};
        SDL_ClearAudioStream(resampler.get());
        SDL_SetAudioStreamFormat(resampler.get(), &source, &mixer_format);
        if (position_ms)
            stb_vorbis_seek(vorbis.get(), unsigned(std::uint64_t(position_ms) * info.sample_rate / 1000));
        song_ms = position_ms;
    }
    // Fills `output` from the CD track. Context songs loop until popped (fcn_00454d2c); a playlist
    // song decodes to its end and drains the resampler, and render_block_locked advances.
    void render_cd_track_locked(std::span<float> output) noexcept {
        auto *const bytes = reinterpret_cast<std::uint8_t *>(output.data());
        const int wanted = int(output.size_bytes());
        int filled = 0;
        bool rewound = false;
        while (filled < wanted) {
            const int got = SDL_GetAudioStreamData(resampler.get(), bytes + filled, wanted - filled);
            if (got > 0) {
                filled += got;
                continue;
            }
            if (got < 0 || vorbis_ended)
                break;
            const int frames = stb_vorbis_get_samples_float_interleaved(vorbis.get(), vorbis_channels, decoded.data(),
                                                                        int(decoded.size()));
            if (frames > 0) {
                SDL_PutAudioStreamData(resampler.get(), decoded.data(), frames * vorbis_channels * int(sizeof(float)));
            } else if (context && !rewound) {
                rewound = true; // once per block: a track that decodes nothing ends instead
                stb_vorbis_seek_start(vorbis.get());
                song_ms = 0;
            } else {
                vorbis_ended = true;
                SDL_FlushAudioStream(resampler.get());
            }
        }
        for (auto &sample : output.first(std::size_t(filled) / sizeof(float)))
            sample *= cd_gain;
        song_ms += block_frames * 1000.0 / sample_rate;
    }
    bool song_ended_locked() const {
        if (vorbis)
            return vorbis_ended && SDL_GetAudioStreamAvailable(resampler.get()) <= 0;
        return song && !next_event;
    }
    void apply_event_locked(const tml_message &event) noexcept {
        auto *const f = synth.get();
        const int channel = event.channel;
        switch (event.type) {
        case TML_PROGRAM_CHANGE:
            tsf_channel_set_presetnumber(f, channel, event.program, channel == 9);
            break;
        case TML_NOTE_ON:
            if (event.velocity > 0) {
                tsf_channel_note_on(f, channel, event.key, float(event.velocity) / 127.0F);
                break;
            }
            [[fallthrough]]; // velocity 0 is a note-off
        case TML_NOTE_OFF:
            tsf_channel_note_off(f, channel, event.key);
            break;
        case TML_PITCH_BEND:
            tsf_channel_set_pitchwheel(f, channel, event.pitch_bend);
            break;
        case TML_CONTROL_CHANGE:
            tsf_channel_midi_control(f, channel, event.control, event.control_value);
            break;
        default:
            break;
        }
    }
    void start_track(unsigned position_ms = 0) { // main thread
        const std::lock_guard lock(mutex);
        start_track_locked(position_ms);
    }
    unsigned current_position_locked() const {
        if (player_stale)
            return resume_ms;
        return song ? unsigned(song_ms) : 0;
    }
    void push(Music music, bool save) {
        log(save ? "music_push" : "music_replace", music_file(music));
        load_cd_tracks(music_file(music));
        const std::lock_guard lock(mutex);
        if (save || music_stack.empty())
            music_stack.push_back({context, track, current_position_locked()});
        context = music;
        start_track_locked();
    }
    void pop() {
        if (music_stack.empty())
            return;
        const auto saved = music_stack.back();
        music_stack.pop_back();
        std::string file;
        {
            const std::lock_guard lock(mutex);
            context = saved.context;
            track = saved.track;
            file = current_file_locked();
        }
        log("music_pop", file);
        start_track(saved.position_ms);
    }

    // Main thread only; may block on the startup archive loader (normally long finished).
    const std::vector<float> &load_clip(bool voice, std::size_t index) {
        const auto key = std::pair{voice, index};
        if (const auto found = samples.find(key); found != samples.end()) {
            return found->second;
        }
        if (archive_loader.valid()) {
            try {
                archive_loader.get();
            } catch (const std::exception &error) {
                archive_error = error.what();
            }
        }
        auto &archive = voice ? voices : effects;
        if (!archive) // a missing archive fails at first use, as before
            throw assets::AssetError(archive_error.empty() ? "audio archive unavailable" : archive_error);
        auto pcm = decode_wave(archive->decode(index));
        return samples.emplace(key, std::move(pcm)).first->second;
    }

    // Caller holds `mutex`.
    void start_clip_locked(const std::vector<float> &pcm, bool voice, bool keep_queue, bool loop = false,
                           Loop channel = Loop::spinner) {
        if (voice) {
            if (!keep_queue)
                speech_queue.clear();
            std::erase_if(clips, [](const Clip &clip) { return clip.voice; });
            ++stats.voices_started;
        } else {
            ++stats.effects_started;
        }
        // Bound bursts of user input. Voice always has its own single slot;
        // effects can overlap, with the oldest effect replaced after 16 voices.
        if (!voice &&
            std::count_if(clips.begin(), clips.end(), [](const Clip &clip) { return !clip.voice; }) >= 16) {
            clips.erase(
                std::find_if(clips.begin(), clips.end(), [](const Clip &clip) { return !clip.voice; }));
        }
        clips.push_back({&pcm, 0, voice, loop, channel});
    }

    void play_clip(bool voice, std::size_t index, bool keep_queue = false, bool loop = false,
                   Loop channel = Loop::spinner) {
        if (!enabled) {
            return;
        }
        const auto &pcm = load_clip(voice, index); // decode outside the lock
        const std::lock_guard lock(mutex);
        start_clip_locked(pcm, voice, keep_queue, loop, channel);
    }

    // ---- audio thread ----
    static void SDLCALL feed_callback(void *self, SDL_AudioStream *stream, int additional_amount, int) {
        static_cast<Impl *>(self)->feed(stream, additional_amount);
    }
    void feed(SDL_AudioStream *output, int additional_amount) noexcept {
        if (additional_amount <= 0)
            return;
        const auto begin = SDL_GetTicksNS();
        std::array<float, block_frames * channels> block;
        const std::lock_guard lock(mutex);
        // Whole blocks keep MIDI timing identical to the old main-thread mixer; the (< 1 block)
        // surplus stays queued in the stream and shortens the next request.
        int produced = 0;
        while (produced < additional_amount) {
            render_block_locked(block);
            const int byte_count = int(block.size() * sizeof(float));
            if (!SDL_PutAudioStreamData(output, block.data(), byte_count)) {
                render_error = std::string("queue native mixed audio: ") + SDL_GetError();
                break;
            }
            capture_block_locked(block);
            produced += byte_count;
        }
        // Real-time budget: the device can only starve if mixing ever runs slower than playback.
        const auto end = SDL_GetTicksNS();
        const auto spent_ns = end - begin;
        const auto audio_ns = std::uint64_t(produced / bytes_per_frame) * 1000000000ULL / sample_rate;
        stats.max_render_us = std::max<std::uint64_t>(stats.max_render_us, spent_ns / 1000);
        if (spent_ns > audio_ns) {
            ++stats.underruns;
            stats.starved_ms += (spent_ns - audio_ns) / 1000000;
        }
    }

    void render_block_locked(std::span<float> output) noexcept {
        std::fill(output.begin(), output.end(), 0.0F);
        if (music_on && song_ended_locked()) {
            if (context) {
                next_event = song; // context MIDIs loop until popped (fcn_00454d2c)
                song_ms = 0;
            } else {
                track = track % 8 + 1;
                pending_log.emplace_back("music_next", current_file_locked());
                start_track_locked();
            }
        }
        if (music_on && vorbis) {
            render_cd_track_locked(output);
            stats.music_frames += block_frames;
        } else if (music_on && song) {
            constexpr double event_ms = event_frames * 1000.0 / sample_rate;
            for (int offset = 0; offset < block_frames; offset += event_frames) {
                const double until = song_ms + event_ms;
                for (; next_event && next_event->time < until; next_event = next_event->next)
                    apply_event_locked(*next_event);
                tsf_render_float(synth.get(), output.data() + std::ptrdiff_t(offset) * channels, event_frames, 0);
                song_ms = until;
            }
            stats.music_frames += block_frames;
        }
        for (auto &clip : clips) {
            std::size_t written = 0;
            do {
                const auto available = clip.samples->size() - clip.position;
                const auto count = std::min(output.size() - written, available);
                for (std::size_t i = 0; i < count; ++i) {
                    output[written + i] += (*clip.samples)[clip.position + i] * clip_gain;
                }
                clip.position += count;
                written += count;
                if (clip.position == clip.samples->size()) {
                    ++stats.clips_completed;
                    if (clip.loop && !clip.samples->empty()) {
                        clip.position = 0;
                    }
                }
            } while (clip.loop && !clip.samples->empty() && written < output.size());
        }
        std::erase_if(clips, [](const Clip &clip) { return clip.position == clip.samples->size(); });
        if (!speech_queue.empty() &&
            std::none_of(clips.begin(), clips.end(), [](const Clip &clip) { return clip.voice; })) {
            const auto next = speech_queue.front();
            speech_queue.erase(speech_queue.begin());
            pending_log.emplace_back("speech", std::to_string(next.index));
            start_clip_locked(*next.samples, true, true);
        }
        for (const auto sample : output) {
            const float magnitude = std::abs(sample);
            stats.peak = std::max(stats.peak, magnitude);
            if (magnitude > 1.0F)
                ++stats.clipped_samples; // what the old hard clamp would have squared off
        }
        stats.limited_samples += limiter.process(output);
        for (const auto sample : output) {
            if (std::abs(sample) > 0.000001F) {
                ++stats.nonzero_samples;
            }
        }
        stats.rendered_frames += block_frames;
    }

    void capture_block_locked(std::span<const float> block) {
        if (capture_path.empty())
            return;
        const auto room = capture.capacity() - capture.size(); // never reallocates on this thread
        capture.insert(capture.end(), block.begin(), block.begin() + std::ptrdiff_t(std::min(room, block.size())));
    }
    void write_capture() const {
        if (capture_path.empty())
            return;
        std::ofstream out(capture_path, std::ios::binary);
        const auto put32 = [&out](std::uint32_t value) { out.write(reinterpret_cast<const char *>(&value), 4); };
        const auto put16 = [&out](std::uint16_t value) { out.write(reinterpret_cast<const char *>(&value), 2); };
        const auto data_bytes = std::uint32_t(capture.size() * 2);
        out.write("RIFF", 4);
        put32(36 + data_bytes);
        out.write("WAVEfmt ", 8);
        put32(16);
        put16(1); // PCM
        put16(channels);
        put32(sample_rate);
        put32(sample_rate * channels * 2);
        put16(channels * 2);
        put16(16);
        out.write("data", 4);
        put32(data_bytes);
        for (const float sample : capture) {
            const auto value = std::int16_t(std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
            put16(std::uint16_t(value));
        }
    }

    // ---- main thread, once per frame ----
    // After the playlist advanced: read the following song ahead, drop the finished one, and start
    // the new song if the audio thread reached it before its track was read.
    void update_cd_tracks() {
        if (cd_tracks.empty())
            return;
        {
            const std::lock_guard lock(mutex);
            if (cd_loaded_for == std::pair{current_file_locked(), track})
                return;
        }
        load_cd_tracks();
        const std::lock_guard lock(mutex);
        if (music_on && !player_stale && !vorbis && !song && context != Music::silence &&
            cd_cache.contains(current_file_locked()))
            start_track_locked();
    }

    void update() {
        if (!enabled) {
            return;
        }
        const auto now_ns = SDL_GetTicksNS();
        std::vector<std::pair<std::string_view, std::string>> lines;
        std::string error;
        {
            const std::lock_guard lock(mutex);
            if (last_update_ns)
                stats.max_update_gap_ms =
                    std::max<std::uint64_t>(stats.max_update_gap_ms, (now_ns - last_update_ns) / 1000000);
            lines.swap(pending_log);
            error.swap(render_error);
        }
        last_update_ns = now_ns;
        for (const auto &[what, detail] : lines)
            log(what, detail);
        update_cd_tracks();
        if (!error.empty() && !error_reported) {
            error_reported = true;
            diagnostic = error;
            std::fprintf(stderr, "audio: %s\n", error.c_str());
        }
    }
};

Audio::Audio(const std::filesystem::path &directory, bool enabled, const std::filesystem::path &soundbank)
    : impl_(std::make_unique<Impl>(directory, enabled, soundbank)) {}
Audio::~Audio() = default;
Audio::Audio(Audio &&) noexcept = default;
Audio &Audio::operator=(Audio &&) noexcept = default;

void Audio::play_sound(std::size_t index) {
    impl_->log("sound", std::to_string(index));
    impl_->play_clip(false, index);
}

void Audio::play_loop(std::size_t index, Loop channel) {
    auto &slot = impl_->loops.at(std::size_t(channel));
    if (slot == index) {
        return; // already looping here: keep it seamless
    }
    stop_loop(channel);
    slot = index;
    impl_->log("loop_start", std::string(loop_names[std::size_t(channel)]) + " " + std::to_string(index));
    impl_->play_clip(false, index, false, true, channel);
}

void Audio::stop_loop(Loop channel) {
    auto &slot = impl_->loops.at(std::size_t(channel));
    if (!slot) {
        return;
    }
    impl_->log("loop_stop", std::string(loop_names[std::size_t(channel)]) + " " + std::to_string(*slot));
    slot.reset();
    const std::lock_guard lock(impl_->mutex);
    std::erase_if(impl_->clips,
                  [channel](const Impl::Clip &clip) { return clip.loop && clip.channel == channel; });
}

void Audio::stop_all_loops() {
    for (std::size_t channel = 0; channel < impl_->loops.size(); ++channel)
        stop_loop(Loop(channel));
}

std::optional<std::size_t> Audio::looping(Loop channel) const noexcept {
    return std::size_t(channel) < impl_->loops.size() ? impl_->loops[std::size_t(channel)] : std::nullopt;
}

void Audio::play_voice(std::uint8_t character, std::uint16_t category) {
    if (character >= 12 || category >= 27) {
        throw std::invalid_argument("voice character/category outside original event table");
    }
    impl_->play_clip(true, 1050U + character * 27U + category);
}

void Audio::queue_speech(std::size_t index) {
    if (!impl_->enabled) {
        return;
    }
    const auto &pcm = impl_->load_clip(true, index); // decode now: the audio thread starts it later
    bool now = false;
    {
        const std::lock_guard lock(impl_->mutex);
        now = impl_->speech_queue.empty() &&
              std::none_of(impl_->clips.begin(), impl_->clips.end(), [](const Impl::Clip &clip) { return clip.voice; });
        if (now)
            impl_->start_clip_locked(pcm, true, true);
        else
            impl_->speech_queue.push_back({index, &pcm});
    }
    if (now)
        impl_->log("speech", std::to_string(index));
}

std::uint32_t Audio::play_speech(std::size_t index) {
    if (!impl_->enabled) {
        return 0;
    }
    impl_->log("speech", std::to_string(index));
    const auto &pcm = impl_->load_clip(true, index);
    impl_->play_clip(true, index);
    return static_cast<std::uint32_t>(pcm.size() / channels * 1000 / sample_rate);
}

void Audio::stop_voice() {
    const std::lock_guard lock(impl_->mutex);
    impl_->speech_queue.clear();
    std::erase_if(impl_->clips, [](const Impl::Clip &clip) { return clip.voice; });
}

bool Audio::voice_active() const noexcept {
    const std::lock_guard lock(impl_->mutex);
    return std::any_of(impl_->clips.begin(), impl_->clips.end(), [](const Impl::Clip &clip) { return clip.voice; });
}

void Audio::set_music(bool enabled) {
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->music_on == enabled)
            return;
        impl_->music_on = enabled;
        if (enabled && impl_->player_stale)
            impl_->start_track_locked(impl_->resume_ms); // resume what changed while muted
    }
    impl_->log("music_enabled", enabled ? "on" : "off");
}

void Audio::select_music(unsigned track) {
    if (track > 8) {
        throw std::invalid_argument("normal MIDI playlist selection must be 0 through 8");
    }
    if (track == 0) {
        const std::lock_guard lock(impl_->mutex);
        track = impl_->track % 8 + 1;
    }
    impl_->load_cd_tracks(music_files[track - 1]);
    std::string file;
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->track = track;
        impl_->context.reset();
        file = impl_->current_file_locked();
    }
    impl_->music_stack.clear();
    impl_->log("music_select", file);
    impl_->start_track();
}

void Audio::push_music(Music music) {
    impl_->push(music, true);
}
void Audio::pop_music() {
    impl_->pop();
}
void Audio::replace_music(Music music) {
    impl_->push(music, false);
}
std::size_t Audio::music_depth() const noexcept {
    return impl_->music_stack.size();
}
std::optional<Music> Audio::music_context() const noexcept {
    return impl_->context; // written only by the main thread
}
std::string Audio::music_name() const {
    return impl_->current_file();
}
void Audio::set_trace(Trace trace) {
    impl_->trace = std::move(trace);
}

void Audio::update() {
    impl_->update();
}
bool Audio::enabled() const noexcept {
    return impl_->enabled;
}
bool Audio::music_available() const noexcept {
    return impl_->synth != nullptr || !impl_->cd_tracks.empty(); // set once at construction
}
bool Audio::music_enabled() const noexcept {
    return impl_->music_on; // written only by the main thread
}
unsigned Audio::music_track() const noexcept {
    const std::lock_guard lock(impl_->mutex); // the audio thread advances the playlist
    return impl_->track;
}
Statistics Audio::statistics() const noexcept {
    const std::lock_guard lock(impl_->mutex);
    return impl_->stats;
}
int Audio::queued_bytes() const {
    return impl_->stream ? SDL_GetAudioStreamQueued(impl_->stream.get()) : 0;
}
const std::string &Audio::music_diagnostic() const noexcept {
    return impl_->diagnostic;
}

} // namespace rich4::audio
