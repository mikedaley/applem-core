#pragma once

#include "expansion_card.hpp"
#include "../mockingboard/mockingboard.hpp"
#include <memory>

namespace a2e {

/**
 * MockingboardCard - Mockingboard Sound Card adapter
 *
 * Wraps the existing Mockingboard to implement the ExpansionCard interface.
 * The Mockingboard typically occupies slot 4, providing:
 * - I/O space: $C0C0-$C0CF (but actually uses $C4xx ROM space for VIA access)
 *
 * Note: The Mockingboard is unusual in that it uses the slot ROM space
 * ($C400-$C4FF) for its VIA registers rather than the I/O space ($C0C0-$C0CF).
 * This is because it needs more than 16 bytes of address space.
 *
 * VIA 1: $C400-$C40F (left channel PSG)
 * VIA 2: $C480-$C48F (right channel PSG)
 */
class MockingboardCard : public ExpansionCard {
public:
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
    void setCycleCallback(CycleCallback callback) override;

    bool isIRQActive() const override;

    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    const char* getName() const override { return "Mockingboard"; }
    uint8_t getPreferredSlot() const override { return 4; }

    bool isEnabled() const override;
    void setEnabled(bool enabled) override;

    // ===== Mockingboard Specific Methods =====

    /**
     * Get direct access to the underlying Mockingboard
     * Needed for audio generation and debugging
     * @return Reference to the Mockingboard
     */
    Mockingboard& getMockingboard() { return *mockingboard_; }
    const Mockingboard& getMockingboard() const { return *mockingboard_; }

    /**
     * Generate mono audio samples
     * @param buffer Output buffer
     * @param count Number of samples
     * @param sampleRate Sample rate in Hz
     */
    void generateSamples(float* buffer, int count, int sampleRate) {
        mockingboard_->generateSamples(buffer, count, sampleRate);
    }

    /**
     * Generate stereo audio samples
     * @param buffer Output buffer (interleaved L/R)
     * @param count Number of sample frames
     * @param sampleRate Sample rate in Hz
     */
    void generateStereoSamples(float* buffer, int count, int sampleRate) {
        mockingboard_->generateStereoSamples(buffer, count, sampleRate);
    }

    /**
     * Enable/disable debug logging
     * @param enabled true to enable
     */
    void setDebugLogging(bool enabled) {
        mockingboard_->setDebugLogging(enabled);
    }

    // Debug access
    const VIA6522& getVIA1() const { return mockingboard_->getVIA1(); }
    const VIA6522& getVIA2() const { return mockingboard_->getVIA2(); }
    const AY8910& getPSG1() const { return mockingboard_->getPSG1(); }
    const AY8910& getPSG2() const { return mockingboard_->getPSG2(); }

private:
    std::unique_ptr<Mockingboard> mockingboard_;
};

} // namespace a2e
