// SPDX-License-Identifier: GPL-3.0-or-later
// Resource selection follows Iru Cai's Rich4 reverse engineering:
// rich4_sound_effect.asm, rich4_media_music.asm, rich4.asm event strings.
#include "native_audio.hpp"

#include "rich4/assets.hpp"

#include <SDL3/SDL.h>
#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace rich4::audio {
namespace {

constexpr int sample_rate = 48000;
constexpr int channels = 2;
constexpr int block_frames = 512;
constexpr int bytes_per_frame = channels * int(sizeof(float));
constexpr int queue_target_frames = 3072; // 64 ms: enough for the 40-ms original animation tick.
constexpr SDL_AudioSpec mixer_format{SDL_AUDIO_F32, channels, sample_rate};
constexpr std::array<std::string_view, 8> music_files{"rich08.mid", "rich16.mid", "rich17.mid", "rich18.mid",
                                                      "rich19.mid", "rich20.mid", "rich21.mid", "rich22.mid"};
// ref_0047e793, indexed by Music (silence first).
constexpr std::array<std::string_view, 18> context_files{
    "silence",    "midi01.mid", "midi02.mid", "midi03.mid",   "midi04.mid",   "midi05.mid",
    "midi06.mid", "midi07.mid", "midi08.mid", "midi09.mid",   "midi10.mid",   "midi11.mid",
    "midi12.mid", "midi13.mid", "midi14-1.mid", "midi14-2.mid", "midi15.mid", "midi16.mid"};
constexpr std::array<std::string_view, std::size_t(Loop::count)> loop_names{"spinner", "walk", "bomb"};

std::filesystem::path asset_path(const std::filesystem::path &directory, std::string_view name) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        auto candidate = entry.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char value) { return char(std::tolower(value)); });
        if (candidate == name && entry.is_regular_file()) {
            return entry.path();
        }
    }
    throw assets::AssetError("missing audio asset: " + (directory / name).string());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open MIDI: " + path.string());
    }
    const auto length = input.tellg();
    if (length < 0 || length > 16 * 1024 * 1024) {
        throw std::runtime_error("invalid MIDI size: " + path.string());
    }
    std::vector<std::uint8_t> result(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(result.data()), length)) {
        throw std::runtime_error("truncated MIDI: " + path.string());
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

struct Audio::Impl {
    bool enabled{};
    bool music_on{true};
    unsigned track{1};
    std::string diagnostic;
    Statistics stats;
    std::filesystem::path directory;
    std::unique_ptr<AudioSubsystem> subsystem;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{nullptr,
                                                                               SDL_DestroyAudioStream};
    std::unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings{nullptr,
                                                                                 delete_fluid_settings};
    std::unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth{nullptr, delete_fluid_synth};
    std::unique_ptr<fluid_player_t, decltype(&delete_fluid_player)> player{nullptr, delete_fluid_player};
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> midi_cache; // file -> bytes (never evicted)
    // What the MIDI player is playing: a context (looped) or, when empty, playlist `track`.
    std::optional<Music> context;
    struct Saved {
        std::optional<Music> context; // nullopt = board playlist track
        unsigned track{1};
        int tick{};
    };
    std::vector<Saved> music_stack; // pushed contexts save what was under them
    bool player_stale{};            // music was off when the track changed: (re)start when on
    int resume_tick{};              // position to seek to when a stale track is started
    std::array<std::optional<std::size_t>, std::size_t(Loop::count)> loops{};
    std::vector<std::size_t> speech_queue;
    Trace trace;
    std::optional<assets::Archive> effects;
    std::optional<assets::Archive> voices;
    std::map<std::pair<bool, std::size_t>, std::vector<float>> samples;

    struct Clip {
        const std::vector<float> *samples;
        std::size_t position{};
        bool voice{};
        bool loop{}; // play_loop(): restarts at the end until stop_loop()
        Loop channel{Loop::spinner};
    };
    std::vector<Clip> clips;

    Impl(const std::filesystem::path &assets, bool requested, const std::filesystem::path &bank)
        : enabled(requested), directory(assets) {
        if (!enabled) {
            return;
        }
        subsystem = std::make_unique<AudioSubsystem>();
        stream.reset(
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer_format, nullptr, nullptr));
        if (!stream) {
            throw sdl_error("open SDL audio stream");
        }
        if (const auto *path = std::getenv("RICH4_AUDIO_CAPTURE"); path && *path) {
            capture_path = path;
            capture.reserve(std::size_t(sample_rate) * channels * 60);
        }
        initialize_music(bank);
        if (!SDL_ResumeAudioStreamDevice(stream.get())) {
            throw sdl_error("resume SDL audio stream");
        }
    }

    ~Impl() {
        // Stop the only hardware-facing object before releasing the software
        // mixer and its banks. No FluidSynth audio driver is ever constructed.
        stream.reset();
        write_capture();
        player.reset();
        synth.reset();
        settings.reset();
    }

    void initialize_music(std::filesystem::path bank) {
        if (bank.empty()) {
            if (const auto *environment = std::getenv("RICH4_SOUNDFONT")) {
                bank = environment;
            }
        }
#ifdef __APPLE__
        if (bank.empty()) {
            bank = "/System/Library/Components/CoreAudio.component/Contents/Resources/"
                   "gs_instruments.dls";
        }
#endif
        if (bank.empty() || !std::filesystem::is_regular_file(bank)) {
            diagnostic = "MIDI soundbank unavailable; set RICH4_SOUNDFONT to a General MIDI SF2/DLS file";
            return;
        }
        // The original MIDIs carry meta events FluidSynth only warns about; keep stderr for errors.
        fluid_set_log_function(FLUID_WARN, nullptr, nullptr);
        fluid_set_log_function(FLUID_INFO, nullptr, nullptr);
        settings.reset(new_fluid_settings());
        if (!settings) {
            throw std::runtime_error("cannot create FluidSynth settings");
        }
        fluid_settings_setnum(settings.get(), "synth.sample-rate", sample_rate);
        fluid_settings_setnum(settings.get(), "synth.gain", 0.4);
        fluid_settings_setstr(settings.get(), "player.timing-source", "sample");
        fluid_settings_setint(settings.get(), "synth.reverb.active", 0);
        fluid_settings_setint(settings.get(), "synth.chorus.active", 0);
        synth.reset(new_fluid_synth(settings.get()));
        if (!synth || fluid_synth_sfload(synth.get(), bank.string().c_str(), 1) < 0) {
            synth.reset();
            diagnostic = "FluidSynth could not load MIDI soundbank: " + bank.string();
            return;
        }
        start_track();
    }

    void log(std::string_view what, std::string_view detail) const {
        if (trace)
            trace(what, detail);
    }
    std::string_view current_file() const {
        return context ? music_file(*context) : music_files[track - 1];
    }
    // (Re)starts whatever `context`/`track` select, from `tick`. With music switched off the
    // start is deferred (player_stale) so a muted game never renders MIDI.
    void start_track(int tick = 0) {
        if (!synth) {
            return;
        }
        player.reset();
        fluid_synth_system_reset(synth.get());
        if (!music_on) {
            player_stale = true;
            resume_tick = tick;
            return;
        }
        player_stale = false;
        if (context == Music::silence) {
            return; // no player: silence until popped
        }
        const auto file = std::string(current_file());
        auto cached = midi_cache.find(file);
        if (cached == midi_cache.end()) {
            try {
                cached = midi_cache.emplace(file, read_bytes(asset_path(directory, file))).first;
            } catch (const std::exception &error) {
                // A missing context MIDI keeps the game running silently; the playlist files stay
                // mandatory exactly as before.
                if (!context)
                    throw;
                diagnostic = error.what();
                return;
            }
        }
        const auto &bytes = cached->second;
        player.reset(new_fluid_player(synth.get()));
        if (!player || fluid_player_add_mem(player.get(), bytes.data(), bytes.size()) != FLUID_OK) {
            throw std::runtime_error("cannot load original MIDI " + file);
        }
        if (context) {
            fluid_player_set_loop(player.get(), -1); // context MIDIs loop until popped (fcn_00454d2c)
        }
        if (fluid_player_play(player.get()) != FLUID_OK) {
            throw std::runtime_error("cannot start original MIDI " + file);
        }
        if (tick > 0) {
            fluid_player_seek(player.get(), tick); // resume where push_music left it
        }
    }
    int current_tick() {
        if (player_stale)
            return resume_tick;
        return player ? std::max(0, fluid_player_get_current_tick(player.get())) : 0;
    }
    void push(Music music, bool save) {
        if (save || music_stack.empty())
            music_stack.push_back({context, track, current_tick()});
        context = music;
        log(save ? "music_push" : "music_replace", music_file(music));
        start_track();
    }
    void pop() {
        if (music_stack.empty())
            return;
        const auto saved = music_stack.back();
        music_stack.pop_back();
        context = saved.context;
        track = saved.track;
        log("music_pop", current_file());
        start_track(saved.tick);
    }

    const std::vector<float> &load_clip(bool voice, std::size_t index) {
        const auto key = std::pair{voice, index};
        if (const auto found = samples.find(key); found != samples.end()) {
            return found->second;
        }
        auto &archive = voice ? voices : effects;
        if (!archive) {
            archive.emplace(asset_path(directory, voice ? "speaking.mkf" : "effect.mkf"));
        }
        auto pcm = decode_wave(archive->decode(index));
        return samples.emplace(key, std::move(pcm)).first->second;
    }

    void play_clip(bool voice, std::size_t index, bool keep_queue = false) {
        if (!enabled) {
            return;
        }
        const auto &pcm = load_clip(voice, index);
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
        clips.push_back({&pcm, 0, voice});
    }

    void render_block(std::span<float> output) {
        std::fill(output.begin(), output.end(), 0.0F);
        if (music_on && player_stale) {
            start_track(resume_tick);
        }
        if (music_on && player) {
            if (fluid_player_get_status(player.get()) == FLUID_PLAYER_DONE && !context) {
                track = track % 8 + 1;
                log("music_next", current_file());
                start_track();
            }
            if (player && fluid_synth_write_float(synth.get(), block_frames, output.data(), 0, 2, output.data(), 1,
                                        2) != FLUID_OK) {
                throw std::runtime_error("MIDI software rendering failed");
            }
            stats.music_frames += block_frames;
        }
        for (auto &clip : clips) {
            std::size_t written = 0;
            do {
                const auto available = clip.samples->size() - clip.position;
                const auto count = std::min(output.size() - written, available);
                for (std::size_t i = 0; i < count; ++i) {
                    output[written + i] += (*clip.samples)[clip.position + i];
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
            log("speech", std::to_string(next));
            play_clip(true, next, true);
        }
        for (auto &sample : output) {
            const float magnitude = std::abs(sample);
            stats.peak = std::max(stats.peak, magnitude);
            if (magnitude > 1.0F)
                ++stats.clipped_samples;
            sample = std::clamp(sample, -1.0F, 1.0F);
            if (std::abs(sample) > 0.000001F) {
                ++stats.nonzero_samples;
            }
        }
        stats.rendered_frames += block_frames;
    }

    void update() {
        if (!enabled) {
            return;
        }
        const auto now_ns = SDL_GetTicksNS();
        if (last_update_ns)
            stats.max_update_gap_ms = std::max<std::uint64_t>(stats.max_update_gap_ms, (now_ns - last_update_ns) / 1000000);
        last_update_ns = now_ns;
        int queued = SDL_GetAudioStreamQueued(stream.get());
        if (queued < 0) {
            throw sdl_error("query SDL audio queue");
        }
        if (queued == 0 && stats.rendered_frames > 0) {
            ++stats.underruns;
            const auto starved_ns = now_ns > drained_at_ns ? now_ns - drained_at_ns : 0;
            stats.starved_ms += starved_ns / 1000000;
            capture_silence(std::size_t(starved_ns * sample_rate / 1000000000ULL));
        }
        std::array<float, block_frames * channels> block;
        // Bounded catch-up prevents a broken device or long stall from causing
        // an unbounded main-thread loop. MIDI time follows rendered samples.
        for (int iteration = 0; iteration < 8 && queued < queue_target_frames * bytes_per_frame;
             ++iteration) {
            const auto render_start = SDL_GetTicksNS();
            render_block(block);
            stats.max_render_us = std::max<std::uint64_t>(stats.max_render_us, (SDL_GetTicksNS() - render_start) / 1000);
            capture_block(block);
            const int byte_count = int(block.size() * sizeof(float));
            if (!SDL_PutAudioStreamData(stream.get(), block.data(), byte_count)) {
                throw sdl_error("queue native mixed audio");
            }
            queued += byte_count;
        }
        drained_at_ns = SDL_GetTicksNS() + std::uint64_t(queued / bytes_per_frame) * 1000000000ULL / sample_rate;
    }
    std::uint64_t last_update_ns{}, drained_at_ns{};
    // Dev: RICH4_AUDIO_CAPTURE=out.wav records up to 60 s of what the device was given (push mode
    // inserts the estimated starvation as silence), written when the mixer is destroyed.
    std::vector<float> capture;
    std::filesystem::path capture_path;
    void capture_block(std::span<const float> block) {
        if (capture_path.empty())
            return;
        const auto room = capture.capacity() - capture.size();
        capture.insert(capture.end(), block.begin(), block.begin() + std::ptrdiff_t(std::min(room, block.size())));
    }
    void capture_silence(std::size_t frames) {
        if (capture_path.empty())
            return;
        const auto room = capture.capacity() - capture.size();
        capture.resize(capture.size() + std::min(room, frames * channels), 0.0F);
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
        put16(1);
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
    if (!impl_->enabled) {
        return;
    }
    impl_->play_clip(false, index);
    impl_->clips.back().loop = true;
    impl_->clips.back().channel = channel;
}

void Audio::stop_loop(Loop channel) {
    auto &slot = impl_->loops.at(std::size_t(channel));
    if (!slot) {
        return;
    }
    impl_->log("loop_stop", std::string(loop_names[std::size_t(channel)]) + " " + std::to_string(*slot));
    slot.reset();
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
    if (!voice_active() && impl_->speech_queue.empty()) {
        impl_->log("speech", std::to_string(index));
        impl_->play_clip(true, index, true);
        return;
    }
    impl_->speech_queue.push_back(index);
}

std::uint32_t Audio::play_speech(std::size_t index) {
    if (!impl_->enabled) {
        return 0;
    }
    impl_->log("speech", std::to_string(index));
    impl_->play_clip(true, index);
    const auto &pcm = impl_->load_clip(true, index);
    return static_cast<std::uint32_t>(pcm.size() / channels * 1000 / sample_rate);
}

void Audio::stop_voice() {
    impl_->speech_queue.clear();
    std::erase_if(impl_->clips, [](const Impl::Clip &clip) { return clip.voice; });
}

bool Audio::voice_active() const noexcept {
    return std::any_of(impl_->clips.begin(), impl_->clips.end(), [](const Impl::Clip &clip) { return clip.voice; });
}

void Audio::set_music(bool enabled) {
    if (impl_->music_on == enabled)
        return;
    impl_->music_on = enabled;
    impl_->log("music_enabled", enabled ? "on" : "off");
}

void Audio::select_music(unsigned track) {
    if (track > 8) {
        throw std::invalid_argument("normal MIDI playlist selection must be 0 through 8");
    }
    impl_->track = track == 0 ? impl_->track % 8 + 1 : track;
    impl_->music_stack.clear();
    impl_->context.reset();
    impl_->log("music_select", impl_->current_file());
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
    return impl_->context;
}
std::string Audio::music_name() const {
    return std::string(impl_->current_file());
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
    return impl_->synth != nullptr;
}
bool Audio::music_enabled() const noexcept {
    return impl_->music_on;
}
unsigned Audio::music_track() const noexcept {
    return impl_->track;
}
Statistics Audio::statistics() const noexcept {
    return impl_->stats;
}
int Audio::queued_bytes() const {
    return impl_->stream ? SDL_GetAudioStreamQueued(impl_->stream.get()) : 0;
}
const std::string &Audio::music_diagnostic() const noexcept {
    return impl_->diagnostic;
}

} // namespace rich4::audio
