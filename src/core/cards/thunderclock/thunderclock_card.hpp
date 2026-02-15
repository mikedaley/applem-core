/*
 * thunderclock_card.hpp - Thunderclock Plus real-time clock card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../expansion_card.hpp"
#include <array>
#include <cstdint>

namespace a2e {

/**
 * ThunderclockCard - ProDOS-compatible clock card
 *
 * Implements the Thunderclock Plus clock card that provides automatic
 * date/time stamping for ProDOS applications. This eliminates the
 * "Enter today's date" prompts and enables proper file timestamps.
 *
 * ProDOS Detection:
 * ProDOS scans slots looking for specific ROM signature bytes:
 * - $Cn00: $08 (PHP instruction)
 * - $Cn02: $28 (signature byte)
 * - $Cn04: $58 (signature byte)
 * - $Cn06: $70 (signature byte)
 *
 * When ProDOS finds these bytes, it:
 * 1. Sets bit 0 of MACHID ($BF98) to indicate clock present
 * 2. Patches the clock driver address at $BF07-$BF08
 * 3. Changes $BF06 from $60 (RTS) to $4C (JMP)
 *
 * Hardware Interface:
 * The Thunderclock Plus uses a serial interface via the control register
 * at $C0n0 (where n = slot + 8). Time data is shifted out bit by bit.
 *
 * Control Register ($C0n0):
 * - Bit 2 (STROBE): Rising edge triggers command execution
 * - Bit 1 (CLOCK): Rising edge shifts next data bit
 * - Bits 3-5: Command (from uPD1990C C0-C2 pins)
 *
 * Time Data Format (40 bits, 10 BCD nibbles, LSB-first within each nibble):
 * - Second ones, Second tens (8 bits)
 * - Minute ones, Minute tens (8 bits)
 * - Hour ones, Hour tens (8 bits)
 * - Day ones, Day tens (8 bits)
 * - Day of week (4 bits, 0-6)
 * - Month (4 bits, 1-12)
 *
 * ROM Space:
 * - Slot ROM ($Cn00-$CnFF): 256 bytes - contains ProDOS driver
 * - Expansion ROM ($C800-$CFFF): 1792 bytes - contains utility routines
 *
 * Typically installed in Slot 5 or Slot 7.
 */
class ThunderclockCard : public ExpansionCard {
public:
    ThunderclockCard();
    ~ThunderclockCard() override = default;

    // Delete copy
    ThunderclockCard(const ThunderclockCard&) = delete;
    ThunderclockCard& operator=(const ThunderclockCard&) = delete;

    // Allow move
    ThunderclockCard(ThunderclockCard&&) = default;
    ThunderclockCard& operator=(ThunderclockCard&&) = default;

    // ===== ExpansionCard Interface =====

    // I/O space ($C0n0-$C0nF)
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    // ROM space ($Cn00-$CnFF)
    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    // Expansion ROM ($C800-$CFFF)
    bool hasExpansionROM() const override { return true; }
    uint8_t readExpansionROM(uint16_t offset) override;

    void reset() override;

    const char* getName() const override { return "Thunderclock"; }
    uint8_t getPreferredSlot() const override { return 5; }

    // State serialization
    static constexpr size_t STATE_SIZE = 72;  // 6 bytes state + 64 bytes bits + 2 reserved
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

private:
    // ROM data is loaded from embedded ROM file
    const uint8_t* rom_;       // Pointer to embedded ROM data
    size_t romSize_;           // Size of ROM (should be 2048)

    // Serial interface state
    bool strobe_ = false;      // Previous strobe state
    bool clock_ = false;       // Previous clock state
    uint8_t command_ = 0;      // Current command
    uint8_t register_ = 0;     // Output register (bit 7 is data out)

    // Time data as bit stream
    std::array<uint8_t, 64> bits_;  // Bit buffer
    int bitIndex_ = 0;              // Number of valid bits
    int currentBitIndex_ = 0;       // Current bit being read

    // Unused but kept for API compatibility
    std::array<uint8_t, 16> latches_;

    /**
     * Update the bit stream with current system time
     */
    void updateLatches();
};

} // namespace a2e
