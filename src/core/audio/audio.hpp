#pragma once

#include "../types.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace a2e {

class Audio {
public:
  static constexpr int BUFFER_SIZE = 4096;
  static constexpr int MAX_TOGGLES = 8192;

  Audio();

  // Speaker toggle (called when $C030 is accessed)
  void toggleSpeaker(uint64_t cycleCount);

  // Generate audio samples
  // Returns the number of samples generated
  int generateSamples(float *buffer, int sampleCount, uint64_t currentCycle);

  // Reset
  void reset();

  // Volume control (0.0 - 1.0)
  void setVolume(float volume) { volume_ = volume; }
  float getVolume() const { return volume_; }

  // Mute control
  void setMuted(bool muted) { muted_ = muted; }
  bool isMuted() const { return muted_; }

private:
  // Speaker state
  bool speakerState_ = false;

  // Toggle event recording
  std::vector<uint64_t> toggleCycles_;
  size_t toggleReadIndex_ = 0;

  // Audio generation state
  uint64_t lastSampleCycle_ = 0;

  // Simple low-pass filter state
  float filterState_ = 0.0f;
  static constexpr float FILTER_ALPHA = 0.4f;

  // Volume
  float volume_ = 0.5f;
  bool muted_ = false;

  // DC offset removal
  float dcOffset_ = 0.0f;
  static constexpr float DC_ALPHA = 0.995f;
};

} // namespace a2e
