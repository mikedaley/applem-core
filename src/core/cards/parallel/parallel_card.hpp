/*
 * parallel_card.hpp - Apple Parallel Interface Card expansion card
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

#pragma once

#include "../expansion_card.hpp"
#include <cstdint>
#include <cstddef>
#include <functional>

namespace a2e {

/**
 * ParallelCard - Apple Parallel Interface Card (Grappler+ compatible)
 *
 * Implements a Centronics-compatible parallel interface card used for
 * connecting printers such as the Epson MX/FX series and, via adapter,
 * the Apple ImageWriter series.
 *
 * I/O Space ($C0n0-$C0nF):
 *   Offset 0: Data latch (write) / data read-back (read)
 *   Offset 1: Status register (read)
 *       Bit 7: BUSY      - printer busy (0 = ready)
 *       Bit 6: ACK       - acknowledge pulse (0 during ack)
 *       Bit 5: PAPER_OUT - out of paper
 *       Bit 4: SELECT    - printer online
 *       Bit 3: ERROR     - printer error
 *   Offset 2: Control register (read/write)
 *       Bit 0: STROBE    - data strobe (active low — falling edge latches data)
 *       Bit 1: AUTO_FEED - auto line feed after carriage return
 *       Bit 2: INIT      - reset printer (active low)
 *       Bit 3: SELECT_IN - take printer online
 *
 * ROM Space:
 *   $Cn00-$CnFF: 341-0005 "Parallel Printer" firmware (upper half of PROM 341-0057).
 *                Self-locating via $C080,X indexing — slot-independent.
 *
 * Typically installed in Slot 1.
 */
class ParallelCard : public ExpansionCard {
public:
    // A byte clocked out of the parallel port (host→device, on STROBE).
    // Device-agnostic: whatever is attached (printer, etc.) consumes it.
    using ParallelTxCallback = std::function<void(uint8_t)>;

    ParallelCard();
    ~ParallelCard() override = default;

    // Delete copy
    ParallelCard(const ParallelCard&) = delete;
    ParallelCard& operator=(const ParallelCard&) = delete;

    // Allow move
    ParallelCard(ParallelCard&&) = default;
    ParallelCard& operator=(ParallelCard&&) = default;

    // ===== ExpansionCard Interface =====

    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;

    uint8_t readROM(uint8_t offset) override;
    bool hasROM() const override { return true; }

    void reset() override;

    const char* getName() const override { return "Parallel Card"; }
    uint8_t getPreferredSlot() const override { return 1; }

    // State serialization
    static constexpr size_t STATE_SIZE = 8; // dataLatch + status + control + prevStrobe + slot + 3 reserved
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // ===== Centronics parallel port =====

    void setParallelTxCallback(ParallelTxCallback cb) { txCallback_ = std::move(cb); }

    // Slot number needed to generate correct ROM addresses
    void setSlotNumber(uint8_t slot) { slotNumber_ = slot; buildROM(); }

private:
    void buildROM();

    // Register state
    uint8_t dataLatch_  = 0x00;
    uint8_t statusReg_  = 0b01010000; // ACK=1(idle), SELECT=1, no errors, BUSY=0
    uint8_t controlReg_ = 0b00001100; // INIT=1, SELECT_IN=1, STROBE=1(inactive high)
    bool    prevStrobe_ = true;        // Previous STROBE bit state (true = high = inactive)

    uint8_t slotNumber_ = 1;

    // $Cn00 slot ROM (256 bytes) — 341-0005 "Parallel Printer" firmware.
    uint8_t rom_[256];

    ParallelTxCallback txCallback_;
};

} // namespace a2e
