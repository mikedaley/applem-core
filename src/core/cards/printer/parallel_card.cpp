/*
 * parallel_card.cpp - Apple Parallel Interface Card implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 *  Shawn Bullock <shawn@agenticexpert.ai>
 */

#include "parallel_card.hpp"
#include <cstring>

namespace a2e {

ParallelCard::ParallelCard() {
    buildROM();
}

void ParallelCard::buildROM() {
    memset(rom_, 0xFF, sizeof(rom_));

    // I/O base: slot 1 = $C090, slot 2 = $C0A0, etc.
    uint16_t dataPort  = 0xC080 + static_cast<uint16_t>(slotNumber_) * 0x10;

    // Output routine lives at $Cn10 within this card's ROM page
    uint16_t romBase    = 0xC000 + static_cast<uint16_t>(slotNumber_) * 0x100;
    uint16_t outputAddr = romBase + 0x10;

    // $Cn00 — PR#n init: sets COUT hook ($36/$37) to output routine
    int i = 0x00;
    rom_[i++] = 0xA9; rom_[i++] = static_cast<uint8_t>(outputAddr & 0xFF); // LDA #<outputAddr
    rom_[i++] = 0x85; rom_[i++] = 0x36;                                     // STA $36
    rom_[i++] = 0xA9; rom_[i++] = static_cast<uint8_t>(outputAddr >> 8);   // LDA #>outputAddr
    rom_[i++] = 0x85; rom_[i++] = 0x37;                                     // STA $37
    rom_[i++] = 0x60;                                                        // RTS

    // $Cn10 — output routine: char in A, write to data port, return
    i = 0x10;
    rom_[i++] = 0x8D; rom_[i++] = static_cast<uint8_t>(dataPort & 0xFF);
    rom_[i++] = static_cast<uint8_t>(dataPort >> 8);                        // STA dataPort (abs)
    rom_[i++] = 0x60;                                                        // RTS
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
            if (printCallback_) printCallback_(dataLatch_);
            break;

        case 0x02: {
            // STROBE is active-low (bit 0 = 0 = asserted).
            // Falling edge: data valid — fire callback for Centronics-aware software.
            bool strobe = (val & 0x01) != 0;
            if (prevStrobe_ && !strobe) {
                if (printCallback_) printCallback_(dataLatch_);
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
