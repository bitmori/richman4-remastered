// SPDX-License-Identifier: GPL-3.0-or-later
// iOS platform services: bundle paths for tools/native_game.cpp and haptics for tools/platform.hpp.
#include "rich4_ios.hpp"
#include "platform.hpp" // tools/

#include <SDL3/SDL.h>
#import <UIKit/UIKit.h>
#include <iconv.h> // iOS system libiconv (has BIG5, unlike Android's bionic)

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace rich4::ios_platform {
namespace {
namespace fs = std::filesystem;

void log(const std::string &message) {
    SDL_Log("rich4: %s", message.c_str());
}

// The game decodes every original string from Big5 through iconv (tools/native_ui.hpp).
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

} // namespace

Paths prepare() {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2"); // dimmed; edge swipes need a second swipe
    log_big5_self_test();

    const char *base = SDL_GetBasePath(); // the .app bundle (owned by SDL)
    if (!base)
        throw std::runtime_error(std::string("No bundle path: ") + SDL_GetError());
    const fs::path bundle = base;
    Paths paths;
    paths.assets = bundle / "game"; // copied by ios/copy_resources.cmake
    if (!fs::is_regular_file(paths.assets / "Data.mkf"))
        throw std::runtime_error("App bundle is missing the game files: " + paths.assets.string());

    std::unique_ptr<char, decltype(&SDL_free)> preferences(SDL_GetPrefPath("divby0", "rich4"), SDL_free);
    if (!preferences)
        throw std::runtime_error(std::string("No preference path: ") + SDL_GetError());
    paths.save_directory = fs::path(preferences.get()) / "saves";

    // Bundled Noto Serif CJK TC first; iOS system CJK fonts are not at stable public paths, the
    // PingFang locations below are only a best-effort fallback for builds without the font.
    for (const fs::path candidate : {bundle / "NotoSerifCJKtc-Regular.otf",
                                     fs::path("/System/Library/Fonts/Core/PingFang.ttc"),
                                     fs::path("/System/Library/Fonts/LanguageSupport/PingFang.ttc")})
        if (fs::is_regular_file(candidate)) {
            paths.font = candidate;
            break;
        }
    if (paths.font.empty())
        throw std::runtime_error("No Chinese font found in the app bundle");

    const auto soundfont = bundle / "TimGM6mb.sf2";
    if (fs::is_regular_file(soundfont) && !std::getenv("RICH4_SOUNDFONT"))
        setenv("RICH4_SOUNDFONT", soundfont.c_str(), 1);

    log("Assets " + paths.assets.string() + ", font " + paths.font.string() + ", saves " +
        paths.save_directory.string() + ", soundfont " +
        (fs::is_regular_file(soundfont) ? soundfont.string() : std::string("none")));
    return paths;
}

} // namespace rich4::ios_platform

namespace rich4::platform {

// A light impact, like a keyboard tap. Called on the main thread (SDL runs the game loop there on
// iOS). The generator is kept and re-prepared so the next tap fires without Taptic Engine latency;
// it follows the system haptics setting and does nothing on devices without a Taptic Engine.
void haptic_tap() {
    static UIImpactFeedbackGenerator *generator =
        [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
    [generator impactOccurred];
    [generator prepare];
}

} // namespace rich4::platform
