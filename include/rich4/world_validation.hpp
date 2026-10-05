// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rich4/world.hpp"

namespace rich4 {
// Validates values consumed by native gameplay and rendering, across the whole
// world rather than only the camera viewport. Does not mutate/normalize state.
// Throw std::invalid_argument with the table/index/field on failure. Call before
// replacing a live session or writing an imported world to a native save slot.
// Binary archival decode/encode deliberately remains more permissive.
void validate_playable_world(const World &);
} // namespace rich4
