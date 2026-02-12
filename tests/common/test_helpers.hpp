/*
 * test_helpers.hpp - Shared test utilities for native C++ tests
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "cpu6502.hpp"

namespace test {

/**
 * FlatMemory - Simple 64KB memory for CPU testing
 *
 * Provides read/write callbacks compatible with CPU6502 constructor.
 * Includes helpers for loading programs, setting vectors, etc.
 */
class FlatMemory {
public:
    FlatMemory() { mem_.fill(0); }

    uint8_t read(uint16_t addr) const { return mem_[addr]; }
    void write(uint16_t addr, uint8_t val) { mem_[addr] = val; }

    // Set RESET vector ($FFFC-$FFFD)
    void setResetVector(uint16_t addr) {
        mem_[0xFFFC] = addr & 0xFF;
        mem_[0xFFFD] = (addr >> 8) & 0xFF;
    }

    // Set IRQ/BRK vector ($FFFE-$FFFF)
    void setIRQVector(uint16_t addr) {
        mem_[0xFFFE] = addr & 0xFF;
        mem_[0xFFFF] = (addr >> 8) & 0xFF;
    }

    // Set NMI vector ($FFFA-$FFFB)
    void setNMIVector(uint16_t addr) {
        mem_[0xFFFA] = addr & 0xFF;
        mem_[0xFFFB] = (addr >> 8) & 0xFF;
    }

    // Load a byte sequence at a given address
    void loadProgram(uint16_t addr, const std::vector<uint8_t>& bytes) {
        for (size_t i = 0; i < bytes.size(); i++) {
            mem_[static_cast<uint16_t>(addr + i)] = bytes[i];
        }
    }

    // Load raw byte array
    void loadProgram(uint16_t addr, const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            mem_[static_cast<uint16_t>(addr + i)] = data[i];
        }
    }

    // Direct array access
    uint8_t& operator[](uint16_t addr) { return mem_[addr]; }
    const uint8_t& operator[](uint16_t addr) const { return mem_[addr]; }

    // Clear all memory
    void clear() { mem_.fill(0); }

private:
    std::array<uint8_t, 65536> mem_;
};

/**
 * Execute exactly N instructions on the CPU
 */
inline void runInstructions(a2e::CPU6502& cpu, int count) {
    for (int i = 0; i < count; i++) {
        cpu.executeInstruction();
    }
}

/**
 * Run until PC reaches target address, with a maximum instruction limit.
 * Returns true if target was reached, false if limit was hit.
 */
inline bool runUntilPC(a2e::CPU6502& cpu, uint16_t target, int maxInstructions = 100000) {
    for (int i = 0; i < maxInstructions; i++) {
        if (cpu.getPC() == target) return true;
        cpu.executeInstruction();
    }
    return cpu.getPC() == target;
}

/**
 * Helper: create a CPU with FlatMemory and reset it to a given PC
 */
struct CPUTestFixture {
    FlatMemory mem;
    std::unique_ptr<a2e::CPU6502> cpu;

    CPUTestFixture(uint16_t startPC = 0x0400,
                   a2e::CPUVariant variant = a2e::CPUVariant::CMOS_65C02) {
        mem.setResetVector(startPC);
        cpu = std::make_unique<a2e::CPU6502>(
            [this](uint16_t addr) -> uint8_t { return mem.read(addr); },
            [this](uint16_t addr, uint8_t val) { mem.write(addr, val); },
            variant
        );
        cpu->reset();
    }

    void loadAndReset(uint16_t addr, const std::vector<uint8_t>& program) {
        mem.setResetVector(addr);
        mem.loadProgram(addr, program);
        cpu->reset();
    }
};

} // namespace test
