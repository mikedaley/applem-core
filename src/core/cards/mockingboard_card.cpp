/*
 * mockingboard_card.cpp - Mockingboard sound card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "mockingboard_card.hpp"
#include <cstring>
#include <algorithm>

namespace a2e {

MockingboardCard::MockingboardCard() {
    // Set IDs for debug logging
    psg1_.setPsgId(1);
    psg2_.setPsgId(2);
    via1_.setViaId(1);
    via2_.setViaId(2);

    // Connect PSGs to VIAs
    via1_.connectPSG(&psg1_);
    via2_.connectPSG(&psg2_);

    reset();
}

void MockingboardCard::setDebugLogging(bool enabled) {
    AY8910::setDebugLogging(enabled);
    VIA6522::setDebugLogging(enabled);
}

uint8_t MockingboardCard::readIO(uint8_t offset) {
    // Mockingboard doesn't use I/O space ($C0C0-$C0CF)
    // It uses ROM space for VIA registers
    (void)offset;
    return 0xFF;
}

void MockingboardCard::writeIO(uint8_t offset, uint8_t value) {
    // Mockingboard doesn't use I/O space
    (void)offset;
    (void)value;
}

uint8_t MockingboardCard::peekIO(uint8_t offset) const {
    (void)offset;
    return 0xFF;
}

uint8_t MockingboardCard::readROM(uint8_t offset) {
    if (!enabled_) return 0xFF;

    // Address decoding for slot ROM space
    // VIA 1 is mirrored at $C400-$C47F (active when bit 7 = 0)
    // VIA 2 is mirrored at $C480-$C4FF (active when bit 7 = 1)
    // Register is determined by bits 0-3

    uint8_t reg = offset & 0x0F;

    if ((offset & 0x80) == 0) {
        // VIA 1 ($C400-$C47F)
        return via1_.read(reg);
    } else {
        // VIA 2 ($C480-$C4FF)
        return via2_.read(reg);
    }
}

void MockingboardCard::writeROM(uint8_t offset, uint8_t value) {
    if (!enabled_) return;

    uint8_t reg = offset & 0x0F;

    if ((offset & 0x80) == 0) {
        // VIA 1 ($C400-$C47F)
        via1_.write(reg, value);
    } else {
        // VIA 2 ($C480-$C4FF)
        via2_.write(reg, value);
    }
}

void MockingboardCard::reset() {
    via1_.reset();
    via2_.reset();
    psg1_.reset();
    psg2_.reset();
    cycleAccum_ = 0.0;
    sampleAccum_.clear();
    sampleReadPos_ = 0;
}

void MockingboardCard::update(int cycles) {
    if (!enabled_) return;

    via1_.update(cycles);
    via2_.update(cycles);

    // Incremental audio generation: accumulate CPU cycles and generate
    // audio samples at 48kHz rate. This ensures PSG register changes
    // from VIA timer IRQ handlers are immediately reflected in output.
    cycleAccum_ += cycles;
    while (cycleAccum_ >= CYCLES_PER_SAMPLE) {
        cycleAccum_ -= CYCLES_PER_SAMPLE;

        float left = psg1_.generateSingleSample();
        float right = psg2_.generateSingleSample();

        sampleAccum_.push_back(left);
        sampleAccum_.push_back(right);
    }
}

void MockingboardCard::setIRQCallback(IRQCallback callback) {
    // Both VIAs can trigger IRQ
    via1_.setIRQCallback(callback);
    via2_.setIRQCallback(callback);
}

bool MockingboardCard::isIRQActive() const {
    return enabled_ && (via1_.isIRQActive() || via2_.isIRQActive());
}

size_t MockingboardCard::getStateSize() const {
    return STATE_SIZE;
}

size_t MockingboardCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;

    size_t offset = 0;

    // Enabled flag
    buffer[offset++] = enabled_ ? 1 : 0;

    // VIA1 and PSG1
    offset += via1_.exportState(buffer + offset);
    offset += psg1_.exportState(buffer + offset);

    // VIA2 and PSG2
    offset += via2_.exportState(buffer + offset);
    offset += psg2_.exportState(buffer + offset);

    return offset;
}

size_t MockingboardCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;

    size_t offset = 0;

    // Enabled flag
    enabled_ = buffer[offset++] != 0;

    // VIA1 and PSG1
    via1_.importState(buffer + offset);
    offset += VIA6522::STATE_SIZE;
    psg1_.importState(buffer + offset);
    offset += AY8910::STATE_SIZE;

    // VIA2 and PSG2
    via2_.importState(buffer + offset);
    offset += VIA6522::STATE_SIZE;
    psg2_.importState(buffer + offset);
    offset += AY8910::STATE_SIZE;

    return offset;
}

void MockingboardCard::generateSamples(float* buffer, int count, int sampleRate) {
    if (!enabled_ || count <= 0) {
        for (int i = 0; i < count; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    if (static_cast<int>(audioBuffer1_.size()) < count) {
        audioBuffer1_.resize(count);
    }

    psg1_.generateSamples(buffer, count, sampleRate);
    psg2_.generateSamples(audioBuffer1_.data(), count, sampleRate);

    for (int i = 0; i < count; i++) {
        buffer[i] = (buffer[i] + audioBuffer1_[i]) * 0.5f;
    }
}

void MockingboardCard::generateSamples(float* buffer, int count, int sampleRate, uint64_t startCycle, uint64_t endCycle) {
    if (!enabled_ || count <= 0) {
        for (int i = 0; i < count; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    if (static_cast<int>(audioBuffer1_.size()) < count) {
        audioBuffer1_.resize(count);
    }

    // Generate with proper timing
    psg1_.generateSamples(buffer, count, sampleRate, startCycle, endCycle);
    psg2_.generateSamples(audioBuffer1_.data(), count, sampleRate, startCycle, endCycle);

    for (int i = 0; i < count; i++) {
        buffer[i] = (buffer[i] + audioBuffer1_[i]) * 0.5f;
    }
}

void MockingboardCard::generateStereoSamples(float* buffer, int count, int sampleRate) {
    if (!enabled_ || count <= 0) {
        for (int i = 0; i < count * 2; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    if (static_cast<int>(audioBuffer1_.size()) < count) {
        audioBuffer1_.resize(count);
    }
    if (static_cast<int>(audioBuffer2_.size()) < count) {
        audioBuffer2_.resize(count);
    }

    psg1_.generateSamples(audioBuffer1_.data(), count, sampleRate);
    psg2_.generateSamples(audioBuffer2_.data(), count, sampleRate);

    for (int i = 0; i < count; i++) {
        buffer[i * 2] = audioBuffer1_[i];
        buffer[i * 2 + 1] = audioBuffer2_[i];
    }
}

void MockingboardCard::generateStereoSamples(float* buffer, int count, int sampleRate, uint64_t startCycle, uint64_t endCycle) {
    if (!enabled_ || count <= 0) {
        for (int i = 0; i < count * 2; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    if (static_cast<int>(audioBuffer1_.size()) < count) {
        audioBuffer1_.resize(count);
    }
    if (static_cast<int>(audioBuffer2_.size()) < count) {
        audioBuffer2_.resize(count);
    }

    // Generate with proper timing
    psg1_.generateSamples(audioBuffer1_.data(), count, sampleRate, startCycle, endCycle);
    psg2_.generateSamples(audioBuffer2_.data(), count, sampleRate, startCycle, endCycle);

    for (int i = 0; i < count; i++) {
        buffer[i * 2] = audioBuffer1_[i];
        buffer[i * 2 + 1] = audioBuffer2_[i];
    }
}

int MockingboardCard::consumeStereoSamples(float* buffer, int frameCount) {
    if (!enabled_ || frameCount <= 0) {
        for (int i = 0; i < frameCount * 2; i++) {
            buffer[i] = 0.0f;
        }
        return frameCount;
    }

    int availableFrames = static_cast<int>((sampleAccum_.size() - sampleReadPos_) / 2);
    int framesToCopy = std::min(frameCount, availableFrames);

    // Copy available accumulated samples
    if (framesToCopy > 0) {
        std::memcpy(buffer, sampleAccum_.data() + sampleReadPos_,
                    framesToCopy * 2 * sizeof(float));
        sampleReadPos_ += framesToCopy * 2;
    }

    // If we need more samples than accumulated, generate the remainder on the spot
    // (handles slight timing drift between CPU execution and audio requests)
    for (int i = framesToCopy; i < frameCount; i++) {
        buffer[i * 2] = psg1_.generateSingleSample();
        buffer[i * 2 + 1] = psg2_.generateSingleSample();
    }

    // Compact the buffer: remove consumed samples
    if (sampleReadPos_ > 0) {
        size_t remaining = sampleAccum_.size() - sampleReadPos_;
        if (remaining > 0) {
            std::memmove(sampleAccum_.data(), sampleAccum_.data() + sampleReadPos_,
                         remaining * sizeof(float));
        }
        sampleAccum_.resize(remaining);
        sampleReadPos_ = 0;
    }

    return frameCount;
}

int MockingboardCard::consumeMonoSamples(float* buffer, int sampleCount) {
    if (!enabled_ || sampleCount <= 0) {
        for (int i = 0; i < sampleCount; i++) {
            buffer[i] = 0.0f;
        }
        return sampleCount;
    }

    int availableFrames = static_cast<int>((sampleAccum_.size() - sampleReadPos_) / 2);
    int framesToCopy = std::min(sampleCount, availableFrames);

    // Mix stereo to mono from accumulated samples
    for (int i = 0; i < framesToCopy; i++) {
        float left = sampleAccum_[sampleReadPos_++];
        float right = sampleAccum_[sampleReadPos_++];
        buffer[i] = (left + right) * 0.5f;
    }

    // Generate remainder on the spot if needed
    for (int i = framesToCopy; i < sampleCount; i++) {
        float left = psg1_.generateSingleSample();
        float right = psg2_.generateSingleSample();
        buffer[i] = (left + right) * 0.5f;
    }

    // Compact
    if (sampleReadPos_ > 0) {
        size_t remaining = sampleAccum_.size() - sampleReadPos_;
        if (remaining > 0) {
            std::memmove(sampleAccum_.data(), sampleAccum_.data() + sampleReadPos_,
                         remaining * sizeof(float));
        }
        sampleAccum_.resize(remaining);
        sampleReadPos_ = 0;
    }

    return sampleCount;
}

} // namespace a2e
