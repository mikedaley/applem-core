#include "ay8910.hpp"
#include <cmath>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace a2e {

// Debug logging flag - set via setDebugLogging()
static bool debugLogging_ = false;

void AY8910::setDebugLogging(bool enabled) {
    debugLogging_ = enabled;
}

void AY8910::setPsgId(int id) {
    psgId_ = id;
}

static const char* getRegisterName(int reg) {
    static const char* names[] = {
        "ToneA_Fine", "ToneA_Coarse", "ToneB_Fine", "ToneB_Coarse",
        "ToneC_Fine", "ToneC_Coarse", "NoisePeriod", "Mixer",
        "AmpA", "AmpB", "AmpC", "EnvFine", "EnvCoarse", "EnvShape",
        "IOPortA", "IOPortB"
    };
    return (reg >= 0 && reg < 16) ? names[reg] : "Unknown";
}

// Volume table based on AppleWin/MAME measurements
// Values represent amplitude levels for 4-bit volume (0-15)
// Converted from 16-bit values: 0x0000, 0x0385, 0x053D, 0x0770, etc.
const float AY8910::volumeTable_[16] = {
    0.0000f, 0.0137f, 0.0205f, 0.0291f,
    0.0423f, 0.0618f, 0.0847f, 0.1369f,
    0.1691f, 0.2647f, 0.3527f, 0.4499f,
    0.5704f, 0.6873f, 0.8482f, 1.0000f
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
    envContinue_ = false;
    envAttack_ = false;
    envAlternate_ = false;
    envHold_ = false;

    phaseAccumulator_ = 0.0;

    // Clear any pending register writes
    pendingWrites_.clear();
}

void AY8910::setRegisterAddress(uint8_t address) {
    currentRegister_ = address & 0x0F;
}

void AY8910::writeRegister(uint8_t value) {
    // Track writes for debugging
    writeCount_++;
    lastWriteReg_ = currentRegister_;
    lastWriteVal_ = value;

    // If we have a cycle callback, queue the write for proper timing during sample generation
    if (cycleCallback_) {
        uint64_t cycle = cycleCallback_();
        pendingWrites_.push_back({cycle, currentRegister_, value});
    } else {
        // No timing available, apply immediately
        applyRegisterWrite(currentRegister_, value);
    }

#ifdef __EMSCRIPTEN__
    if (debugLogging_) {
        const char* regName = getRegisterName(currentRegister_);
        EM_ASM({
            const reg = $0;
            const val = $1;
            const regName = UTF8ToString($2);
            const psgId = $3;
            console.log(`PSG${psgId}: R${reg} (${regName}) = $${val.toString(16).toUpperCase().padStart(2,'0')} (${val})`);
        }, currentRegister_, value, regName, psgId_);
    }
#endif
}

void AY8910::applyRegisterWrite(uint8_t reg, uint8_t value) {
    // Apply masks based on register
    switch (reg) {
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
            envHolding_ = false;
            // Decode envelope shape bits: CONT ATT ALT HOLD (bits 3-0)
            envContinue_ = (value & 0x08) != 0;
            envAttack_ = (value & 0x04) != 0;
            envAlternate_ = (value & 0x02) != 0;
            envHold_ = (value & 0x01) != 0;
            // Set initial volume based on direction
            if (envAttack_) {
                envVolume_ = 0;   // Start at 0 for attack (rising)
            } else {
                envVolume_ = 15;  // Start at 15 for decay (falling)
            }
            break;
    }

    registers_[reg] = value;
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

    // Period 0 acts as period 1 (highest frequency noise)
    // Hardware testing confirms noise does not halt on period 0
    if (period == 0) period = 1;

    noiseCounter_++;
    // Noise runs at clock/16 while tones run at clock/8
    // Since we step at clock/8 rate, double the period comparison
    if (noiseCounter_ >= static_cast<uint32_t>(period) * 2) {
        noiseCounter_ = 0;

        // 17-bit LFSR (Galois form) with polynomial x^17 + x^14 + 1
        // This matches MAME's implementation and the reverse-engineered chip.
        // Taps at bits 13 and 16: when LSB is 1, XOR those bit positions after shift.
        uint32_t lsb = noiseShiftReg_ & 1;
        noiseShiftReg_ = (noiseShiftReg_ >> 1) ^ (lsb ? 0x12000 : 0);
        noiseOutput_ = (noiseShiftReg_ & 1) != 0;
    }
}

void AY8910::updateEnvelopeGenerator() {
    if (envHolding_) return;

    uint16_t period = getEnvPeriod();

    envCounter_++;
    // Envelope timing: datasheet specifies clock/256 for envelope generator.
    // Since we step at clock/8 rate, we need a multiplier of 32 (8*32=256).
    // Special case: Period 0 runs at HALF the time of period 1 (twice as fast).
    // For period 0: use 16 (half of 32) to maintain the 2× speed relationship.
    uint32_t threshold = (period == 0) ? 16 : static_cast<uint32_t>(period) * 32;
    if (envCounter_ >= threshold) {
        envCounter_ = 0;

        // Update envelope volume based on current direction
        if (envAttack_) {
            // Attack (rising)
            if (envVolume_ < 15) {
                envVolume_++;
            } else {
                // Reached max (15) - handle end of cycle
                handleEnvelopeCycleEnd();
            }
        } else {
            // Decay (falling)
            if (envVolume_ > 0) {
                envVolume_--;
            } else {
                // Reached min (0) - handle end of cycle
                handleEnvelopeCycleEnd();
            }
        }
    }
}

void AY8910::handleEnvelopeCycleEnd() {
    // Called when envelope reaches its limit (0 or 15)
    // Envelope shape bits: CONT(3) ATT(2) ALT(1) HOLD(0)

    if (!envContinue_) {
        // CONT=0: After first cycle, always hold at 0
        envVolume_ = 0;
        envHolding_ = true;
        return;
    }

    // CONT=1: Continue behavior depends on ALT and HOLD
    if (envHold_) {
        // HOLD=1: Stop after this cycle
        if (envAlternate_) {
            // ALT=1, HOLD=1: Hold at opposite extreme
            // If we were attacking (going up), hold at 0
            // If we were decaying (going down), hold at 15
            envVolume_ = envAttack_ ? 0 : 15;
        }
        // else ALT=0, HOLD=1: Hold at current extreme (already there)
        envHolding_ = true;
    } else {
        // HOLD=0: Continue cycling
        if (envAlternate_) {
            // ALT=1: Reverse direction (triangle wave)
            envAttack_ = !envAttack_;
        } else {
            // ALT=0: Reset to start (sawtooth wave)
            envVolume_ = envAttack_ ? 0 : 15;
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

    // AY-3-8910 mixer uses AND logic:
    // Output = (ToneOut OR ToneDisabled) AND (NoiseOut OR NoiseDisabled)
    // When disabled, that source is treated as always HIGH
    // When enabled, the source's actual output is used
    bool toneGate = toneDisabled || toneOutput_[channel];
    bool noiseGate = noiseDisabled || noiseOutput_;
    bool output = toneGate && noiseGate;

    // Generate bipolar output centered at 0
    // When output is HIGH: +volume, when LOW: -volume
    // This creates a proper AC signal without DC offset
    float amplitude = volumeTable_[volume];
    return output ? amplitude : -amplitude;
}

void AY8910::setChannelMute(int channel, bool muted) {
    if (channel >= 0 && channel < NUM_CHANNELS) {
        channelMuted_[channel] = muted;
    }
}

bool AY8910::isChannelMuted(int channel) const {
    if (channel >= 0 && channel < NUM_CHANNELS) {
        return channelMuted_[channel];
    }
    return false;
}

void AY8910::generateSamples(float* buffer, int count, int sampleRate) {
    // Legacy version - apply any pending writes immediately and generate
    for (const auto& write : pendingWrites_) {
        applyRegisterWrite(write.reg, write.value);
    }
    pendingWrites_.clear();

    // PSG clock cycles per audio sample
    double cyclesPerSample = static_cast<double>(PSG_CLOCK) / sampleRate;
    double toneStepsPerSample = cyclesPerSample / 8.0;

    for (int i = 0; i < count; i++) {
        phaseAccumulator_ += toneStepsPerSample;

        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                updateToneGenerator(ch);
            }
            updateNoiseGenerator();
            updateEnvelopeGenerator();
        }

        float sample = 0.0f;
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            if (!channelMuted_[ch]) {
                sample += getChannelOutput(ch);
            }
        }
        sample /= 3.0f;
        buffer[i] = sample;
    }
}

void AY8910::generateSamples(float* buffer, int count, int sampleRate, uint64_t startCycle, uint64_t endCycle) {
    // PSG clock cycles per audio sample
    double cyclesPerSample = static_cast<double>(PSG_CLOCK) / sampleRate;
    double toneStepsPerSample = cyclesPerSample / 8.0;

    // Calculate CPU cycles per sample for timing
    double cpuCyclesTotal = static_cast<double>(endCycle - startCycle);
    double cpuCyclesPerSample = (count > 0) ? cpuCyclesTotal / count : 0;

    // Index into pending writes
    size_t writeIdx = 0;

    for (int i = 0; i < count; i++) {
        // Calculate the CPU cycle for this sample
        uint64_t sampleCycle = startCycle + static_cast<uint64_t>(i * cpuCyclesPerSample);

        // Apply any pending writes that should happen before this sample
        while (writeIdx < pendingWrites_.size() && pendingWrites_[writeIdx].cycle <= sampleCycle) {
            applyRegisterWrite(pendingWrites_[writeIdx].reg, pendingWrites_[writeIdx].value);
            writeIdx++;
        }

        // Advance PSG state
        phaseAccumulator_ += toneStepsPerSample;

        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                updateToneGenerator(ch);
            }
            updateNoiseGenerator();
            updateEnvelopeGenerator();
        }

        // Mix channels (output is now bipolar, centered at 0)
        float sample = 0.0f;
        for (int ch = 0; ch < NUM_CHANNELS; ch++) {
            if (!channelMuted_[ch]) {
                sample += getChannelOutput(ch);
            }
        }
        sample /= 3.0f;
        buffer[i] = sample;
    }

    // Apply any remaining writes (for the end of the buffer)
    while (writeIdx < pendingWrites_.size()) {
        applyRegisterWrite(pendingWrites_[writeIdx].reg, pendingWrites_[writeIdx].value);
        writeIdx++;
    }

    // Clear processed writes
    pendingWrites_.clear();
}

void AY8910::generateChannelSamples(float* buffer, int count, int sampleRate, int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) {
        for (int i = 0; i < count; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    // PSG clock cycles per audio sample
    double cyclesPerSample = static_cast<double>(PSG_CLOCK) / sampleRate;
    double toneStepsPerSample = cyclesPerSample / 8.0;

    for (int i = 0; i < count; i++) {
        // Accumulate fractional cycles
        phaseAccumulator_ += toneStepsPerSample;

        // Process whole cycles
        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;

            // Update all generators (needed for accurate state)
            for (int ch = 0; ch < NUM_CHANNELS; ch++) {
                updateToneGenerator(ch);
            }
            updateNoiseGenerator();
            updateEnvelopeGenerator();
        }

        // Output only the requested channel
        buffer[i] = getChannelOutput(channel);
    }
}

size_t AY8910::exportState(uint8_t* buffer) const {
    size_t offset = 0;

    // 16 registers
    for (int i = 0; i < 16; i++) {
        buffer[offset++] = registers_[i];
    }

    // Current register address
    buffer[offset++] = currentRegister_;

    // Tone counters (3 x 4 bytes = 12 bytes)
    for (int i = 0; i < 3; i++) {
        buffer[offset++] = (toneCounters_[i] >> 0) & 0xFF;
        buffer[offset++] = (toneCounters_[i] >> 8) & 0xFF;
        buffer[offset++] = (toneCounters_[i] >> 16) & 0xFF;
        buffer[offset++] = (toneCounters_[i] >> 24) & 0xFF;
    }

    // Tone outputs (1 byte packed)
    buffer[offset++] = (toneOutput_[0] ? 1 : 0) |
                       (toneOutput_[1] ? 2 : 0) |
                       (toneOutput_[2] ? 4 : 0);

    // Noise state
    buffer[offset++] = noiseOutput_ ? 1 : 0;

    // Envelope state
    buffer[offset++] = envVolume_;

    // Additional state for proper audio continuity (added in state version 5)
    // Noise counter (4 bytes)
    buffer[offset++] = (noiseCounter_ >> 0) & 0xFF;
    buffer[offset++] = (noiseCounter_ >> 8) & 0xFF;
    buffer[offset++] = (noiseCounter_ >> 16) & 0xFF;
    buffer[offset++] = (noiseCounter_ >> 24) & 0xFF;

    // Noise shift register (4 bytes) - critical for noise pattern continuity
    buffer[offset++] = (noiseShiftReg_ >> 0) & 0xFF;
    buffer[offset++] = (noiseShiftReg_ >> 8) & 0xFF;
    buffer[offset++] = (noiseShiftReg_ >> 16) & 0xFF;
    buffer[offset++] = (noiseShiftReg_ >> 24) & 0xFF;

    // Envelope counter (4 bytes)
    buffer[offset++] = (envCounter_ >> 0) & 0xFF;
    buffer[offset++] = (envCounter_ >> 8) & 0xFF;
    buffer[offset++] = (envCounter_ >> 16) & 0xFF;
    buffer[offset++] = (envCounter_ >> 24) & 0xFF;

    // Envelope flags (1 byte packed)
    buffer[offset++] = (envHolding_ ? 0x01 : 0) |
                       (envAttack_ ? 0x02 : 0);

    // Pad to STATE_SIZE for consistent serialization
    while (offset < STATE_SIZE) {
        buffer[offset++] = 0;
    }

    return offset;  // Exactly STATE_SIZE bytes
}

void AY8910::importState(const uint8_t* buffer) {
    size_t offset = 0;

    // 16 registers
    for (int i = 0; i < 16; i++) {
        registers_[i] = buffer[offset++];
    }

    // Current register address
    currentRegister_ = buffer[offset++];

    // Tone counters
    for (int i = 0; i < 3; i++) {
        toneCounters_[i] = buffer[offset] |
                          (buffer[offset + 1] << 8) |
                          (buffer[offset + 2] << 16) |
                          (buffer[offset + 3] << 24);
        offset += 4;
    }

    // Tone outputs
    uint8_t outputs = buffer[offset++];
    toneOutput_[0] = (outputs & 1) != 0;
    toneOutput_[1] = (outputs & 2) != 0;
    toneOutput_[2] = (outputs & 4) != 0;

    // Noise state
    noiseOutput_ = buffer[offset++] != 0;

    // Envelope state
    envVolume_ = buffer[offset++];

    // Additional state for proper audio continuity (added in state version 5)
    // Noise counter (4 bytes)
    noiseCounter_ = buffer[offset] |
                    (buffer[offset + 1] << 8) |
                    (buffer[offset + 2] << 16) |
                    (buffer[offset + 3] << 24);
    offset += 4;

    // Noise shift register (4 bytes)
    noiseShiftReg_ = buffer[offset] |
                     (buffer[offset + 1] << 8) |
                     (buffer[offset + 2] << 16) |
                     (buffer[offset + 3] << 24);
    offset += 4;

    // Envelope counter (4 bytes)
    envCounter_ = buffer[offset] |
                  (buffer[offset + 1] << 8) |
                  (buffer[offset + 2] << 16) |
                  (buffer[offset + 3] << 24);
    offset += 4;

    // Envelope flags (1 byte packed)
    uint8_t envFlags = buffer[offset++];
    envHolding_ = (envFlags & 0x01) != 0;
    envAttack_ = (envFlags & 0x02) != 0;

    // Restore envelope shape flags from register 13 (these are constant per shape)
    uint8_t envShape = registers_[REG_ENV_SHAPE] & 0x0F;
    envContinue_ = (envShape & 0x08) != 0;
    envAlternate_ = (envShape & 0x02) != 0;
    envHold_ = (envShape & 0x01) != 0;
}

} // namespace a2e
