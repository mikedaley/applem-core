#include "mockingboard.hpp"

namespace a2e {

Mockingboard::Mockingboard() {
    // Connect PSGs to VIAs
    via1_.connectPSG(&psg1_);
    via2_.connectPSG(&psg2_);

    reset();
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
    // $C400-$C40F: VIA 1
    // $C480-$C48F: VIA 2
    // $C4xx where bit 7 determines which VIA

    uint8_t reg = address & 0x0F;

    if (address >= 0xC400 && address < 0xC410) {
        // VIA 1
        return via1_.read(reg);
    } else if (address >= 0xC480 && address < 0xC490) {
        // VIA 2
        return via2_.read(reg);
    }

    // Unmapped addresses return floating bus value
    return 0xFF;
}

void Mockingboard::write(uint16_t address, uint8_t value) {
    if (!enabled_) return;

    uint8_t reg = address & 0x0F;

    if (address >= 0xC400 && address < 0xC410) {
        // VIA 1
        via1_.write(reg, value);
    } else if (address >= 0xC480 && address < 0xC490) {
        // VIA 2
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

} // namespace a2e
