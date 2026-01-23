#pragma once

#include <cstdint>
#include <array>

namespace a2e {

// AY-3-8910 Programmable Sound Generator emulation
// Used in the Mockingboard sound card (2 chips per card)
class AY8910 {
public:
    static constexpr int NUM_CHANNELS = 3;
    static constexpr int PSG_CLOCK = 1000000;  // 1 MHz clock for Mockingboard

    AY8910();

    // Register access via 6522 VIA
    void setRegisterAddress(uint8_t address);
    void writeRegister(uint8_t value);
    uint8_t readRegister() const;

    // Audio generation
    void generateSamples(float* buffer, int count, int sampleRate);

    // Reset
    void reset();

    // State access for debugging
    uint8_t getRegister(int reg) const {
        return (reg >= 0 && reg < 16) ? registers_[reg] : 0;
    }

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

    // Noise generator state
    uint32_t noiseCounter_ = 0;
    uint32_t noiseShiftReg_ = 1;  // 17-bit LFSR, must not be 0
    bool noiseOutput_ = false;

    // Envelope generator state
    uint32_t envCounter_ = 0;
    uint8_t envVolume_ = 0;
    bool envHolding_ = false;
    bool envAttack_ = false;
    bool envAlternate_ = false;
    bool envHold_ = false;

    // Fractional accumulator for sample rate conversion
    double phaseAccumulator_ = 0.0;

    // Volume table (4-bit to amplitude)
    static const float volumeTable_[16];

    // Helper methods
    uint16_t getTonePeriod(int channel) const;
    uint8_t getNoisePeriod() const;
    uint16_t getEnvPeriod() const;
    void updateToneGenerator(int channel);
    void updateNoiseGenerator();
    void updateEnvelopeGenerator();
    float getChannelOutput(int channel) const;
};

} // namespace a2e
