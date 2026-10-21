// SPDX-License-Identifier: GPL-3.0-or-later
// Small per-platform services for the native game. Each build links exactly one
// implementation: tools/platform_desktop.cpp (macOS), android/.../rich4_android.cpp,
// or the iOS shell.
#pragma once

// RICH4_TOUCH (CMake option, forced on for Android/iOS): touch adaptations of the original mouse
// UI -- press instead of hover, tap targets instead of right-click. Off (the default), the desktop
// build keeps the original mouse behaviour.
#ifndef RICH4_TOUCH
#define RICH4_TOUCH 0
#endif

namespace rich4::platform {

inline constexpr bool touch_ui = RICH4_TOUCH != 0;

// A light "key tap" haptic for a button press; a no-op where there is no haptic engine.
void haptic_tap();

} // namespace rich4::platform
