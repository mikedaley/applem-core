/*
 * test_peak_limiter.cpp - The mix is held under full scale, and left alone
 * below it
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "peak_limiter.hpp"

#include <cmath>
#include <vector>

using namespace a2e;

namespace {
std::vector<float> tone(float amplitude, int frames, float rate = 48000.0f) {
  std::vector<float> stereo(static_cast<size_t>(frames) * 2);
  for (int i = 0; i < frames; i++) {
    const float s = amplitude * std::sin(2.0f * 3.14159265f * 440.0f * i / rate);
    stereo[i * 2] = s;
    stereo[i * 2 + 1] = s * 0.5f;
  }
  return stereo;
}
} // namespace

TEST_CASE("A peak limiter leaves a mix under the ceiling untouched", "[audio][limiter]") {
  PeakLimiter limiter;
  limiter.setSampleRate(48000.0f);
  std::vector<float> stereo = tone(0.9f, 4800);
  const std::vector<float> before = stereo;
  limiter.process(stereo.data(), 4800);
  REQUIRE(stereo == before);
  REQUIRE(limiter.gain() == 1.0f);
}

TEST_CASE("A peak limiter never lets a sample past the ceiling", "[audio][limiter]") {
  // Four times full scale, as the Ensoniq's 32 oscillators can be together.
  PeakLimiter limiter;
  limiter.setSampleRate(48000.0f);
  std::vector<float> stereo = tone(4.0f, 48000);
  limiter.process(stereo.data(), 48000);
  float loudest = 0;
  for (float s : stereo) loudest = std::max(loudest, std::fabs(s));
  REQUIRE(loudest <= PeakLimiter::CEILING + 1e-6f);
  // Turned down, not flattened: the waveform still reaches the ceiling, and
  // both channels by the same amount, so the right is still half the left.
  REQUIRE(loudest > PeakLimiter::CEILING * 0.9f);
  for (size_t i = 0; i < stereo.size(); i += 2) REQUIRE(stereo[i + 1] == Approx(stereo[i] * 0.5f).margin(1e-6f));
}

TEST_CASE("A peak limiter comes back to unity once the peaks stop", "[audio][limiter]") {
  PeakLimiter limiter;
  limiter.setSampleRate(48000.0f);
  std::vector<float> loud = tone(2.0f, 4800);
  limiter.process(loud.data(), 4800);
  REQUIRE(limiter.gain() < 0.6f);
  // Two seconds of quiet: eight release times.
  std::vector<float> quiet(48000 * 2 * 2, 0.0f);
  limiter.process(quiet.data(), 48000 * 2);
  REQUIRE(limiter.gain() > 0.999f);
}
