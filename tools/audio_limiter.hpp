// SPDX-License-Identifier: GPL-3.0-or-later
// Output limiter of the native mixer (tools/native_audio.cpp). The original game summed its
// DirectSound buffers; our software mix of MIDI + up to 16 effects + a voice can exceed full
// scale, and a hard clamp turns every such overlap into harsh square-wave distortion.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace rich4::audio {

// Stereo-linked peak limiter: instant attack (the gain for a frame is computed from that frame's
// own peak, so nothing ever exceeds the ceiling), exponential release so the gain glides back to
// unity instead of pumping. Signals that stay at or below the ceiling pass through bit-exact.
class Limiter {
  public:
    static constexpr float default_ceiling = 0.891251F; // -1 dBFS

    explicit Limiter(float sample_rate, float ceiling = default_ceiling, float release_ms = 150.0F)
        : ceiling_(ceiling), release_(std::exp(-1.0F / (release_ms * 0.001F * sample_rate))) {}

    // Processes interleaved stereo in place; returns how many samples were attenuated.
    std::size_t process(std::span<float> interleaved) {
        std::size_t limited = 0;
        for (std::size_t i = 0; i + 1 < interleaved.size(); i += 2) {
            float &left = interleaved[i];
            float &right = interleaved[i + 1];
            if (!std::isfinite(left))
                left = 0.0F;
            if (!std::isfinite(right))
                right = 0.0F;
            const float peak = std::max(std::abs(left), std::abs(right));
            envelope_ = std::max(peak, envelope_ * release_);
            if (envelope_ > ceiling_) {
                const float gain = ceiling_ / envelope_;
                // Final safety clamp guards float rounding of peak * (ceiling / envelope).
                left = std::clamp(left * gain, -ceiling_, ceiling_);
                right = std::clamp(right * gain, -ceiling_, ceiling_);
                limited += 2;
            }
        }
        return limited;
    }
    [[nodiscard]] float ceiling() const noexcept {
        return ceiling_;
    }
    [[nodiscard]] float gain() const noexcept { // current gain for a full-scale-free signal
        return envelope_ > ceiling_ ? ceiling_ / envelope_ : 1.0F;
    }

  private:
    float ceiling_;
    float release_;
    float envelope_{};
};

} // namespace rich4::audio
