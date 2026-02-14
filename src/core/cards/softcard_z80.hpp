/*
 * softcard_z80.hpp - Microsoft Z-80 SoftCard expansion card
 *
 * The SoftCard puts a Z80 CPU on an expansion card that shares the Apple II's
 * RAM via DMA, enabling CP/M to run.
 *
 * CPU switching is done via writes to the slot ROM space ($Cn00):
 * - 6502 writes to $Cn00 → activates Z80 (resets it on first activation), halts 6502
 * - Z80 writes to $En00 (which maps to Apple II $Cn00) → deactivates Z80, resumes 6502
 *
 * Address translation (per MAME/hardware schematics):
 *   Z80 $0000-$AFFF → Apple II $1000-$BFFF  (44KB contiguous RAM for CP/M)
 *   Z80 $B000-$BFFF → Apple II $D000-$DFFF  (Language Card bank 2)
 *   Z80 $C000-$CFFF → Apple II $E000-$EFFF  (Language Card)
 *   Z80 $D000-$DFFF → Apple II $F000-$FFFF  (Language Card)
 *   Z80 $E000-$EFFF → Apple II $C000-$CFFF  (I/O space)
 *   Z80 $F000-$FFFF → Apple II $0000-$0FFF  (zero page/stack)
 *
 * The Z80 runs at 2.041 MHz (2x the 6502's 1.023 MHz clock).
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "expansion_card.hpp"
#include "../z80/z80.hpp"
#include <cstdint>
#include <functional>

namespace a2e {

class SoftCardZ80 : public ExpansionCard {
public:
    using MemReadFunc = std::function<uint8_t(uint16_t)>;
    using MemWriteFunc = std::function<void(uint16_t, uint8_t)>;
    using CpuHaltFunc = std::function<void(bool)>;  // true = halt 6502

    SoftCardZ80();
    ~SoftCardZ80() override = default;

    // ExpansionCard interface
    uint8_t readIO(uint8_t offset) override;
    void writeIO(uint8_t offset, uint8_t value) override;
    uint8_t peekIO(uint8_t offset) const override;
    uint8_t readROM(uint8_t offset) override;
    void writeROM(uint8_t offset, uint8_t value) override;
    bool hasROM() const override { return false; }
    void reset() override;
    void update(int cycles) override;
    const char* getName() const override { return "Z-80 SoftCard"; }

    // State serialization
    size_t getStateSize() const override;
    size_t serialize(uint8_t* buffer, size_t maxSize) const override;
    size_t deserialize(const uint8_t* buffer, size_t size) override;

    // Configuration
    void setSlotNumber(uint8_t slot) { slotNumber_ = slot; }
    void setMemReadCallback(MemReadFunc cb) { memRead_ = std::move(cb); }
    void setMemWriteCallback(MemWriteFunc cb) { memWrite_ = std::move(cb); }
    void setCpuHaltCallback(CpuHaltFunc cb) { cpuHalt_ = std::move(cb); }

    // State queries
    bool isZ80Active() const { return z80Active_; }

private:
    // Address translation: Z80 address → Apple II address (piecewise mapping)
    uint16_t translateAddress(uint16_t z80Addr) const;

    // Z80 memory/IO callbacks (static trampolines)
    static uint8_t z80MemRead(uint16_t addr, void* param);
    static void z80MemWrite(uint16_t addr, uint8_t data, void* param);
    static uint8_t z80IoRead(uint16_t addr, void* param);
    static void z80IoWrite(uint16_t addr, uint8_t data, void* param);
    static void z80Contention(uint16_t addr, uint32_t tstates, void* param);

    void activateZ80();
    void deactivateZ80();

    Z80 z80_;
    bool z80Active_ = false;
    bool z80Initialized_ = false;  // First activation resets the Z80
    uint8_t slotNumber_ = 0;

    // Fractional T-state accumulator for cycle-accurate Z80/6502 ratio
    // Z80 runs at ~2.041 MHz, 6502 at ~1.023 MHz → ratio = 2:1
    int32_t tstateAccumulator_ = 0;
    int traceCount_ = 0;  // Debug: limit trace output

    // Callbacks to access Apple II bus
    MemReadFunc memRead_;
    MemWriteFunc memWrite_;
    CpuHaltFunc cpuHalt_;
};

} // namespace a2e
