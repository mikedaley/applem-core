/*
 * peak_limiter.hpp - Holds a stereo mix under full scale without clipping it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace a2e {

// A mix of sources that can each reach full scale on their own can add up to
// more than full scale together, and whatever is past it is clipped flat at
// the device: a buzz on every peak. This turns the mix down only while a
// peak would go past the ceiling, both channels together so the stereo image
// does not move, and brings it back up over a release slow enough not to be
// heard pumping. Below the ceiling it is not there at all.
//
// The gain drops at once, on the sample that would have gone over, so no
// sample ever leaves above the ceiling; with no look-ahead, that one sample
// is the whole of the attack.
class PeakLimiter {
public:
  static constexpr float CEILING = 0.98f;         // a little under full scale
  static constexpr float RELEASE_SECONDS = 0.25f; // back to unity, about

  void setSampleRate(float rate) {
    release_ = 1.0f - std::exp(-1.0f / (RELEASE_SECONDS * std::max(1.0f, rate)));
  }

  void process(float *stereo, int frames) {
    for (int i = 0; i < frames; i++) {
      float &left = stereo[i * 2];
      float &right = stereo[i * 2 + 1];
      gain_ += (1.0f - gain_) * release_;
      const float peak = std::max(std::fabs(left), std::fabs(right));
      if (peak * gain_ > CEILING) gain_ = CEILING / peak;
      left *= gain_;
      right *= gain_;
    }
  }

  void reset() { gain_ = 1.0f; }
  float gain() const { return gain_; }

private:
  float gain_ = 1.0f;
  float release_ = 1.0f - std::exp(-1.0f / (RELEASE_SECONDS * 48000.0f)); // AUDIO_SAMPLE_RATE
};

} // namespace a2e
