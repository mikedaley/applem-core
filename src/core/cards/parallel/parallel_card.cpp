/*
 * parallel_card.cpp - Apple Parallel Interface Card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

#include "parallel_card.hpp"
#include "roms.cpp" // embedded ROM data (roms::ROM_PARALLEL)
#include <cstring>

namespace a2e {

ParallelCard::ParallelCard() {
    memset(expansionRom_, 0xFF, sizeof(expansionRom_));  // 4KB: bank0=0x000-0x7FF, bank1=0x800-0xFFF
    buildROM();
}

void ParallelCard::buildROM() {
    // The real Apple Parallel Interface Card PROM (341-0057) contains two
    // 256-byte firmwares; DIP SW1:6 selects which maps to $Cn00 —
    // "Parallel Printer" (341-0005, auto-LF after CR) in the upper half at
    // PROM offset $100, "Centronics" (341-0019, no auto-LF) in the lower half.
    // We default to Parallel Printer. Apple peripheral firmware self-locates
    // its slot via $C080,X indexing, so the ROM image is slot-independent.
    constexpr size_t kPrinterBase = 0x100; // 341-0005 "Parallel Printer" half
    memcpy(rom_, roms::ROM_PARALLEL + kPrinterBase, sizeof(rom_));
}

uint8_t ParallelCard::statusWithBusy() const {
    // ACK status read at $C0n4: bit7 = ACK flip-flop (tested via sign/N), bit6 =
    // ACK line (tested via V — BVS OUT = "printer ready"), bit4 = SELECT (online).
    // Derived from the same ack latch the PROMSEL ROM remap reads, so the I/O poll
    // and the ROM-stream branch always agree. Latch set = ready (0xD0); latch
    // clear = byte in flight / printer busy (0x10, SELECT only).
    uint8_t s = 0x10;                   // SELECT online
    if (ackLatch_) s |= 0xC0;           // ACK flip-flop + ACK line
    return s;
}

uint8_t ParallelCard::readIO(uint8_t offset) {
    switch (offset & 0x0F) {
        case 0x00: return dataLatch_;
        case 0x01: return statusWithBusy();   // legacy/aux; manual marks $C0n1 "not used"
        case 0x02: return controlReg_;
        // $C0n4 = ACK status — the address the 341-0005/341-0019 firmware actually
        // polls before each STA $C0n0 (Apple PIC manual Table A-6 p.23: bit6 = ACK
        // line, bit7 = ACK flip-flop). The firmware busy-waits here (BCC*/BCS* images,
        // BVS OUT) for the printer-ready edge. Serving the post-strobe BUSY/ACK
        // transient here is what lets a firmware-driven (PR#1) byte complete; a static
        // 0xFF made the ready edge never appear and the firmware spun forever.
        case 0x04: return statusWithBusy();
        // $C0n6 = enable interrupt capability. No card IRQ is wired in this
        // emulation; return status so a firmware probe reads something sane.
        case 0x06: return statusWithBusy();
        // $C0n7 = reset mode: resets the ACK flip-flop to the ready state (and on
        // real HW disables autostrobe/IRQ). The firmware reads here during init to
        // prime the handshake before the first character.
        case 0x07: ackLatch_ = true; return statusWithBusy();
        default:   return 0xFF;
    }
}

void ParallelCard::update(int cycles) {
    if (busyCycles_ > 0) {
        busyCycles_ -= cycles;
        if (busyCycles_ <= 0) {
            busyCycles_ = 0;
            // Strobe one-shot expired → the printer pulses ACK and the latch can
            // finally set. The real card refuses to latch ACK while STROBE is
            // still active (the "ack before strobe ends → wait forever" hazard),
            // which is exactly why a static-ready status never produced the edge.
            // This rising latch is the ready edge the firmware's ROM-mapped
            // busy-wait spins on.
            ackLatch_ = true;
        }
    }
}

void ParallelCard::writeIO(uint8_t offset, uint8_t val) {
    switch (offset & 0x0F) {
        case 0x00:
            // Writing the data latch fires the printer callback. On the real
            // Apple Parallel Interface Card the latch hardware auto-pulses the
            // Centronics STROBE one-shot, so a single STA $C0n0 clocks the byte
            // out — exactly what the 341-0005 firmware (and the synthetic STA
            // fallback) does. This is the one and only emit per byte.
            dataLatch_ = val;
            if (txCallback_) txCallback_(dataLatch_);
            // The strobe one-shot clears the ACK latch and starts the strobe
            // window. While the window is open the PROMSEL remap keeps the
            // firmware in its byte-in-flight ROM path; update() re-sets the latch
            // when the window expires (printer ACK), releasing the wait.
            ackLatch_ = false;
            busyCycles_ = kStrobeBusyCycles;
            break;

        case 0x02: {
            // Control register: track STROBE level for status read-back only.
            // The byte was already clocked out by the data-latch auto-strobe
            // above, so a manual STROBE toggle here must NOT re-emit it (doing
            // so double-printed every byte for strobe-aware drivers).
            prevStrobe_ = (val & 0x01) != 0;
            controlReg_ = val;
            break;
        }

        default:
            break;
    }
}

uint8_t ParallelCard::peekIO(uint8_t offset) const {
    switch (offset & 0x0F) {
        case 0x00: return dataLatch_;
        case 0x01: return statusWithBusy();
        case 0x02: return controlReg_;
        case 0x04: return statusWithBusy();   // ACK status ($C0n4) — see readIO
        default:   return 0xFF;
    }
}

uint8_t ParallelCard::readROM(uint8_t offset) {
    // PROMSEL — ACK-modulated ROM addressing (Apple PIC "standard"/X1 mode, which
    // the 341-0005 Parallel Printer firmware requires). The card hardware rewrites
    // ROM address bit 6 on every fetch from the ack latch, so the CPU runs a
    // *different* instruction stream depending on the printer handshake: the
    // firmware's per-byte busy-wait is implemented in the ROM map, not an I/O poll.
    // (This is also why $C100 — offset $00 — actually runs the canonical slot-entry
    // code stored at offset $40/$C140, not the raw byte at $00.) Mirrors MAME
    // a2bus/a2pic read_cnxx(). Without this remap the firmware fetches the wrong
    // bytes and spins forever — the PR#1 hang.
    uint8_t a = offset;
    if (!(a & 0x40) || ((a & 0x80) && !ackLatch_))
        a |= 0x40;                              // force ROM A6 high
    else
        a &= static_cast<uint8_t>(~0x40);       // force ROM A6 low ($BF mask)
    return rom_[a];
}

uint8_t ParallelCard::readExpansionROM(uint16_t offset) {
    return expansionRom_[(offset & 0x7FF) | romBank_];
}

void ParallelCard::reset() {
    dataLatch_  = 0x00;
    statusReg_  = 0b01010000;
    controlReg_ = 0b00001100;
    prevStrobe_ = true;
    busyCycles_ = 0;
    ackLatch_   = true;   // printer ready at idle (no byte in flight)
}

size_t ParallelCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;
    size_t offset = 0;
    buffer[offset++] = dataLatch_;
    buffer[offset++] = statusReg_;
    buffer[offset++] = controlReg_;
    buffer[offset++] = prevStrobe_ ? 1 : 0;
    buffer[offset++] = slotNumber_;
    buffer[offset++] = static_cast<uint8_t>(romBank_ >> 8);
    buffer[offset++] = static_cast<uint8_t>(romBank_ & 0xFF);
    buffer[offset++] = 0; // reserved
    buffer[offset++] = 0; // reserved
    return offset;
}

size_t ParallelCard::deserialize(const uint8_t* buffer, size_t size) {
    if (size < STATE_SIZE) return 0;
    size_t offset = 0;
    dataLatch_  = buffer[offset++];
    statusReg_  = buffer[offset++];
    controlReg_ = buffer[offset++];
    prevStrobe_ = buffer[offset++] != 0;
    slotNumber_ = buffer[offset++];
    romBank_    = (static_cast<uint16_t>(buffer[offset]) << 8) | buffer[offset + 1];
    offset += 2;
    offset += 2; // reserved
    buildROM();
    return offset;
}

} // namespace a2e
