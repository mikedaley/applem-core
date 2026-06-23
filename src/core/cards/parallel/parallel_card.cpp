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
            // Writing the data latch fires the printer callback. On the real
            // Apple Parallel Interface Card the latch hardware auto-pulses the
            // Centronics STROBE one-shot, so a single STA $C0n0 clocks the byte
            // out — exactly what the 341-0005 firmware (and the synthetic STA
            // fallback) does. This is the one and only emit per byte.
            dataLatch_ = val;
            if (txCallback_) txCallback_(dataLatch_);
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
        case 0x01: return statusReg_;
        case 0x02: return controlReg_;
        default:   return 0xFF;
    }
}

uint8_t ParallelCard::readROM(uint8_t offset) {
    return rom_[offset];
}

uint8_t ParallelCard::readExpansionROM(uint16_t offset) {
    return expansionRom_[(offset & 0x7FF) | romBank_];
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
