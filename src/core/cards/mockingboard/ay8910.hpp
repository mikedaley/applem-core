#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <functional>

namespace a2e {

// AY-3-8910 Programmable Sound Generator emulation
// Used in the Mockingboard sound card (2 chips per card)
class AY8910 {
public:
    static constexpr int NUM_CHANNELS = 3;
    // Mockingboard uses the Apple II CPU clock divided down
    // Apple IIe runs at 1.023 MHz (actually 1.0227272... MHz from 14.31818 MHz / 14)
    static constexpr int PSG_CLOCK = 1023000;  // ~1.023 MHz for accuracy

    using CycleCallback = std::function<uint64_t()>;

    AY8910();

    // Set PSG ID for debug logging (1 or 2)
    void setPsgId(int id);

    // Enable/disable console debug logging
    static void setDebugLogging(bool enabled);

    // Set callback to get current CPU cycle (for timestamping register writes)
    void setCycleCallback(CycleCallback callback) { cycleCallback_ = std::move(callback); }

    // Register access via 6522 VIA
    void setRegisterAddress(uint8_t address);
    void writeRegister(uint8_t value);
    uint8_t readRegister() const;

    // Audio generation - pass cycle range for proper timing
    void generateSamples(float* buffer, int count, int sampleRate, uint64_t startCycle, uint64_t endCycle);
    // Legacy version without timing (uses immediate register values)
    void generateSamples(float* buffer, int count, int sampleRate);
    void generateChannelSamples(float* buffer, int count, int sampleRate, int channel);

    // Channel muting (for debug/mixing purposes)
    void setChannelMute(int channel, bool muted);
    bool isChannelMuted(int channel) const;

    // Reset
    void reset();

    // State access for debugging
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
    bool noiseToggle_ = false;    // AppleWin/FUSE-style toggle output

    // Envelope generator state
    uint32_t envCounter_ = 0;
    uint8_t envVolume_ = 0;
    bool envHolding_ = false;
    bool envContinue_ = false;   // Bit 3: Continue after first cycle
    bool envAttack_ = false;     // Bit 2: Attack direction (1=up, 0=down)
    bool envAlternate_ = false;  // Bit 1: Alternate direction each cycle
    bool envHold_ = false;       // Bit 0: Hold final value

    // Fractional accumulator for sample rate conversion
    double phaseAccumulator_ = 0.0;

    // Single-pole low-pass filter to emulate analog output roll-off
    // Cutoff ~4kHz — tames square wave harmonics for a warmer/bassier sound
    static constexpr float LPF_CUTOFF_HZ = 4000.0f;
    float lpfState_ = 0.0f;

    // Volume table (4-bit to amplitude)
    static const float volumeTable_[16];

    // Debug counters
    uint32_t writeCount_ = 0;
    uint8_t lastWriteReg_ = 0;
    uint8_t lastWriteVal_ = 0;
    int psgId_ = 1;  // PSG identifier for logging

    // Timestamped register writes for accurate sample generation
    struct RegisterWrite {
        uint64_t cycle;
        uint8_t reg;
        uint8_t value;
    };
    std::vector<RegisterWrite> pendingWrites_;
    CycleCallback cycleCallback_;

    // Apply a register write (internal, doesn't timestamp)
    void applyRegisterWrite(uint8_t reg, uint8_t value);

    // Helper methods
    uint16_t getTonePeriod(int channel) const;
    uint8_t getNoisePeriod() const;
    uint16_t getEnvPeriod() const;
    void updateToneGenerator(int channel);
    void updateNoiseGenerator();
    void updateEnvelopeGenerator();
    void handleEnvelopeCycleEnd();
    float getChannelOutput(int channel) const;
};

} // namespace a2e
