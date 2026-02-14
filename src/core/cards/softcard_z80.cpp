/*
 * softcard_z80.cpp - Microsoft Z-80 SoftCard expansion card
 *
 * CPU switching uses writes to the slot ROM space ($Cn00), NOT the I/O space.
 * This matches the real hardware per MAME source and Microsoft documentation.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "softcard_z80.hpp"
#include <cstring>

namespace a2e {

// Z80 clock is 2.041 MHz, 6502 is 1.023 MHz → ratio = 2:1
// Each 6502 cycle = 2 Z80 T-states
static constexpr int TSTATE_RATIO = 2;

SoftCardZ80::SoftCardZ80() {
    z80_.initialise(
        z80MemRead, z80MemWrite,
        z80IoRead, z80IoWrite,
        z80Contention, this);
}

void SoftCardZ80::reset() {
    z80_.reset(true);
    z80Active_ = false;
    z80Initialized_ = false;
    tstateAccumulator_ = 0;
}

uint16_t SoftCardZ80::translateAddress(uint16_t z80Addr) const {
    // SoftCard address mapping (per MAME a2softcard.cpp):
    //   Z80 $0000-$AFFF → Apple $1000-$BFFF  (main RAM, 44KB contiguous)
    //   Z80 $B000-$BFFF → Apple $D000-$DFFF  (Language Card bank 2)
    //   Z80 $C000-$CFFF → Apple $E000-$EFFF  (Language Card)
    //   Z80 $D000-$DFFF → Apple $F000-$FFFF  (Language Card)
    //   Z80 $E000-$EFFF → Apple $C000-$CFFF  (I/O space)
    //   Z80 $F000-$FFFF → Apple $0000-$0FFF  (zero page/stack)
    uint8_t highNibble = (z80Addr >> 12) & 0x0F;
    uint16_t offset = z80Addr & 0x0FFF;
    static constexpr uint8_t mapping[16] = {
        0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8,  // 0-7 → 1-8
        0x9, 0xA, 0xB,                              // 8-A → 9-B
        0xD, 0xE, 0xF,                              // B-D → D-F
        0xC, 0x0                                     // E-F → C, 0
    };
    return (static_cast<uint16_t>(mapping[highNibble]) << 12) | offset;
}

// --- Z80 callback trampolines ---

uint8_t SoftCardZ80::z80MemRead(uint16_t addr, void* param) {
    auto* self = static_cast<SoftCardZ80*>(param);
    uint16_t appleAddr = self->translateAddress(addr);
    if (self->memRead_) {
        return self->memRead_(appleAddr);
    }
    return 0xFF;
}

void SoftCardZ80::z80MemWrite(uint16_t addr, uint8_t data, void* param) {
    auto* self = static_cast<SoftCardZ80*>(param);
    uint16_t appleAddr = self->translateAddress(addr);

    // Z80 writing to Apple II $Cn00 deactivates the Z80
    // (Z80 $En00 maps to Apple $Cn00 via address translation)
    if (appleAddr == (0xC000 | (static_cast<uint16_t>(self->slotNumber_) << 8))) {
        self->deactivateZ80();
        return;
    }

    if (self->memWrite_) {
        self->memWrite_(appleAddr, data);
    }
}

uint8_t SoftCardZ80::z80IoRead(uint16_t addr, void* param) {
    (void)addr;
    (void)param;
    // Z80 I/O ports are not used by the SoftCard (CPU switching is via memory writes)
    return 0xFF;
}

void SoftCardZ80::z80IoWrite(uint16_t addr, uint8_t data, void* param) {
    (void)addr;
    (void)data;
    (void)param;
}

void SoftCardZ80::z80Contention(uint16_t addr, uint32_t tstates, void* param) {
    (void)addr;
    (void)tstates;
    (void)param;
    // No memory contention on Apple II (no ULA)
}

// --- ExpansionCard I/O ($C0n0-$C0nF) ---

uint8_t SoftCardZ80::readIO(uint8_t offset) {
    (void)offset;
    return 0xFF;
}

void SoftCardZ80::writeIO(uint8_t offset, uint8_t value) {
    (void)offset;
    (void)value;
}

uint8_t SoftCardZ80::peekIO(uint8_t offset) const {
    (void)offset;
    return 0xFF;
}

// --- Slot ROM space ($Cn00-$CnFF) ---

uint8_t SoftCardZ80::readROM(uint8_t offset) {
    (void)offset;
    // SoftCard has no readable ROM — returns floating bus
    return 0xFF;
}

void SoftCardZ80::writeROM(uint8_t offset, uint8_t value) {
    (void)value;
    // Writing to $Cn00 toggles the Z80 on/off
    // Per Microsoft documentation: write to $CN00 activates Z80
    if (offset == 0x00) {
        if (!z80Active_) {
            activateZ80();
        } else {
            deactivateZ80();
        }
    }
}

void SoftCardZ80::activateZ80() {
    z80Active_ = true;
    tstateAccumulator_ = 0;

    // First activation resets the Z80 (PC = $0000)
    if (!z80Initialized_) {
        z80_.reset(true);
        z80Initialized_ = true;
    }
}

void SoftCardZ80::deactivateZ80() {
    z80Active_ = false;
}

void SoftCardZ80::update(int cycles) {
    if (!z80Active_) return;

    // Convert 6502 cycles to Z80 T-states (2:1 ratio)
    int32_t tstates = cycles * TSTATE_RATIO;

    if (tstates > 0) {
        z80_.execute(static_cast<uint32_t>(tstates));
    }
}

// --- State serialization ---

static constexpr size_t SOFTCARD_STATE_SIZE = 64;

size_t SoftCardZ80::getStateSize() const {
    return SOFTCARD_STATE_SIZE;
}

size_t SoftCardZ80::serialize(uint8_t* buffer, size_t maxSize) const {
    if (maxSize < SOFTCARD_STATE_SIZE) return 0;
    memset(buffer, 0, SOFTCARD_STATE_SIZE);

    size_t pos = 0;

    auto writeLE16 = [&](uint16_t val) {
        buffer[pos++] = val & 0xFF;
        buffer[pos++] = (val >> 8) & 0xFF;
    };

    writeLE16(z80_.getRegister(Z80::WordReg::AF));
    writeLE16(z80_.getRegister(Z80::WordReg::BC));
    writeLE16(z80_.getRegister(Z80::WordReg::DE));
    writeLE16(z80_.getRegister(Z80::WordReg::HL));
    writeLE16(z80_.getRegister(Z80::WordReg::IX));
    writeLE16(z80_.getRegister(Z80::WordReg::IY));
    writeLE16(z80_.getRegister(Z80::WordReg::SP));
    writeLE16(z80_.getRegister(Z80::WordReg::PC));
    writeLE16(z80_.getRegister(Z80::WordReg::AltAF));
    writeLE16(z80_.getRegister(Z80::WordReg::AltBC));
    writeLE16(z80_.getRegister(Z80::WordReg::AltDE));
    writeLE16(z80_.getRegister(Z80::WordReg::AltHL));

    buffer[pos++] = z80_.getRegister(Z80::ByteReg::I);
    buffer[pos++] = z80_.getRegister(Z80::ByteReg::R);

    buffer[pos++] = z80_.getIFF1();
    buffer[pos++] = z80_.getIFF2();
    buffer[pos++] = z80_.getIMMode();
    buffer[pos++] = z80_.getHalted() ? 1 : 0;
    buffer[pos++] = z80Active_ ? 1 : 0;
    buffer[pos++] = z80Initialized_ ? 1 : 0;
    buffer[pos++] = slotNumber_;

    buffer[pos++] = tstateAccumulator_ & 0xFF;
    buffer[pos++] = (tstateAccumulator_ >> 8) & 0xFF;
    buffer[pos++] = (tstateAccumulator_ >> 16) & 0xFF;
    buffer[pos++] = (tstateAccumulator_ >> 24) & 0xFF;

    return SOFTCARD_STATE_SIZE;
}

size_t SoftCardZ80::deserialize(const uint8_t* buffer, size_t size) {
    if (size < SOFTCARD_STATE_SIZE) return 0;

    size_t pos = 0;

    auto readLE16 = [&]() -> uint16_t {
        uint16_t val = buffer[pos] | (buffer[pos + 1] << 8);
        pos += 2;
        return val;
    };

    z80_.setRegister(Z80::WordReg::AF, readLE16());
    z80_.setRegister(Z80::WordReg::BC, readLE16());
    z80_.setRegister(Z80::WordReg::DE, readLE16());
    z80_.setRegister(Z80::WordReg::HL, readLE16());
    z80_.setRegister(Z80::WordReg::IX, readLE16());
    z80_.setRegister(Z80::WordReg::IY, readLE16());
    z80_.setRegister(Z80::WordReg::SP, readLE16());
    z80_.setRegister(Z80::WordReg::PC, readLE16());
    z80_.setRegister(Z80::WordReg::AltAF, readLE16());
    z80_.setRegister(Z80::WordReg::AltBC, readLE16());
    z80_.setRegister(Z80::WordReg::AltDE, readLE16());
    z80_.setRegister(Z80::WordReg::AltHL, readLE16());

    z80_.setRegister(Z80::ByteReg::I, buffer[pos++]);
    z80_.setRegister(Z80::ByteReg::R, buffer[pos++]);

    z80_.setIFF1(buffer[pos++]);
    z80_.setIFF2(buffer[pos++]);
    z80_.setIMMode(buffer[pos++]);
    z80_.setHalted(buffer[pos++] != 0);
    z80Active_ = buffer[pos++] != 0;
    z80Initialized_ = buffer[pos++] != 0;
    slotNumber_ = buffer[pos++];

    tstateAccumulator_ = buffer[pos] | (buffer[pos + 1] << 8) |
                         (buffer[pos + 2] << 16) | (buffer[pos + 3] << 24);
    pos += 4;

    return SOFTCARD_STATE_SIZE;
}

} // namespace a2e
