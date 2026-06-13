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
    buildROM();
}

void ParallelCard::buildROM() {
    // Authentic firmware. The real Apple Parallel Interface Card PROM (341-0057)
    // contains TWO 256-byte firmwares; on the real card DIP SW1:6 selects which
    // one maps to $Cn00 — "Parallel Printer" (341-0005, auto-LF after CR) in the
    // upper half at PROM offset $100, "Centronics" (341-0019, no auto-LF) in the
    // lower half. We default to Parallel Printer (no DIP yet). Apple peripheral
    // firmware self-locates its slot via $C080,X indexing, so the ROM image is
    // slot-independent — no per-slot patching.
    constexpr size_t kPrinterBase = 0x100; // 341-0005 "Parallel Printer" half
    if (roms::ROM_PARALLEL_SIZE >= kPrinterBase + sizeof(rom_)) {
        memcpy(rom_, roms::ROM_PARALLEL + kPrinterBase, sizeof(rom_));
        usingRealRom_ = true;
        return;
    }

    // Fallback: synthetic per-character output handler, used only when the ROM
    // file is absent from the build.
    //
    // Apple II output convention: PR#n sets CSW ($36/$37) = $Cn00, then COUT
    // does JMP ($36) into here ONCE PER CHARACTER with the char in A (high bit
    // set). We emit it via a bare STA to the data port (which fires the print
    // callback) and RTS. STA preserves A/X/Y, so the protocol contract that the
    // character survives the call is honoured. The JS printer strips the high
    // bit. No two-stage revectoring — that swallowed the first character and
    // clobbered A.
    usingRealRom_ = false;
    memset(rom_, 0xFF, sizeof(rom_));

    // I/O base: slot 1 = $C090, slot 2 = $C0A0, etc.
    uint16_t dataPort = 0xC080 + static_cast<uint16_t>(slotNumber_) * 0x10;
    int i = 0x00;
    rom_[i++] = 0x8D;                                          // STA dataPort (abs)
    rom_[i++] = static_cast<uint8_t>(dataPort & 0xFF);
    rom_[i++] = static_cast<uint8_t>(dataPort >> 8);
    rom_[i++] = 0x60;                                          // RTS
}

uint8_t ParallelCard::readIO(uint8_t offset) {
    switch (offset & 0x0F) {
        case 0x00: return dataLatch_;
        case 0x01: return statusReg_;
        case 0x02: return controlReg_;
        default:   return 0xFF;
    }
}

void ParallelCard::writeIO(uint8_t offset, uint8_t val) {
    switch (offset & 0x0F) {
        case 0x00:
            // Direct data write — fires callback immediately.
            // Matches the synthetic ROM which does a bare STA to this port.
            dataLatch_ = val;
            if (txCallback_) txCallback_(dataLatch_);
            break;

        case 0x02: {
            // STROBE is active-low (bit 0 = 0 = asserted).
            // Falling edge: data valid — fire callback for Centronics-aware software.
            bool strobe = (val & 0x01) != 0;
            if (prevStrobe_ && !strobe) {
                if (txCallback_) txCallback_(dataLatch_);
            }
            prevStrobe_ = strobe;
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
        case 0x01: return statusReg_;
        case 0x02: return controlReg_;
        default:   return 0xFF;
    }
}

uint8_t ParallelCard::readROM(uint8_t offset) {
    return rom_[offset];
}

void ParallelCard::reset() {
    dataLatch_  = 0x00;
    statusReg_  = 0b01010000;
    controlReg_ = 0b00001100;
    prevStrobe_ = true;
}

size_t ParallelCard::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < STATE_SIZE) return 0;
    size_t offset = 0;
    buffer[offset++] = dataLatch_;
    buffer[offset++] = statusReg_;
    buffer[offset++] = controlReg_;
    buffer[offset++] = prevStrobe_ ? 1 : 0;
    buffer[offset++] = slotNumber_;
    buffer[offset++] = 0; // reserved
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
    offset += 3; // reserved
    buildROM();
    return offset;
}

} // namespace a2e
