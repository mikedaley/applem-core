#include "mockingboard.hpp"

namespace a2e {

Mockingboard::Mockingboard() {
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

void Mockingboard::setDebugLogging(bool enabled) {
    AY8910::setDebugLogging(enabled);
    VIA6522::setDebugLogging(enabled);
}

void Mockingboard::reset() {
    via1_.reset();
    via2_.reset();
    psg1_.reset();
    psg2_.reset();
}

uint8_t Mockingboard::read(uint16_t address) {
    if (!enabled_) return 0xFF;

    // Address decoding for slot 4
    // VIA 1 is mirrored at $C400-$C47F (active when bit 7 = 0)
    // VIA 2 is mirrored at $C480-$C4FF (active when bit 7 = 1)
    // Register is determined by bits 0-3

    uint8_t reg = address & 0x0F;

    if ((address & 0x80) == 0) {
        // VIA 1 ($C400-$C47F)
        return via1_.read(reg);
    } else {
        // VIA 2 ($C480-$C4FF)
        return via2_.read(reg);
    }
}

void Mockingboard::write(uint16_t address, uint8_t value) {
    if (!enabled_) return;

    uint8_t reg = address & 0x0F;

    if ((address & 0x80) == 0) {
        // VIA 1 ($C400-$C47F)
        via1_.write(reg, value);
    } else {
        // VIA 2 ($C480-$C4FF)
        via2_.write(reg, value);
    }
}

void Mockingboard::update(int cycles) {
    if (!enabled_) return;

    via1_.update(cycles);
    via2_.update(cycles);
}

void Mockingboard::generateSamples(float* buffer, int count, int sampleRate) {
    if (!enabled_ || count <= 0) {
        // Fill with silence
        for (int i = 0; i < count; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    // Generate samples from both PSGs
    // We use a temporary buffer for PSG2 and mix with PSG1
    std::vector<float> psg2Buffer(count);

    psg1_.generateSamples(buffer, count, sampleRate);
    psg2_.generateSamples(psg2Buffer.data(), count, sampleRate);

    // Mix both PSGs together (stereo would put them in L/R channels,
    // but for mono output we sum them)
    // Don't attenuate here - let the final mixer handle levels
    for (int i = 0; i < count; i++) {
        buffer[i] = buffer[i] + psg2Buffer[i];
    }
}

void Mockingboard::setIRQCallback(IRQCallback cb) {
    // Both VIAs can trigger IRQ
    via1_.setIRQCallback(cb);
    via2_.setIRQCallback(cb);
}

size_t Mockingboard::exportState(uint8_t* buffer, size_t maxSize) const {
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

size_t Mockingboard::importState(const uint8_t* buffer, size_t size) {
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

} // namespace a2e
