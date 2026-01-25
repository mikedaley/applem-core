#include "audio.hpp"
#include "../mockingboard/mockingboard.hpp"
#include <cmath>

namespace a2e {

Audio::Audio() {
  toggleCycles_.reserve(MAX_TOGGLES);
  reset();
}

void Audio::reset() {
  speakerState_ = false;
  toggleCycles_.clear();
  toggleReadIndex_ = 0;
  lastSampleCycle_ = 0;
  filterState_ = 0.0f;
  dcOffset_ = 0.0f;
}

void Audio::toggleSpeaker(uint64_t cycleCount) {
  // Record the toggle event
  if (toggleCycles_.size() < MAX_TOGGLES) {
    toggleCycles_.push_back(cycleCount);
  }
}

int Audio::generateSamples(float *buffer, int sampleCount,
                           uint64_t currentCycle) {
  if (muted_ || sampleCount <= 0) {
    // Fill with silence
    for (int i = 0; i < sampleCount; i++) {
      buffer[i] = 0.0f;
    }
    // Clear toggle events
    toggleCycles_.clear();
    toggleReadIndex_ = 0;
    lastSampleCycle_ = currentCycle;
    return sampleCount;
  }

  // Calculate the cycle range for this buffer
  uint64_t startCycle = lastSampleCycle_;
  uint64_t endCycle = currentCycle;
  uint64_t totalCycles = endCycle - startCycle;

  if (totalCycles == 0) {
    totalCycles = static_cast<uint64_t>(sampleCount * CYCLES_PER_SAMPLE);
    startCycle = endCycle - totalCycles;
  }

  double cyclesPerSample = static_cast<double>(totalCycles) / sampleCount;

  // Process each sample
  for (int i = 0; i < sampleCount; i++) {
    // Calculate the cycle position for this sample
    uint64_t sampleCycleStart =
        startCycle + static_cast<uint64_t>(i * cyclesPerSample);
    uint64_t sampleCycleEnd =
        startCycle + static_cast<uint64_t>((i + 1) * cyclesPerSample);

    // Count toggles and calculate average state during this sample period
    float highTime = 0.0f;
    float lowTime = 0.0f;
    uint64_t lastCycle = sampleCycleStart;
    bool currentState = speakerState_;

    // Process any toggle events that fall within this sample
    while (toggleReadIndex_ < toggleCycles_.size() &&
           toggleCycles_[toggleReadIndex_] < sampleCycleEnd) {

      uint64_t toggleCycle = toggleCycles_[toggleReadIndex_];

      if (toggleCycle >= sampleCycleStart) {
        // Accumulate time in current state
        float cycles = static_cast<float>(toggleCycle - lastCycle);
        if (currentState) {
          highTime += cycles;
        } else {
          lowTime += cycles;
        }
        lastCycle = toggleCycle;
      }

      // Toggle the state
      currentState = !currentState;
      toggleReadIndex_++;
    }

    // Accumulate remaining time in final state
    float cycles = static_cast<float>(sampleCycleEnd - lastCycle);
    if (currentState) {
      highTime += cycles;
    } else {
      lowTime += cycles;
    }

    // Update speaker state for next sample
    speakerState_ = currentState;

    // Calculate raw sample value based on duty cycle
    float totalTime = highTime + lowTime;
    float rawValue = 0.0f;
    if (totalTime > 0) {
      rawValue = (highTime - lowTime) / totalTime;
    }

    // Apply low-pass filter for smoother sound
    filterState_ = filterState_ + FILTER_ALPHA * (rawValue - filterState_);

    // Remove DC offset
    dcOffset_ = DC_ALPHA * dcOffset_ + (1.0f - DC_ALPHA) * filterState_;
    float dcCorrected = filterState_ - dcOffset_;

    // Apply volume and clamp
    float sample = dcCorrected * volume_;
    sample = std::max(-1.0f, std::min(1.0f, sample));

    buffer[i] = sample;
  }

  // Clear processed toggles
  if (toggleReadIndex_ > 0) {
    toggleCycles_.erase(toggleCycles_.begin(),
                        toggleCycles_.begin() + toggleReadIndex_);
    toggleReadIndex_ = 0;
  }

  lastSampleCycle_ = currentCycle;

  // Mix in Mockingboard audio if present
  if (mockingboard_) {
    std::vector<float> mbBuffer(sampleCount);
    mockingboard_->generateSamples(mbBuffer.data(), sampleCount, AUDIO_SAMPLE_RATE);
    for (int i = 0; i < sampleCount; i++) {
      // Mix speaker and Mockingboard
      // Mockingboard output is normalized per-PSG (~0.33 max per channel after /3)
      // Use 1.5x boost to avoid clipping while still being audible
      float mbSample = mbBuffer[i] * 1.5f * volume_;
      // Additive mix - speaker clicks are transient, MB is sustained
      buffer[i] = buffer[i] + mbSample;
      // Clamp to valid range
      buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
    }
  }

  return sampleCount;
}

int Audio::generateStereoSamples(float *buffer, int sampleCount,
                                  uint64_t currentCycle) {
  if (muted_ || sampleCount <= 0) {
    // Fill with silence (interleaved stereo)
    for (int i = 0; i < sampleCount * 2; i++) {
      buffer[i] = 0.0f;
    }
    toggleCycles_.clear();
    toggleReadIndex_ = 0;
    lastSampleCycle_ = currentCycle;
    return sampleCount;
  }

  // First generate mono speaker samples into a temp buffer
  std::vector<float> speakerBuffer(sampleCount);

  uint64_t startCycle = lastSampleCycle_;
  uint64_t endCycle = currentCycle;
  uint64_t totalCycles = endCycle - startCycle;

  if (totalCycles == 0) {
    totalCycles = static_cast<uint64_t>(sampleCount * CYCLES_PER_SAMPLE);
    startCycle = endCycle - totalCycles;
  }

  double cyclesPerSample = static_cast<double>(totalCycles) / sampleCount;

  // Process each sample for speaker
  for (int i = 0; i < sampleCount; i++) {
    uint64_t sampleCycleStart =
        startCycle + static_cast<uint64_t>(i * cyclesPerSample);
    uint64_t sampleCycleEnd =
        startCycle + static_cast<uint64_t>((i + 1) * cyclesPerSample);

    float highTime = 0.0f;
    float lowTime = 0.0f;
    uint64_t lastCycle = sampleCycleStart;
    bool currentState = speakerState_;

    while (toggleReadIndex_ < toggleCycles_.size() &&
           toggleCycles_[toggleReadIndex_] < sampleCycleEnd) {
      uint64_t toggleCycle = toggleCycles_[toggleReadIndex_];

      if (toggleCycle >= sampleCycleStart) {
        float cycles = static_cast<float>(toggleCycle - lastCycle);
        if (currentState) {
          highTime += cycles;
        } else {
          lowTime += cycles;
        }
        lastCycle = toggleCycle;
      }

      currentState = !currentState;
      toggleReadIndex_++;
    }

    float cycles = static_cast<float>(sampleCycleEnd - lastCycle);
    if (currentState) {
      highTime += cycles;
    } else {
      lowTime += cycles;
    }

    speakerState_ = currentState;

    float totalTime = highTime + lowTime;
    float rawValue = 0.0f;
    if (totalTime > 0) {
      rawValue = (highTime - lowTime) / totalTime;
    }

    filterState_ = filterState_ + FILTER_ALPHA * (rawValue - filterState_);
    dcOffset_ = DC_ALPHA * dcOffset_ + (1.0f - DC_ALPHA) * filterState_;
    float dcCorrected = filterState_ - dcOffset_;
    float sample = dcCorrected * volume_;
    sample = std::max(-1.0f, std::min(1.0f, sample));

    speakerBuffer[i] = sample;
  }

  if (toggleReadIndex_ > 0) {
    toggleCycles_.erase(toggleCycles_.begin(),
                        toggleCycles_.begin() + toggleReadIndex_);
    toggleReadIndex_ = 0;
  }

  lastSampleCycle_ = currentCycle;

  // Get stereo Mockingboard samples
  std::vector<float> mbBuffer(sampleCount * 2);
  if (mockingboard_) {
    mockingboard_->generateStereoSamples(mbBuffer.data(), sampleCount, AUDIO_SAMPLE_RATE);
  }

  // Mix speaker (center) with Mockingboard stereo
  for (int i = 0; i < sampleCount; i++) {
    float speakerSample = speakerBuffer[i];

    // Mockingboard: PSG1 left, PSG2 right with 1.5x boost
    float mbLeft = mbBuffer[i * 2] * 1.5f * volume_;
    float mbRight = mbBuffer[i * 2 + 1] * 1.5f * volume_;

    // Mix: speaker goes to both channels
    float left = speakerSample + mbLeft;
    float right = speakerSample + mbRight;

    // Clamp to valid range
    buffer[i * 2] = std::max(-1.0f, std::min(1.0f, left));
    buffer[i * 2 + 1] = std::max(-1.0f, std::min(1.0f, right));
  }

  return sampleCount;
}

} // namespace a2e
