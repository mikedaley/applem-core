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

// The AY-3-8910's 16 output levels, 0 silent and 15 the maximum, each
// doubled onto the 5-bit scale: level L is steps 2L and 2L + 1, so a fixed
// level (step 2L + 1) and the envelope's top four bits play the same value.
const float AY8910::ayLevels_[32] = {
    0.0000f, 0.0000f, 0.0137f, 0.0137f, 0.0205f, 0.0205f, 0.0291f, 0.0291f,
    0.0423f, 0.0423f, 0.0618f, 0.0618f, 0.0847f, 0.0847f, 0.1369f, 0.1369f,
    0.1691f, 0.1691f, 0.2647f, 0.2647f, 0.3527f, 0.3527f, 0.4499f, 0.4499f,
    0.5704f, 0.5704f, 0.6873f, 0.6873f, 0.8482f, 0.8482f, 1.0000f, 1.0000f
};

// The YM2149's D/A converter, from Yamaha's datasheet (YM2149, "2. D-A
// Convertor" and Fig. 1, "Output level of DA convertor"): logarithmic, the
// maximum normalised to 1, each of the 32 steps 1.5dB below the next
// (1, .841, .707, .595, .5 ... on the figure's axis), so step n is
// 2^((n - 31) / 4); step 0 is the figure's floor, silence. Fixed levels are
// labelled on every other step, 15 at the top: level L is step 2L + 1.
const float AY8910::ymLevels_[32] = {
    0.000000f, 0.005524f, 0.006570f, 0.007812f,
    0.009291f, 0.011049f, 0.013139f, 0.015625f,
    0.018581f, 0.022097f, 0.026278f, 0.031250f,
    0.037163f, 0.044194f, 0.052556f, 0.062500f,
    0.074325f, 0.088388f, 0.105112f, 0.125000f,
    0.148651f, 0.176777f, 0.210224f, 0.250000f,
    0.297302f, 0.353553f, 0.420448f, 0.500000f,
    0.594604f, 0.707107f, 0.840896f, 1.000000f,
};

void AY8910::setModel(Model model) {
    model_ = model;
    levels_ = (model == Model::YM2149) ? ymLevels_ : ayLevels_;
}

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
                envVolume_ = 31;  // Start at 31 for decay (falling)
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
    // The datasheets' fE = fCLOCK / (256 * EP) is one ramp. The YM2149's
    // counts 32 steps in it ("the envelope generator counts the envelope
    // clock fEA 32 times for each envelope pattern cycle"), a step every
    // 8 * EP master clocks: EP of these clock/8 ticks. An AY-3-8910 plays the
    // counter's top four bits, 16 steps of 2 * EP ticks in the same ramp.
    // Period 0 behaves as period 1.
    uint32_t threshold = (period == 0) ? 1 : period;
    if (envCounter_ >= threshold) {
        envCounter_ = 0;

        // Update envelope volume based on current direction
        if (envAttack_) {
            // Attack (rising)
            if (envVolume_ < 31) {
                envVolume_++;
            } else {
                // Reached max (31) - handle end of cycle
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
    // Called when envelope reaches its limit (0 or 31)
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
            // If we were decaying (going down), hold at 31
            envVolume_ = envAttack_ ? 0 : 31;
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
            envVolume_ = envAttack_ ? 0 : 31;
        }
    }
}

float AY8910::getChannelOutput(int channel) const {
    uint8_t mixer = registers_[REG_MIXER];
    uint8_t ampReg = registers_[REG_AMP_A + channel];

    float level = levels_[levelIndex(ampReg)];
    if (level == 0.0f) return 0.0f;

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

        float level = levels_[levelIndex(registers_[REG_AMP_A + ch])];
        if (level == 0.0f) continue;

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

void AY8910::advance(double ticks) {
    phaseAccumulator_ += ticks;
    while (phaseAccumulator_ >= 1.0) {
        phaseAccumulator_ -= 1.0;
        tick();
    }
}

float AY8910::sampleNow() const {
    // phaseAccumulator_ is how far past the newest tick the output time is.
    // The filter is centred FILTER_TAPS / 2 ticks behind it, so every tap it
    // needs has already been taken.
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

float AY8910::nextSample(double ticks) {
    advance(ticks);
    return sampleNow();
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
        float level = levels_[levelIndex(registers_[REG_AMP_A + channel])];
        if (level == 0.0f) {
            buffer[i] = 0.0f;
            continue;
        }

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

    // The envelope's form: 1, a 5-bit counter stepping every EP ticks. A
    // state without it (0, from the padding) has a 4-bit level stepping every
    // 2 EP ticks.
    buffer[offset++] = ENVELOPE_FORM_5BIT;

    // Pad to STATE_SIZE for consistent serialization
    while (offset < STATE_SIZE) {
        buffer[offset++] = 0;
    }

    return offset;  // Exactly STATE_SIZE bytes
}

// An older state's envelope: a 4-bit level and a count towards a step of
// 2 EP ticks. The same moment on the 5-bit counter is the first of the
// level's two steps in the direction it is going, or the second once EP of
// those ticks have passed.
void AY8910::convertFourBitEnvelope() {
    const uint8_t level = envVolume_ & 0x0F;
    if (envHolding_) {
        envVolume_ = level == 0 ? 0 : static_cast<uint8_t>(level * 2 + 1);
        return;
    }
    envVolume_ = static_cast<uint8_t>(level * 2 + (envAttack_ ? 0 : 1));
    const uint32_t period = getEnvPeriod() == 0 ? 1 : getEnvPeriod();
    if (envCounter_ >= period) {
        envCounter_ -= period;
        envVolume_ = envAttack_ ? envVolume_ + 1 : envVolume_ - 1;
    }
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
    if (buffer[offset++] != ENVELOPE_FORM_5BIT) convertFourBitEnvelope();

    // Restore envelope shape flags from register 13 (these are constant per shape)
    uint8_t envShape = registers_[REG_ENV_SHAPE] & 0x0F;
    envContinue_ = (envShape & 0x08) != 0;
    envAlternate_ = (envShape & 0x02) != 0;
    envHold_ = (envShape & 0x01) != 0;
}

} // namespace a2e
