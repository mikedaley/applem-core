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
 * I/O Space ($C0n0-$C0nF) — authentic Apple PIC 341-0057 decode (manual Table A-6):
 *   Offset 0 ($C0n0): Data latch (write) — latches the byte AND, when the autostrobe
 *                     is armed (firmware ROM page was addressed), fires the strobe
 *                     one-shot. Read returns the latched byte.
 *   Offset 1 ($C0n1): Not used on the real card (we also mirror status here for legacy).
 *   Offset 2 ($C0n2): Send-a-strobe (write, unconditional) / strobe (read). We keep it
 *                     as a readback register only — the data-latch write auto-strobes.
 *   Offset 3 ($C0n3): Read input port (data-in lines).
 *   Offset 4 ($C0n4): ACK status (READ) — the per-character handshake the firmware polls:
 *       Bit 7: ACK flip-flop status (tested via N/sign)
 *       Bit 6: ACK line          (tested via V — BVS OUT = "printer ready")
 *       Bit 4: SELECT            - printer online
 *   Offset 6 ($C0n6): Enable interrupt capability.
 *   Offset 7 ($C0n7): Disable interrupt; reset ACK flip-flop; DISABLE autostrobe.
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

    // Tick the Centronics BUSY/ACK handshake timer. The authentic 341-0005
    // firmware clocks a byte out (STA data latch) then busy-waits on the status
    // register for the printer to pulse BUSY high→low before sending the next.
    // A static "always ready" status never produces that edge, so the firmware
    // spins forever. We assert BUSY for a short window after each strobe and
    // decay it here, matching the real per-byte acknowledge.
    void update(int cycles) override;

    bool hasExpansionROM() const override { return hasExpansionRom_; }
    uint8_t readExpansionROM(uint16_t offset) override;

    void reset() override;

    const char* getName() const override { return "Parallel Card"; }
    uint8_t getPreferredSlot() const override { return 1; }

    // State serialization
    static constexpr size_t STATE_SIZE = 9; // dataLatch + status + control + prevStrobe + slot + romBank(2) + 2 reserved
    size_t getStateSize() const override { return STATE_SIZE; }
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // ===== Centronics parallel port =====

    void setParallelTxCallback(ParallelTxCallback cb) { txCallback_ = std::move(cb); }

    // Slot number needed to generate correct ROM addresses
    void setSlotNumber(uint8_t slot) { slotNumber_ = slot; buildROM(); }

private:
    void buildROM();

    // Status register ($C0n1) read value with the post-strobe BUSY/ACK
    // handshake transient overlaid on the idle baseline.
    uint8_t statusWithBusy() const;

    // Register state
    uint8_t dataLatch_  = 0x00;
    uint8_t statusReg_  = 0b01010000; // ACK=1(idle), SELECT=1, no errors, BUSY=0
    uint8_t controlReg_ = 0b00001100; // INIT=1, SELECT_IN=1, STROBE=1(inactive high)
    bool    prevStrobe_ = true;        // Previous STROBE bit state (true = high = inactive)

    uint8_t slotNumber_ = 1;

    // Centronics BUSY/ACK handshake: cycles remaining that the printer holds
    // BUSY asserted after a strobe. Decayed by update(); while > 0 the status
    // read reports BUSY=1 / ACK=0 so the firmware sees the byte-accepted edge.
    int busyCycles_ = 0;
    static constexpr int kStrobeBusyCycles = 64; // ~63us at 1.023MHz

    // ACK latch (the printer-acknowledge flip-flop). The single piece of
    // handshake state the firmware actually branches on. It feeds TWO things:
    //   1. The PROMSEL ROM-address remap in readROM() (bit-6 rewrite) — the real
    //      mechanism by which the 341-0005 firmware busy-waits, executing a
    //      different ROM byte-stream while a byte is in flight vs. once the
    //      printer is ready.
    //   2. The $C0n4 ACK status read.
    // Cleared by a data-latch strobe; re-set once the strobe one-shot expires
    // (update()), modelling the printer pulsing ACK after STROBE deasserts.
    bool ackLatch_ = true;

    // $Cn00 slot ROM (256 bytes) — 341-0005 "Parallel Printer" firmware.
    uint8_t rom_[256];

    // $C800-$CFFF expansion ROM (2KB window, bank-switched from a 4KB ROM).
    // Inactive until hasExpansionRom_ is set — current 341-0057 card has none.
    bool     hasExpansionRom_ = false;
    uint16_t romBank_         = 0;       // 0 or 0x800 — selects which 2KB half of the 4KB ROM
    uint8_t  expansionRom_[4096];

    ParallelTxCallback txCallback_;
};

} // namespace a2e
