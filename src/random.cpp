// SPDX-License-Identifier: GPL-3.0-or-later
// Original Watcom CRT routines at 0x456f2d / 0x456f50; see migration 008.
#include "rich4/random.hpp"

namespace rich4 {

std::uint16_t Random::next() noexcept {
    // Unsigned operations deliberately wrap at 32 bits on every architecture.
    state_ = state_ * 1103515245U + 12345U;
    return static_cast<std::uint16_t>((state_ >> 16U) & 0x7fffU);
}

} // namespace rich4
