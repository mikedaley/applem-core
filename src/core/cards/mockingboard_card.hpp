#pragma once

#include "expansion_card.hpp"
#include "mockingboard/via6522.hpp"
#include "mockingboard/ay8910.hpp"
#include <vector>

namespace a2e {

/**
 * MockingboardCard - Mockingboard Sound Card
 *
 * Implements the ExpansionCard interface with direct ownership of VIA/PSG chips.
 * The Mockingboard typically occupies slot 4, providing:
 * - I/O space: $C0C0-$C0CF (unused - Mockingboard uses ROM space for VIA access)
 *
 * Note: The Mockingboard is unusual in that it uses the slot ROM space
 * ($C400-$C4FF) for its VIA registers rather than the I/O space ($C0C0-$C0CF).
 * This is because it needs more than 16 bytes of address space.
 *
 * VIA 1: $C400-$C47F (bit 7 = 0) - left channel PSG
 * VIA 2: $C480-$C4FF (bit 7 = 1) - right channel PSG
 */
class MockingboardCard : public ExpansionCard {
public:
    using CycleCallback = std::function<uint64_t()>;

    // State size for serialization: enabled(1) + VIA1(32) + PSG1(32) + VIA2(32) + PSG2(32) = 129
    static constexpr size_t STATE_SIZE = 129;

    MockingboardCard();
    ~MockingboardCard() override = default;

    // Delete copy
    MockingboardCard(const MockingboardCard&) = delete;
    MockingboardCard& operator=(const MockingboardCard&) = delete;

    // Allow move
    MockingboardCard(MockingboardCard&&) = default;
    MockingboardCard& operator=(MockingboardCard&&) = default;

    // ===== ExpansionCard Interface =====

    // I/O space ($C0C0-$C0CF) - Mockingboard doesn't use this
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // ROM space ($C400-$C4FF) - Contains VIA registers
    uint8_t readROM(uint8_t offset) override;
    void writeROM(uint8_t offset, uint8_t value) override;
    bool hasROM() const override { return true; }

    bool hasExpansionROM() const override { return false; }

    void reset() override;
    void update(int cycles) override;

    void setIRQCallback(IRQCallback callback) override;
    void setCycleCallback(CycleCallback callback) override { cycleCallback_ = std::move(callback); }

    bool isIRQActive() const override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "Mockingboard"; }
    uint8_t getPreferredSlot() const override { return 4; }

    bool isEnabled() const override { return enabled_; }
    void setEnabled(bool enabled) override { enabled_ = enabled; }

    // ===== Audio Generation =====

    /**
     * Generate mono audio samples
     * @param buffer Output buffer
     * @param count Number of samples
     * @param sampleRate Sample rate in Hz
     */
    void generateSamples(float* buffer, int count, int sampleRate);

    /**
     * Generate stereo audio samples
     * @param buffer Output buffer (interleaved L/R)
     * @param count Number of sample frames
     * @param sampleRate Sample rate in Hz
     */
    void generateStereoSamples(float* buffer, int count, int sampleRate);

    /**
     * Enable/disable debug logging
     * @param enabled true to enable
     */
    void setDebugLogging(bool enabled);

    // ===== Debug Access =====
    const VIA6522& getVIA1() const { return via1_; }
    const VIA6522& getVIA2() const { return via2_; }
    const AY8910& getPSG1() const { return psg1_; }
    const AY8910& getPSG2() const { return psg2_; }
    AY8910& getPSG1() { return psg1_; }
    AY8910& getPSG2() { return psg2_; }

private:
    // Two VIA chips
    VIA6522 via1_;  // $C400-$C47F (bit 7 = 0)
    VIA6522 via2_;  // $C480-$C4FF (bit 7 = 1)

    // Two PSG chips
    AY8910 psg1_;   // Connected to VIA1 (left channel)
    AY8910 psg2_;   // Connected to VIA2 (right channel)

    // Enabled state
    bool enabled_ = true;

    // Callbacks
    CycleCallback cycleCallback_;

    // Preallocated audio buffers to avoid heap allocations in audio hot path
    mutable std::vector<float> audioBuffer1_;
    mutable std::vector<float> audioBuffer2_;
};

} // namespace a2e
