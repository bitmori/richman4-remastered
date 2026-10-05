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
constexpr std::array<std::string_view, 8> music_files{"rich08.mid", "rich16.mid", "rich17.mid",
                                                      "rich18.mid", "rich19.mid", "rich20.mid",
                                                      "rich21.mid", "rich22.mid"};

std::filesystem::path asset_path(const std::filesystem::path& directory, std::string_view name) {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        auto candidate = entry.path().filename().string();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(),
                       [](unsigned char value) { return char(std::tolower(value)); });
        if (candidate == name && entry.is_regular_file()) {
            return entry.path();
        }
    }
    throw assets::AssetError("missing audio asset: " + (directory / name).string());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
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
    if (!input.read(reinterpret_cast<char*>(result.data()), length)) {
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
    ~AudioSubsystem() { SDL_QuitSubSystem(SDL_INIT_AUDIO); }
};

struct SdlFree {
    void operator()(void* pointer) const { SDL_free(pointer); }
};

std::vector<float> decode_wave(std::span<const std::uint8_t> bytes) {
    auto* input = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!input) {
        throw sdl_error("open WAV memory");
    }
    SDL_AudioSpec source_format{};
    Uint8* wav_pointer = nullptr;
    Uint32 wav_length = 0;
    if (!SDL_LoadWAV_IO(input, true, &source_format, &wav_pointer, &wav_length)) {
        throw sdl_error("decode original WAV");
    }
    std::unique_ptr<Uint8, SdlFree> wav(wav_pointer);
    if (wav_length > std::uint32_t(std::numeric_limits<int>::max())) {
        throw assets::AssetError("WAV exceeds SDL conversion length range");
    }
    Uint8* converted_pointer = nullptr;
    int converted_length = 0;
    if (!SDL_ConvertAudioSamples(&source_format, wav.get(), static_cast<int>(wav_length),
                                 &mixer_format, &converted_pointer, &converted_length)) {
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

struct Audio::Impl {
    bool enabled{};
    bool music_on{true};
    unsigned track{1};
    std::string diagnostic;
    Statistics stats;
    std::filesystem::path directory;
    std::unique_ptr<AudioSubsystem> subsystem;
    std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> stream{
        nullptr, SDL_DestroyAudioStream};
    std::unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings{
        nullptr, delete_fluid_settings};
    std::unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth{nullptr,
                                                                        delete_fluid_synth};
    std::unique_ptr<fluid_player_t, decltype(&delete_fluid_player)> player{nullptr,
                                                                           delete_fluid_player};
    std::vector<std::uint8_t> midi_bytes;
    std::optional<assets::Archive> effects;
    std::optional<assets::Archive> voices;
    std::map<std::pair<bool, std::size_t>, std::vector<float>> samples;

    struct Clip {
        const std::vector<float>* samples;
        std::size_t position{};
        bool voice{};
    };
    std::vector<Clip> clips;

    Impl(const std::filesystem::path& assets, bool requested, const std::filesystem::path& bank)
        : enabled(requested), directory(assets) {
        if (!enabled) {
            return;
        }
        subsystem = std::make_unique<AudioSubsystem>();
        stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mixer_format,
                                               nullptr, nullptr));
        if (!stream) {
            throw sdl_error("open SDL audio stream");
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
        player.reset();
        synth.reset();
        settings.reset();
    }

    void initialize_music(std::filesystem::path bank) {
        if (bank.empty()) {
            if (const auto* environment = std::getenv("RICH4_SOUNDFONT")) {
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
            diagnostic =
                "MIDI soundbank unavailable; set RICH4_SOUNDFONT to a General MIDI SF2/DLS file";
            return;
        }
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

    void start_track() {
        if (!synth) {
            return;
        }
        player.reset();
        fluid_synth_system_reset(synth.get());
        midi_bytes = read_bytes(asset_path(directory, music_files[track - 1]));
        player.reset(new_fluid_player(synth.get()));
        if (!player ||
            fluid_player_add_mem(player.get(), midi_bytes.data(), midi_bytes.size()) != FLUID_OK ||
            fluid_player_play(player.get()) != FLUID_OK) {
            throw std::runtime_error("cannot start original MIDI track " + std::to_string(track));
        }
    }

    const std::vector<float>& load_clip(bool voice, std::size_t index) {
        const auto key = std::pair{voice, index};
        if (const auto found = samples.find(key); found != samples.end()) {
            return found->second;
        }
        auto& archive = voice ? voices : effects;
        if (!archive) {
            archive.emplace(asset_path(directory, voice ? "speaking.mkf" : "effect.mkf"));
        }
        auto pcm = decode_wave(archive->decode(index));
        return samples.emplace(key, std::move(pcm)).first->second;
    }

    void play_clip(bool voice, std::size_t index) {
        if (!enabled) {
            return;
        }
        const auto& pcm = load_clip(voice, index);
        if (voice) {
            std::erase_if(clips, [](const Clip& clip) { return clip.voice; });
            ++stats.voices_started;
        } else {
            ++stats.effects_started;
        }
        // Bound bursts of user input. Voice always has its own single slot;
        // effects can overlap, with the oldest effect replaced after 16 voices.
        if (!voice && std::count_if(clips.begin(), clips.end(),
                                    [](const Clip& clip) { return !clip.voice; }) >= 16) {
            clips.erase(std::find_if(clips.begin(), clips.end(),
                                     [](const Clip& clip) { return !clip.voice; }));
        }
        clips.push_back({&pcm, 0, voice});
    }

    void render_block(std::span<float> output) {
        std::fill(output.begin(), output.end(), 0.0F);
        if (music_on && player) {
            if (fluid_player_get_status(player.get()) == FLUID_PLAYER_DONE) {
                track = track % 8 + 1;
                start_track();
            }
            if (fluid_synth_write_float(synth.get(), block_frames, output.data(), 0, 2,
                                        output.data(), 1, 2) != FLUID_OK) {
                throw std::runtime_error("MIDI software rendering failed");
            }
            stats.music_frames += block_frames;
        }
        for (auto& clip : clips) {
            const auto available = clip.samples->size() - clip.position;
            const auto count = std::min(output.size(), available);
            for (std::size_t i = 0; i < count; ++i) {
                output[i] += (*clip.samples)[clip.position + i];
            }
            clip.position += count;
            if (clip.position == clip.samples->size()) {
                ++stats.clips_completed;
            }
        }
        std::erase_if(clips,
                      [](const Clip& clip) { return clip.position == clip.samples->size(); });
        for (auto& sample : output) {
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
        int queued = SDL_GetAudioStreamQueued(stream.get());
        if (queued < 0) {
            throw sdl_error("query SDL audio queue");
        }
        std::array<float, block_frames * channels> block;
        // Bounded catch-up prevents a broken device or long stall from causing
        // an unbounded main-thread loop. MIDI time follows rendered samples.
        for (int iteration = 0; iteration < 8 && queued < queue_target_frames * bytes_per_frame;
             ++iteration) {
            render_block(block);
            const int byte_count = int(block.size() * sizeof(float));
            if (!SDL_PutAudioStreamData(stream.get(), block.data(), byte_count)) {
                throw sdl_error("queue native mixed audio");
            }
            queued += byte_count;
        }
    }
};

Audio::Audio(const std::filesystem::path& directory, bool enabled,
             const std::filesystem::path& soundbank)
    : impl_(std::make_unique<Impl>(directory, enabled, soundbank)) {}
Audio::~Audio() = default;
Audio::Audio(Audio&&) noexcept = default;
Audio& Audio::operator=(Audio&&) noexcept = default;

void Audio::play_sound(std::size_t index) { impl_->play_clip(false, index); }

void Audio::play_voice(std::uint8_t character, std::uint16_t category) {
    if (character >= 12 || category >= 27) {
        throw std::invalid_argument("voice character/category outside original event table");
    }
    impl_->play_clip(true, 1050U + character * 27U + category);
}

void Audio::set_music(bool enabled) { impl_->music_on = enabled; }

void Audio::select_music(unsigned track) {
    if (track > 8) {
        throw std::invalid_argument("normal MIDI playlist selection must be 0 through 8");
    }
    impl_->track = track == 0 ? impl_->track % 8 + 1 : track;
    impl_->start_track();
}

void Audio::update() { impl_->update(); }
bool Audio::enabled() const noexcept { return impl_->enabled; }
bool Audio::music_available() const noexcept { return impl_->synth != nullptr; }
bool Audio::music_enabled() const noexcept { return impl_->music_on; }
unsigned Audio::music_track() const noexcept { return impl_->track; }
Statistics Audio::statistics() const noexcept { return impl_->stats; }
int Audio::queued_bytes() const {
    return impl_->stream ? SDL_GetAudioStreamQueued(impl_->stream.get()) : 0;
}
const std::string& Audio::music_diagnostic() const noexcept { return impl_->diagnostic; }

} // namespace rich4::audio
