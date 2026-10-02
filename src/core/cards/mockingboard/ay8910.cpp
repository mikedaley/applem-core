/*
 * ay8910.cpp - AY-3-8910 sound chip emulation implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "ay8910.hpp"
#include <cmath>
#include "../../debug/debug_log.hpp"

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

namespace {

constexpr double PI = 3.14159265358979323846;

// Zeroth-order modified Bessel function, for the Kaiser window.
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// The resampling filter: a Kaiser-windowed sinc over FILTER_TAPS ticks, one
// row per fractional tick position, each row normalised to unity gain so a
// steady level (sample playback through the volume register) passes exactly.
// The cutoff is 20kHz at the //e's tick rate of 127.875kHz: with beta 7 the
// stopband (about 70dB down) begins near 24kHz, so nothing that would fold
// back below 20kHz at a 48kHz output survives.
struct ResampleKernel {
    float rows[AY8910::FILTER_PHASES][AY8910::FILTER_TAPS];

    ResampleKernel() {
        constexpr double TICK_RATE = AY8910::DEFAULT_CLOCK_HZ / 8.0;
        constexpr double CUTOFF = 20000.0 / TICK_RATE;  // cycles per tick
        constexpr double BETA = 7.0;
        constexpr double HALF = AY8910::FILTER_TAPS / 2.0;
        const double norm = besselI0(BETA);
        for (int p = 0; p < AY8910::FILTER_PHASES; p++) {
            const double frac = static_cast<double>(p) / AY8910::FILTER_PHASES;
            double sum = 0.0;
            double row[AY8910::FILTER_TAPS];
            for (int j = 0; j < AY8910::FILTER_TAPS; j++) {
                // Distance from the output time to tap j's tick (newest is j=0).
                const double x = j + frac - HALF;
                const double arg = 2.0 * CUTOFF * x;
                const double sinc = (x == 0.0) ? 1.0 : std::sin(PI * arg) / (PI * arg);
                const double u = x / HALF;
                const double window = (u * u < 1.0) ? besselI0(BETA * std::sqrt(1.0 - u * u)) / norm : 0.0;
                row[j] = sinc * window;
                sum += row[j];
            }
            for (int j = 0; j < AY8910::FILTER_TAPS; j++) {
                rows[p][j] = static_cast<float>(row[j] / sum);
            }
        }
    }
};

const ResampleKernel& resampleKernel() {
    static const ResampleKernel kernel;
    return kernel;
}

} // namespace

AY8910::AY8910() {
    reset();
}

void AY8910::setClock(double hz) {
    if (hz <= 0.0) return;
    clockHz_ = hz;
    ticksPerSample_ = hz / (48000.0 * 8.0);
}

void AY8910::reset() {
    // The RESET pin clears every register to zero, the mixer included, which
    // enables every tone and noise path. The chip is still silent, because
    // every amplitude register is zero as well.
    registers_.fill(0);
    currentRegister_ = 0;

    toneCounters_.fill(0);
    toneOutput_.fill(false);

    noiseCounter_ = 0;
    noiseShiftReg_ = 1;  // Must be non-zero
    noiseToggle_ = false;

    envCounter_ = 0;
    envVolume_ = 0;
    envHolding_ = false;
    envContinue_ = false;
    envAttack_ = false;
    envAlternate_ = false;
    envHold_ = false;

    // The output history is what the chip has already played, so it stays:
    // clearing it would put a step into the filter that the chip never made.
    phaseAccumulator_ = 0.0;
}

void AY8910::setRegisterAddress(uint8_t address) {
    currentRegister_ = address & 0x0F;
}

void AY8910::writeRegister(uint8_t value) {
    // Track writes for debugging
    writeCount_++;
    lastWriteReg_ = currentRegister_;
    lastWriteVal_ = value;

    // Apply register write immediately
    applyRegisterWrite(currentRegister_, value);

    if (debugLogging_) {
        debugLog("PSG%d: R%d (%s) = $%02X (%d)", psgId_, currentRegister_,
                 getRegisterName(currentRegister_), value, value);
    }
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
    // R14 and R15 are the two I/O ports. Set to input (R7 bits 6 and 7 clear)
    // a read returns the pins, and a Mockingboard leaves them unconnected, so
    // they float high. Set to output, the read returns what was written.
    if (currentRegister_ == REG_IO_PORT_A && !(registers_[REG_MIXER] & 0x40)) return 0xFF;
    if (currentRegister_ == REG_IO_PORT_B && !(registers_[REG_MIXER] & 0x80)) return 0xFF;
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
    if (period == 0) period = 1;

    noiseCounter_++;
    // Noise runs at clock/16 while tones run at clock/8
    // Since we step at clock/8 rate, double the period comparison
    if (noiseCounter_ >= static_cast<uint32_t>(period) * 2) {
        noiseCounter_ = 0;

        // MAME-style 17-bit LFSR: feedback = bit0 XOR bit3, inject at bit16, shift right
        // Noise output is directly bit 0 of the shift register (no toggle mechanism)
        uint32_t feedback = (noiseShiftReg_ & 1) ^ ((noiseShiftReg_ >> 3) & 1);
        noiseShiftReg_ = (noiseShiftReg_ >> 1) | (feedback << 16);
    }
}

void AY8910::updateEnvelopeGenerator() {
    if (envHolding_) return;

    uint16_t period = getEnvPeriod();

    envCounter_++;
    // The datasheet's fE = fCLOCK / (256 * EP) is one ramp, and an
    // AY-3-8910's ramp is 16 steps, so a step is 16 * EP master clocks: 2 * EP
    // of these clock/8 ticks. (The 32-step ramp in the same time is the
    // YM2149's.) FUSE, which AppleWin's PSG comes from, gets the same rate by
    // counting EP at clock/16. Period 0 behaves as period 1.
    uint32_t effectivePeriod = (period == 0) ? 1 : period;
    uint32_t threshold = effectivePeriod * 2;
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

    uint8_t volume;
    if (ampReg & 0x10) {
        volume = envVolume_;
    } else {
        volume = ampReg & 0x0F;
    }

    if (volume == 0) return 0.0f;

    float level = volumeTable_[volume];

    // MAME mixer: output = (tone_out | tone_disable) & (noise_out | noise_disable)
    // Register bit = 1 means disabled (bypassed/always high)
    // Unipolar output matching real hardware: 0 or +level (never negative)
    bool toneDisable = (mixer & (1 << channel)) != 0;
    bool noiseDisable = (mixer & (1 << (channel + 3))) != 0;

    bool toneOut = toneOutput_[channel] || toneDisable;
    bool noiseOut = ((noiseShiftReg_ & 1) != 0) || noiseDisable;

    return (toneOut && noiseOut) ? level : 0.0f;
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

float AY8910::computeMixerOutput() const {
    uint8_t mixer = registers_[REG_MIXER];
    float sample = 0.0f;
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        if (channelMuted_[ch]) continue;

        uint8_t ampReg = registers_[REG_AMP_A + ch];
        uint8_t volume = (ampReg & 0x10) ? envVolume_ : (ampReg & 0x0F);
        if (volume == 0) continue;

        float level = volumeTable_[volume];

        bool toneDisable = (mixer & (1 << ch)) != 0;
        bool noiseDisable = (mixer & (1 << (ch + 3))) != 0;
        bool toneOut = toneOutput_[ch] || toneDisable;
        bool noiseOut = ((noiseShiftReg_ & 1) != 0) || noiseDisable;

        sample += (toneOut && noiseOut) ? level : 0.0f;
    }
    return sample / 3.0f;
}

void AY8910::tick() {
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        updateToneGenerator(ch);
    }
    updateNoiseGenerator();
    updateEnvelopeGenerator();
    history_[historyPos_] = computeMixerOutput();
    historyPos_ = (historyPos_ + 1) & (FILTER_TAPS - 1);
}

float AY8910::nextSample(double ticks) {
    phaseAccumulator_ += ticks;
    while (phaseAccumulator_ >= 1.0) {
        phaseAccumulator_ -= 1.0;
        tick();
    }

    // phaseAccumulator_ is now how far past the newest tick the output time
    // is. The filter is centred FILTER_TAPS / 2 ticks behind it, so every tap
    // it needs has already been taken.
    int phase = static_cast<int>(phaseAccumulator_ * FILTER_PHASES);
    if (phase >= FILTER_PHASES) phase = FILTER_PHASES - 1;
    const float* row = resampleKernel().rows[phase];

    float out = 0.0f;
    int idx = historyPos_;
    for (int j = 0; j < FILTER_TAPS; j++) {
        idx = (idx - 1) & (FILTER_TAPS - 1);
        out += history_[idx] * row[j];
    }
    return out;
}

void AY8910::generateSamples(float* buffer, int count, int sampleRate) {
    const double ticks = clockHz_ / (static_cast<double>(sampleRate) * 8.0);
    for (int i = 0; i < count; i++) {
        buffer[i] = nextSample(ticks);
    }
}

float AY8910::generateSingleSample() {
    return nextSample(ticksPerSample_);
}

void AY8910::generateChannelSamples(float* buffer, int count, int sampleRate, int channel) {
    if (channel < 0 || channel >= NUM_CHANNELS) {
        for (int i = 0; i < count; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }

    const double toneStepsPerSample = clockHz_ / (static_cast<double>(sampleRate) * 8.0);

    for (int i = 0; i < count; i++) {
        phaseAccumulator_ += toneStepsPerSample;

        // Advance all generators (needed for accurate state)
        while (phaseAccumulator_ >= 1.0) {
            phaseAccumulator_ -= 1.0;
            tick();
        }

        // Unipolar output for visualization (no DC removal - shows raw waveform)
        uint8_t mixer = registers_[REG_MIXER];
        uint8_t ampReg = registers_[REG_AMP_A + channel];
        uint8_t volume = (ampReg & 0x10) ? envVolume_ : (ampReg & 0x0F);
        if (volume == 0) {
            buffer[i] = 0.0f;
            continue;
        }

        float level = volumeTable_[volume];

        bool toneDisable = (mixer & (1 << channel)) != 0;
        bool noiseDisable = (mixer & (1 << (channel + 3))) != 0;
        bool toneOut = toneOutput_[channel] || toneDisable;
        bool noiseOut = ((noiseShiftReg_ & 1) != 0) || noiseDisable;

        buffer[i] = (toneOut && noiseOut) ? level : 0.0f;
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
    buffer[offset++] = noiseToggle_ ? 1 : 0;

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
    noiseToggle_ = buffer[offset++] != 0;

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
