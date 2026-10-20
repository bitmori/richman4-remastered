// SPDX-License-Identifier: GPL-3.0-or-later
#include "rich4_android.hpp"
#include "platform.hpp" // tools/

#include <SDL3/SDL.h>
#include <android/log.h>
#include <jni.h>
#include <iconv.h> // GNU libiconv (see CMakeLists.txt)
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace rich4::android_platform {
namespace {
namespace fs = std::filesystem;

constexpr const char *tag = "rich4";
constexpr const char *packaged = "rich4-data/"; // APK assets/ prefix written by app/build.gradle.kts

void log(const std::string &message) {
    __android_log_write(ANDROID_LOG_INFO, tag, message.c_str());
}

// std::cerr diagnostics from the shared game code otherwise vanish on Android (stdout is left
// alone: SDL already mirrors its own log there, which would duplicate every line).
void route_stderr_to_logcat() {
    static bool started = false;
    if (started)
        return;
    started = true;
    int pipe_ends[2];
    if (pipe(pipe_ends) != 0)
        return;
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(pipe_ends[1], STDERR_FILENO);
    std::thread([read_end = pipe_ends[0]] {
        std::array<char, 1024> buffer;
        std::string line;
        for (ssize_t count; (count = read(read_end, buffer.data(), buffer.size())) > 0;)
            for (ssize_t i = 0; i < count; ++i)
                if (buffer[std::size_t(i)] == '\n') {
                    __android_log_write(ANDROID_LOG_WARN, tag, line.c_str());
                    line.clear();
                } else
                    line += buffer[std::size_t(i)];
    }).detach();
}

// Proves the bundled GNU libiconv (not bionic, which lacks BIG5) is what the game links against.
void log_big5_self_test() {
    char big5[] = "\xA4\x6A\xB4\x49\xAF\xCE"; // 大富翁
    char utf8[32] = {};
    char *source = big5, *destination = utf8;
    std::size_t source_left = 6, destination_left = sizeof utf8 - 1;
    const iconv_t converter = iconv_open("UTF-8", "BIG5");
    const bool ok = converter != iconv_t(-1) &&
                    iconv(converter, &source, &source_left, &destination, &destination_left) != std::size_t(-1);
    if (converter != iconv_t(-1))
        iconv_close(converter);
    log(ok ? std::string("Big5 self-test: ") + utf8 : std::string("Big5 self-test FAILED"));
}

std::string load_packaged_text(const std::string &name) {
    std::size_t size = 0;
    std::unique_ptr<char, decltype(&SDL_free)> data(
        static_cast<char *>(SDL_LoadFile((packaged + name).c_str(), &size)), SDL_free);
    if (!data)
        throw std::runtime_error("APK is missing " + name + ": " + SDL_GetError());
    return {data.get(), size};
}

void extract(const std::string &name, const fs::path &destination) {
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> input(SDL_IOFromFile((packaged + name).c_str(), "rb"),
                                                                SDL_CloseIO);
    if (!input)
        throw std::runtime_error("Cannot open packaged " + name + ": " + SDL_GetError());
    fs::create_directories(destination.parent_path());
    const auto temporary = fs::path(destination.string() + ".tmp");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    std::vector<char> buffer(1 << 20);
    for (std::size_t count; (count = SDL_ReadIO(input.get(), buffer.data(), buffer.size())) > 0;)
        output.write(buffer.data(), std::streamsize(count));
    if (SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_ERROR)
        throw std::runtime_error("Cannot read packaged " + name + ": " + SDL_GetError());
    output.close();
    if (!output)
        throw std::runtime_error("Cannot write " + destination.string() + " (storage full?)");
    fs::rename(temporary, destination);
}

// Extracts on first launch and whenever the APK carries different game files.
fs::path prepare_assets(const fs::path &root) {
    const auto directory = root / "game-data";
    const auto stamp_file = directory / ".stamp";
    std::istringstream manifest(load_packaged_text("manifest.txt"));
    std::string stamp;
    std::getline(manifest, stamp);
    std::string installed;
    if (std::ifstream existing(stamp_file); existing)
        std::getline(existing, installed);
    if (installed == stamp) {
        log("Game data up to date: " + directory.string());
        return directory;
    }
    log("Extracting game data (" + stamp + ") to " + directory.string());
    const auto started = SDL_GetTicks();
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    for (std::string name; std::getline(manifest, name);)
        if (!name.empty())
            extract(name, directory / name);
    std::ofstream(stamp_file) << stamp << '\n';
    log("Extracted game data in " + std::to_string(SDL_GetTicks() - started) + " ms");
    return directory;
}

} // namespace

Paths prepare() {
    route_stderr_to_logcat();
    log_big5_self_test();
    const char *internal = SDL_GetAndroidInternalStoragePath();
    if (!internal)
        throw std::runtime_error(std::string("No internal storage path: ") + SDL_GetError());
    Paths paths;
    const auto data = prepare_assets(internal);
    paths.assets = data / "game";

    std::unique_ptr<char, decltype(&SDL_free)> preferences(SDL_GetPrefPath("divby0", "rich4"), SDL_free);
    if (!preferences)
        throw std::runtime_error(std::string("No preference path: ") + SDL_GetError());
    paths.save_directory = fs::path(preferences.get()) / "saves";

    // Bundled Traditional Chinese font first (optional at build time), then system CJK fonts.
    for (const fs::path candidate : {data / "font.otf", fs::path("/system/fonts/NotoSerifCJK-Regular.ttc"),
                                     fs::path("/system/fonts/NotoSansCJK-Regular.ttc"),
                                     fs::path("/system/fonts/DroidSansFallback.ttf")})
        if (fs::is_regular_file(candidate)) {
            paths.font = candidate;
            break;
        }
    if (paths.font.empty())
        throw std::runtime_error("No Chinese font found on this device");

    const auto soundfont = data / "TimGM6mb.sf2";
    if (fs::is_regular_file(soundfont) && !std::getenv("RICH4_SOUNDFONT"))
        setenv("RICH4_SOUNDFONT", soundfont.c_str(), 1);

    log("Assets " + paths.assets.string() + ", font " + paths.font.string() + ", saves " +
        paths.save_directory.string() + ", soundfont " +
        (fs::is_regular_file(soundfont) ? soundfont.string() : std::string("none")));
    return paths;
}

} // namespace rich4::android_platform

namespace rich4::platform {

// Rich4Activity.hapticTap(): View.performHapticFeedback(KEYBOARD_TAP) on the UI thread. It needs
// no VIBRATE permission and follows the phone's own touch-feedback setting.
void haptic_tap() {
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return;
    static jmethodID method = nullptr;
    if (!method) {
        const jclass type = env->GetObjectClass(activity);
        method = env->GetMethodID(type, "hapticTap", "()V");
        env->DeleteLocalRef(type);
    }
    if (method)
        env->CallVoidMethod(activity, method);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteLocalRef(activity);
}

} // namespace rich4::platform
