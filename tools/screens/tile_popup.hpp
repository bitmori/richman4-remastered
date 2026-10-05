// SPDX-License-Identifier: GPL-3.0-or-later
// Map-tile info popup (docs/ui/main-screen.md §7): fcn_00417559 (rich4.asm:9646) shows the
// D517:q pointer frame with the owner, level/type and toll (or chain count) of the board instance
// under the press; fcn_00417c67 removes it. On touch a tap shows it and the next tap dismisses it.
#pragma once

#include "../native_frame.hpp"
#include "rich4/board_renderer.hpp"
#include "rich4/world.hpp"

#include <cstdint>

namespace rich4::native::tile_popup {

inline constexpr int hit_id = 95; // Board-area tap; registered after every other board control.

struct Popup {
    int x = 0, y = 0;       // Press point (screen); the frame's pointer sits here.
    std::uint16_t code = 0; // Original instance code (see instance_at).
};

// Board instance under a screen point, in the codes of _rich4_get_instance_from_position:
// 1..1999 named road node, 2001.. land, 4001.. facility, 6001.. company, 8001.. landscape,
// 0x8000|(1<<slot) player/special actor, 0x8000|(object+1)<<8 map object; 0 for nothing.
// The original reads an ID buffer of the drawn sprites; this approximates it with sprite boxes
// around each projected anchor (front-most wins) and board::unproject for road tiles.
[[nodiscard]] std::uint16_t instance_at(const World &world, const board::Camera &camera, int x, int y);

// The text lines fcn_00417559 draws for `code` (empty: no popup), with their y offsets from the
// text base. Exposed for review screenshots.
struct Line {
    int dy;
    std::string text;
};
[[nodiscard]] std::vector<Line> lines(const World &world, std::uint16_t code);

void draw(Frame &frame, const World &world, const Popup &popup);

} // namespace rich4::native::tile_popup
