#include "ay8910.hpp"
#include <cmath>

namespace a2e {

// Volume table based on AY-3-8910 datasheet
// Values represent amplitude levels for 4-bit volume (0-15)
// Using exponential curve matching hardware behavior
const float AY8910::volumeTable_[16] = {
    0.0000f, 0.0078f, 0.0110f, 0.0156f,
    0.0221f, 0.0312f, 0.0441f, 0.0624f,
    0.0883f, 0.1249f, 0.1766f, 0.2498f,
    0.3535f, 0.5000f, 0.7071f, 1.0000f
};

AY8910::AY8910() {
    reset();
}

void AY8910::reset() {
    registers_.fill(0);
    currentRegister_ = 0;

    // Set mixer to 0x3F - all tone and noise DISABLED for silence
    // Bits 0-2: tone disable (1=disabled), Bits 3-5: noise disable (1=disabled)
    registers_[REG_MIXER] = 0x3F;

    toneCounters_.fill(0);
    toneOutput_.fill(false);

    noiseCounter_ = 0;
    noiseShiftReg_ = 1;  // Must be non-zero
    noiseOutput_ = false;

    envCounter_ = 0;
    envVolume_ = 0;
    envHolding_ = false;
    envAttack_ = false;
    envAlternate_ = false;
    envHold_ = false;

    phaseAccumulator_ = 0.0;
}

void AY8910::setRegisterAddress(uint8_t address) {
    currentRegister_ = address & 0x0F;
}

void AY8910::writeRegister(uint8_t value) {
    // Apply masks based on register
    switch (currentRegister_) {
        case REG_TONE_A_COARSE:
        case REG_TONE_B_COARSE:
        case REG_TONE_C_COARSE:
            value &= 0x0F;  // 4-bit coarse tune
            break;
        case REG_NOISE_PERIOD:
            value &= 0x1F;  // 5-bit noise period
            break;
        case REG_AMP_A:
        case REG_AMP_B:
        case REG_AMP_C:
            value &= 0x1F;  // 5-bit (bit 4 = envelope mode)
            break;
        case REG_ENV_SHAPE:
            value &= 0x0F;  // 4-bit envelope shape
            // Writing to envelope shape resets the envelope
            envCounter_ = 0;
            envVolume_ = 0;
            envHolding_ = false;
            // Decode envelope shape
            envAttack_ = (value & 0x04) != 0;
            envAlternate_ = (value & 0x02) != 0;
            envHold_ = (value & 0x01) != 0;
            if (!envAttack_) {
                envVolume_ = 15;  // Start at max if not attack
            }
            break;
    }

    registers_[currentRegister_] = value;
}

uint8_t AY8910::readRegister() const {
    return registers_[currentRegister_];
}

uint16_t AY8910::getTonePeriod(int channel) const {
    int fineReg = channel * 2;
    int coarseReg = channel * 2 + 1;
    return registers_[fineReg] | ((registers_[coarseReg] & 0x0F) << 8);
}

uint8_t AY8910::getNoisePeriod() const {
    return registers_[REG_NOISE_PERIOD] & 0x1F;
}

uint16_t AY8910::getEnvPeriod() const {
    return registers_[REG_ENV_FINE] | (registers_[REG_ENV_COARSE] << 8);
}

void AY8910::updateToneGenerator(int channel) {
    uint16_t period = getTonePeriod(channel);
    if (period == 0) period = 1;  // Avoid division by zero

    toneCounters_[channel]++;
    if (toneCounters_[channel] >= period) {
        toneCounters_[channel] = 0;
        toneOutput_[channel] = !toneOutput_[channel];
    }
}

void AY8910::updateNoiseGenerator() {
    uint8_t period = getNoisePeriod();
    if (period == 0) period = 1;

    noiseCounter_++;
    if (noiseCounter_ >= period) {
        noiseCounter_ = 0;

        // 17-bit LFSR with taps at bits 0 and 3
        // XOR bits 0 and 3, shift right, put result in bit 16
        bool bit = ((noiseShiftReg_ ^ (noiseShiftReg_ >> 3)) & 1) != 0;
        noiseShiftReg_ = (noiseShiftReg_ >> 1) | (bit ? 0x10000 : 0);
        noiseOutput_ = (noiseShiftReg_ & 1) != 0;
    }
}

void AY8910::updateEnvelopeGenerator() {
    if (envHolding_) return;

    uint16_t period = getEnvPeriod();
    if (period == 0) period = 1;

    envCounter_++;
    if (envCounter_ >= period) {
        envCounter_ = 0;

        // Update envelope volume
        if (envAttack_) {
            // Attack (rising)
            if (envVolume_ < 15) {
                envVolume_++;
            } else {
                // Reached max
                if (envHold_) {
                    if (envAlternate_) {
                        envVolume_ = 0;  // Hold at 0
                    }
                    envHolding_ = true;
                } else if (envAlternate_) {
                    envAttack_ = false;  // Switch to decay
                } else {
                    envVolume_ = 0;  // Restart
                }
            }
        } else {
            // Decay (falling)
            if (envVolume_ > 0) {
                envVolume_--;
            } else {
                // Reached min
                if (envHold_) {
                    if (envAlternate_) {
                        envVolume_ = 15;  // Hold at max
                    }
                    envHolding_ = true;
                } else if (envAlternate_) {
                    envAttack_ = true;  // Switch to attack
                } else {
                    envVolume_ = 15;  // Restart from max (will decay again)
                }
            }
        }
    }
}

float AY8910::getChannelOutput(int channel) const {
    uint8_t mixer = registers_[REG_MIXER];
    uint8_t ampReg = registers_[REG_AMP_A + channel];

    // Get volume first - if zero, skip all other processing
    uint8_t volume;
    if (ampReg & 0x10) {
        // Use envelope
        volume = envVolume_;
    } else {
        // Fixed amplitude
        volume = ampReg & 0x0F;
    }

    // Early exit if volume is zero
    if (volume == 0) return 0.0f;

    // Check if tone and/or noise are enabled for this channel
    // Note: bit=0 means enabled, bit=1 means disabled
    bool toneDisabled = (mixer & (1 << channel)) != 0;
    bool noiseDisabled = (mixer & (1 << (channel + 3))) != 0;

    // Determine output state
    bool output;
    if (toneDisabled && noiseDisabled) {
        // Both disabled: constant HIGH output (DC at volume level)
        // This is correct AY-3-8910 behavior but produces unwanted DC offset
        // Return 0 instead to avoid stuck sounds when channels are "off"
        return 0.0f;
    } else {
        // Mix enabled sources - output is high when ANY enabled source is high
        output = false;
        if (!toneDisabled) output = output || toneOutput_[channel];
        if (!noiseDisabled) output = output || noiseOutput_;
    }

    // When output is low, return silence
    if (!output) return 0.0f;

    return volumeTable_[volume];
}

void AY8910::generateSamples(float* buffer, int count, int sampleRate) {
    // PSG clock cycles per audio sample
    double cyclesPerSample = static_cast<double>(PSG_CLOCK) / sampleRate;
    // The tone generators run at clock/16 (divide by 16)
    // So effective cycles per sample for tone = cyclesPerSample / 16
    double toneStepsPerSample = cyclesPerSample / 16.0;

    for (int i = 0; i < count; i++) {
        // Accumulate fractional cycles
        phaseAccumulator_ += toneStepsPerSample;

        // Process whole cycles
        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;

            // Update all generators
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                updateToneGenerator(ch);
            }
            updateNoiseGenerator();
            updateEnvelopeGenerator();
        }

        // Mix all channels
        float sample = 0.0f;
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            sample += getChannelOutput(ch);
        }

        // Normalize (3 channels, each max 1.0)
        sample /= 3.0f;

        buffer[i] = sample;
    }
}

} // namespace a2e
