/*
 * test_cpu_cycle_counts.cpp - 65C02 instruction cycle count tests
 *
 * Verifies correct cycle counts for various instructions and
 * addressing modes, including page boundary crossing penalties.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "test_helpers.hpp"

// Helper: returns cycles consumed by the next instruction
static uint64_t measureCycles(a2e::CPU6502& cpu) {
    uint64_t before = cpu.getTotalCycles();
    cpu.executeInstruction();
    uint64_t after = cpu.getTotalCycles();
    return after - before;
}

// ============================================================================
// Basic Instruction Cycle Counts
// ============================================================================

TEST_CASE("Basic instruction cycle counts", "[cpu][cycles]") {

    SECTION("NOP takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xEA}); // NOP
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("LDA immediate takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42}); // LDA #$42
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("LDX immediate takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x10}); // LDX #$10
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("LDY immediate takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x10}); // LDY #$10
        REQUIRE(measureCycles(*f.cpu) == 2);
    }
}

// ============================================================================
// Zero Page Cycle Counts
// ============================================================================

TEST_CASE("Zero page instruction cycle counts", "[cpu][cycles]") {

    SECTION("LDA zero page takes 3 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA5, 0x10}); // LDA $10
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("STA zero page takes 3 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x85, 0x10}); // STA $10
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("LDA zero page,X takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xB5, 0x10}); // LDA $10,X
        REQUIRE(measureCycles(*f.cpu) == 4);
    }
}

// ============================================================================
// Absolute Addressing Cycle Counts
// ============================================================================

TEST_CASE("Absolute addressing cycle counts", "[cpu][cycles]") {

    SECTION("LDA absolute takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xAD, 0x00, 0x20}); // LDA $2000
        REQUIRE(measureCycles(*f.cpu) == 4);
    }

    SECTION("STA absolute takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x8D, 0x00, 0x20}); // STA $2000
        REQUIRE(measureCycles(*f.cpu) == 4);
    }
}

// ============================================================================
// Absolute,X with Page Crossing
// ============================================================================

TEST_CASE("Absolute,X cycle counts with page crossing", "[cpu][cycles]") {

    SECTION("LDA abs,X no page cross takes 4 cycles") {
        test::CPUTestFixture f;
        // X=$01, base=$2000 -> $2001, same page
        f.loadAndReset(0x0400, {0xA2, 0x01, 0xBD, 0x00, 0x20}); // LDX #$01; LDA $2000,X
        measureCycles(*f.cpu); // LDX
        REQUIRE(measureCycles(*f.cpu) == 4);
    }

    SECTION("LDA abs,X with page cross takes 5 cycles") {
        test::CPUTestFixture f;
        // X=$01, base=$20FF -> $2100, page crossed
        f.loadAndReset(0x0400, {0xA2, 0x01, 0xBD, 0xFF, 0x20}); // LDX #$01; LDA $20FF,X
        measureCycles(*f.cpu); // LDX
        REQUIRE(measureCycles(*f.cpu) == 5);
    }

    SECTION("STA abs,X always takes 5 cycles (no page cross optimization)") {
        test::CPUTestFixture f;
        // Stores always take the same cycles regardless of page cross
        f.loadAndReset(0x0400, {0xA2, 0x01, 0x9D, 0x00, 0x20}); // LDX #$01; STA $2000,X
        measureCycles(*f.cpu); // LDX
        REQUIRE(measureCycles(*f.cpu) == 5);
    }
}

// ============================================================================
// Absolute,Y with Page Crossing
// ============================================================================

TEST_CASE("Absolute,Y cycle counts with page crossing", "[cpu][cycles]") {

    SECTION("LDA abs,Y no page cross takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB9, 0x00, 0x20}); // LDY #$01; LDA $2000,Y
        measureCycles(*f.cpu); // LDY
        REQUIRE(measureCycles(*f.cpu) == 4);
    }

    SECTION("LDA abs,Y with page cross takes 5 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB9, 0xFF, 0x20}); // LDY #$01; LDA $20FF,Y
        measureCycles(*f.cpu); // LDY
        REQUIRE(measureCycles(*f.cpu) == 5);
    }
}

// ============================================================================
// (Indirect),Y with Page Crossing
// ============================================================================

TEST_CASE("(Indirect),Y cycle counts with page crossing", "[cpu][cycles]") {

    SECTION("LDA (zp),Y no page cross takes 5 cycles") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x00;
        f.mem[0x11] = 0x20;
        // Y=$01 -> $2000+$01 = $2001, same page
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB1, 0x10}); // LDY #$01; LDA ($10),Y
        measureCycles(*f.cpu); // LDY
        REQUIRE(measureCycles(*f.cpu) == 5);
    }

    SECTION("LDA (zp),Y with page cross takes 6 cycles") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xFF;
        f.mem[0x11] = 0x20;
        // Y=$01 -> $20FF+$01 = $2100, page crossed
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB1, 0x10}); // LDY #$01; LDA ($10),Y
        measureCycles(*f.cpu); // LDY
        REQUIRE(measureCycles(*f.cpu) == 6);
    }
}

// ============================================================================
// Branch Instruction Cycle Counts
// ============================================================================

TEST_CASE("Branch instruction cycle counts", "[cpu][cycles]") {

    SECTION("Branch not taken takes 2 cycles") {
        test::CPUTestFixture f;
        // LDA #$01 sets Z=0, then BEQ (not taken)
        f.loadAndReset(0x0400, {0xA9, 0x01, 0xF0, 0x02}); // LDA #$01; BEQ +2
        measureCycles(*f.cpu); // LDA
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("Branch taken same page takes 3 cycles") {
        test::CPUTestFixture f;
        // LDA #$00 sets Z=1, then BEQ +2 (taken, same page)
        f.loadAndReset(0x0400, {0xA9, 0x00, 0xF0, 0x02}); // LDA #$00; BEQ +2
        measureCycles(*f.cpu); // LDA
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("Branch taken crossing page takes 4 cycles") {
        test::CPUTestFixture f;
        // Place code near page boundary: $04FB: LDA #$00; BEQ +$05
        // BEQ at $04FD, PC after fetch = $04FF, target = $0504 (different page)
        // oldPC ($04FF) is in page $0400, target ($0504) is in page $0500
        f.mem.loadProgram(0x04FB, {0xA9, 0x00, 0xF0, 0x05});
        f.mem.setResetVector(0x04FB);
        f.cpu->reset();
        measureCycles(*f.cpu); // LDA
        REQUIRE(measureCycles(*f.cpu) == 4);
    }
}

// ============================================================================
// JSR / RTS Cycle Counts
// ============================================================================

TEST_CASE("JSR and RTS cycle counts", "[cpu][cycles]") {

    SECTION("JSR takes 6 cycles") {
        test::CPUTestFixture f;
        f.mem.loadProgram(0x0500, {0xEA}); // NOP at subroutine
        f.loadAndReset(0x0400, {0x20, 0x00, 0x05}); // JSR $0500
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("RTS takes 6 cycles") {
        test::CPUTestFixture f;
        f.mem.loadProgram(0x0500, {0x60}); // RTS at subroutine
        f.loadAndReset(0x0400, {0x20, 0x00, 0x05}); // JSR $0500
        measureCycles(*f.cpu); // JSR
        REQUIRE(measureCycles(*f.cpu) == 6); // RTS
    }
}

// ============================================================================
// BRK Cycle Count
// ============================================================================

TEST_CASE("BRK cycle count", "[cpu][cycles]") {

    SECTION("BRK takes 7 cycles") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem.loadProgram(0x1000, {0xEA});
        f.loadAndReset(0x0400, {0x00, 0xEA}); // BRK; padding
        REQUIRE(measureCycles(*f.cpu) == 7);
    }
}

// ============================================================================
// Read-Modify-Write Cycle Counts
// ============================================================================

TEST_CASE("Read-modify-write cycle counts", "[cpu][cycles]") {

    SECTION("INC absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xEE, 0x00, 0x20}); // INC $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("DEC absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xCE, 0x00, 0x20}); // DEC $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("ASL absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x0E, 0x00, 0x20}); // ASL $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("LSR absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x4E, 0x00, 0x20}); // LSR $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("ROL absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x2E, 0x00, 0x20}); // ROL $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("ROR absolute takes 6 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x6E, 0x00, 0x20}); // ROR $2000
        REQUIRE(measureCycles(*f.cpu) == 6);
    }

    SECTION("INC zero page takes 5 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xE6, 0x10}); // INC $10
        REQUIRE(measureCycles(*f.cpu) == 5);
    }

    SECTION("ASL accumulator takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x0A}); // ASL A
        REQUIRE(measureCycles(*f.cpu) == 2);
    }
}

// ============================================================================
// Stack Operation Cycle Counts
// ============================================================================

TEST_CASE("Stack operation cycle counts", "[cpu][cycles]") {

    SECTION("PHA takes 3 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x48}); // PHA
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("PLA takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x48, 0x68}); // PHA; PLA
        measureCycles(*f.cpu); // PHA
        REQUIRE(measureCycles(*f.cpu) == 4);
    }

    SECTION("PHP takes 3 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x08}); // PHP
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("PLP takes 4 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x08, 0x28}); // PHP; PLP
        measureCycles(*f.cpu); // PHP
        REQUIRE(measureCycles(*f.cpu) == 4);
    }
}

// ============================================================================
// Implied Instruction Cycle Counts
// ============================================================================

TEST_CASE("Implied instruction cycle counts", "[cpu][cycles]") {

    SECTION("INX takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xE8}); // INX
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("DEX takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xCA}); // DEX
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("TAX takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xAA}); // TAX
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("SEC takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x38}); // SEC
        REQUIRE(measureCycles(*f.cpu) == 2);
    }

    SECTION("CLC takes 2 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x18}); // CLC
        REQUIRE(measureCycles(*f.cpu) == 2);
    }
}

// ============================================================================
// JMP Cycle Counts
// ============================================================================

TEST_CASE("JMP cycle counts", "[cpu][cycles]") {

    SECTION("JMP absolute takes 3 cycles") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x4C, 0x00, 0x20}); // JMP $2000
        REQUIRE(measureCycles(*f.cpu) == 3);
    }

    SECTION("JMP indirect takes 5 cycles") {
        test::CPUTestFixture f;
        // Indirect vector at $3000 -> $2000
        f.mem[0x3000] = 0x00;
        f.mem[0x3001] = 0x20;
        f.loadAndReset(0x0400, {0x6C, 0x00, 0x30}); // JMP ($3000)
        // 65C02 JMP (abs) = 5 cycles (not 6 which is NMOS indirect)
        // Actually the cycle table shows opcode $6C = 5 for 65C02
        REQUIRE(measureCycles(*f.cpu) == 5);
    }
}

// ============================================================================
// (Indirect,X) Cycle Count
// ============================================================================

TEST_CASE("(Indirect,X) cycle count", "[cpu][cycles]") {

    SECTION("LDA (zp,X) takes 6 cycles") {
        test::CPUTestFixture f;
        f.mem[0x12] = 0x00;
        f.mem[0x13] = 0x20;
        f.loadAndReset(0x0400, {0xA2, 0x02, 0xA1, 0x10}); // LDX #$02; LDA ($10,X)
        measureCycles(*f.cpu); // LDX
        REQUIRE(measureCycles(*f.cpu) == 6);
    }
}
