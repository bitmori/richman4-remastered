// SPDX-License-Identifier: GPL-3.0-or-later
// iOS-only platform glue for tools/native_game.cpp (compiled only into the iOS app).
// Mirrors android/app/src/main/cpp/rich4_android.hpp.
#pragma once

#include <filesystem>

namespace rich4::ios_platform {

struct Paths {
    std::filesystem::path assets, font, save_directory;
};

// Call once from arguments(), before SDL_Init. Game files, font and soundfont are read straight
// from the app bundle (no extraction); saves go to SDL_GetPrefPath (Library/Application Support).
// Sets landscape/home-indicator hints, points the MIDI synthesizer at the bundled bank through
// RICH4_SOUNDFONT and logs a Big5 self-test of the system iconv. Throws std::runtime_error.
Paths prepare();

} // namespace rich4::ios_platform
