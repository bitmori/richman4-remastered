// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace rich4 {

// Rich4 v3.11 uses Watcom's generator, not the MSVCRT rand linked by the
// reconstructed Windows build. Each game owns a stream; no process globals.
class Random {
  public:
    explicit Random(std::uint32_t seed) noexcept : state_(seed) {}
    void seed(std::uint32_t value) noexcept {
        state_ = value;
    }
    [[nodiscard]] std::uint16_t next() noexcept;
    [[nodiscard]] std::uint32_t state() const noexcept {
        return state_;
    }

  private:
    std::uint32_t state_;
};

} // namespace rich4
