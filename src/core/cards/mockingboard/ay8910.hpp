/*
 * ay8910.hpp - AY-3-8910 sound chip emulation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <array>

namespace a2e {

// AY-3-8910 Programmable Sound Generator emulation
// Used in the Mockingboard sound card (2 chips per card)
class AY8910 {
public:
    static constexpr int NUM_CHANNELS = 3;
    // A Mockingboard clocks its PSGs from the slot's phi0, so the chip runs at
    // whatever the machine's bus does: 1.023MHz on an NTSC machine and about
    // 1.0179MHz on a PAL one. setClock() is how the card says which.
    static constexpr double DEFAULT_CLOCK_HZ = 1023000.0;

    // Output samples are taken from the chip's tick stream (clock / 8) through
    // a windowed-sinc low pass, so tones and noise above the host's Nyquist
    // limit are removed rather than folded back into the audible band.
    static constexpr int FILTER_TAPS = 64;      // ticks either side: 32
    static constexpr int FILTER_PHASES = 512;   // fractional tick positions

    AY8910();

    // The chip's master clock in Hz.
    void setClock(double hz);
    double getClock() const { return clockHz_; }

    // Set PSG ID for debug logging (1 or 2)
    void setPsgId(int id);

    // Enable/disable console debug logging
    static void setDebugLogging(bool enabled);

    // Register access via 6522 VIA
    void setRegisterAddress(uint8_t address);
    void writeRegister(uint8_t value);
    uint8_t readRegister() const;

    // Generate `count` band-limited samples at `sampleRate`, advancing the chip.
    void generateSamples(float* buffer, int count, int sampleRate);
    // One channel's raw output, point sampled, for the debugger's waveforms.
    void generateChannelSamples(float* buffer, int count, int sampleRate, int channel);

    // Generate a single band-limited sample at 48kHz using current register
    // state, advancing the chip by the ticks that sample covers. Used for
    // per-instruction incremental audio generation.
    float generateSingleSample();

    // Channel muting (for debug/mixing purposes)
    void setChannelMute(int channel, bool muted);
    bool isChannelMuted(int channel) const;

    // Reset
    void reset();

    // State access for debugging (the register as written, masked to its width)
    uint8_t getRegister(int reg) const {
        return (reg >= 0 && reg < 16) ? registers_[reg] : 0;
    }

    // Debug: track writes
    uint32_t getWriteCount() const { return writeCount_; }
    uint8_t getLastWriteReg() const { return lastWriteReg_; }
    uint8_t getLastWriteVal() const { return lastWriteVal_; }
    uint8_t getCurrentRegister() const { return currentRegister_; }

    // State serialization
    size_t exportState(uint8_t* buffer) const;
    void importState(const uint8_t* buffer);
    static constexpr size_t STATE_SIZE = 48;  // Expanded to include noise/envelope counters

private:
    // Registers
    static constexpr int REG_TONE_A_FINE = 0;
    static constexpr int REG_TONE_A_COARSE = 1;
    static constexpr int REG_TONE_B_FINE = 2;
    static constexpr int REG_TONE_B_COARSE = 3;
    static constexpr int REG_TONE_C_FINE = 4;
    static constexpr int REG_TONE_C_COARSE = 5;
    static constexpr int REG_NOISE_PERIOD = 6;
    static constexpr int REG_MIXER = 7;
    static constexpr int REG_AMP_A = 8;
    static constexpr int REG_AMP_B = 9;
    static constexpr int REG_AMP_C = 10;
    static constexpr int REG_ENV_FINE = 11;
    static constexpr int REG_ENV_COARSE = 12;
    static constexpr int REG_ENV_SHAPE = 13;
    static constexpr int REG_IO_PORT_A = 14;
    static constexpr int REG_IO_PORT_B = 15;

    // Register array
    std::array<uint8_t, 16> registers_{};
    uint8_t currentRegister_ = 0;

    // Tone generator state (3 channels)
    std::array<uint32_t, 3> toneCounters_{};
    std::array<bool, 3> toneOutput_{};

    // Channel mute state (for debug/visualization)
    std::array<bool, 3> channelMuted_{};

    // Noise generator state
    uint32_t noiseCounter_ = 0;
    uint32_t noiseShiftReg_ = 1;  // 17-bit LFSR, must not be 0
    bool noiseToggle_ = false;    // Legacy (kept for state serialization compat)

    // Envelope generator state
    uint32_t envCounter_ = 0;
    uint8_t envVolume_ = 0;
    bool envHolding_ = false;
    bool envContinue_ = false;   // Bit 3: Continue after first cycle
    bool envAttack_ = false;     // Bit 2: Attack direction (1=up, 0=down)
    bool envAlternate_ = false;  // Bit 1: Alternate direction each cycle
    bool envHold_ = false;       // Bit 0: Hold final value

    // Fractional accumulator for sample rate conversion: ticks since the
    // last one taken, in ticks.
    double phaseAccumulator_ = 0.0;

    // Master clock and the ticks one 48kHz sample covers.
    double clockHz_ = DEFAULT_CLOCK_HZ;
    double ticksPerSample_ = DEFAULT_CLOCK_HZ / (48000.0 * 8.0);

    // The last FILTER_TAPS tick outputs, newest at historyPos_ - 1.
    std::array<float, FILTER_TAPS> history_{};
    int historyPos_ = 0;

    // Volume table (4-bit to amplitude)
    static const float volumeTable_[16];

    // Debug counters
    uint32_t writeCount_ = 0;
    uint8_t lastWriteReg_ = 0;
    uint8_t lastWriteVal_ = 0;
    int psgId_ = 1;  // PSG identifier for logging

    // Apply a register write
    void applyRegisterWrite(uint8_t reg, uint8_t value);

    // Advance one tick (clock / 8) and record the mixer's output.
    void tick();
    // Advance by `ticks` and return the band-limited output at the new time.
    float nextSample(double ticks);

    // Helper methods
    uint16_t getTonePeriod(int channel) const;
    uint8_t getNoisePeriod() const;
    uint16_t getEnvPeriod() const;
    void updateToneGenerator(int channel);
    void updateNoiseGenerator();
    void updateEnvelopeGenerator();
    void handleEnvelopeCycleEnd();
    float getChannelOutput(int channel) const;
    // Compute raw mixer output (sum of all unmuted channels, normalized)
    float computeMixerOutput() const;
};

} // namespace a2e
