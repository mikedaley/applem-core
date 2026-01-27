#pragma once

#include "../types.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace a2e {

// Forward declaration
class MockingboardCard;

class Audio {
public:
  static constexpr int BUFFER_SIZE = 4096;
  static constexpr int MAX_TOGGLES = 8192;

  Audio();

  // Speaker toggle (called when $C030 is accessed)
  void toggleSpeaker(uint64_t cycleCount);

  // Generate mono audio samples
  // Returns the number of samples generated
  int generateSamples(float *buffer, int sampleCount, uint64_t currentCycle);

  // Generate stereo audio samples (interleaved L/R)
  // Mockingboard: PSG1 on left, PSG2 on right
  // Speaker: centered (both channels)
  // Returns the number of sample frames generated
  int generateStereoSamples(float *buffer, int sampleCount, uint64_t currentCycle);

  // Reset
  void reset();

  // Volume control (0.0 - 1.0)
  void setVolume(float volume) { volume_ = volume; }
  float getVolume() const { return volume_; }

  // Mute control
  void setMuted(bool muted) { muted_ = muted; }
  bool isMuted() const { return muted_; }

  // Speaker state (for state serialization)
  bool getSpeakerState() const { return speakerState_; }

  // Mockingboard connection
  void setMockingboard(MockingboardCard* mb) { mockingboard_ = mb; }

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

  // Mockingboard
  MockingboardCard* mockingboard_ = nullptr;
};

} // namespace a2e
