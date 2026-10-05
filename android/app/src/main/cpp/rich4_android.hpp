// SPDX-License-Identifier: GPL-3.0-or-later
// Android-only platform glue for tools/native_game.cpp (compiled only into the APK).
#pragma once

#include <filesystem>

namespace rich4::android_platform {

struct Paths {
    std::filesystem::path assets, font, save_directory;
};

// Call once from arguments(), before SDL_Init. Routes stdout/stderr to logcat,
// extracts the game files packaged in the APK to internal storage when the
// packaged manifest stamp changes, and points FluidSynth at the bundled bank
// through RICH4_SOUNDFONT. Throws std::runtime_error on failure.
Paths prepare();

} // namespace rich4::android_platform
