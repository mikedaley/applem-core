/*
 * test_cpu_addressing.cpp - 65C02 addressing mode tests
 *
 * Tests all addressing modes: immediate, zero page, absolute,
 * indexed, indirect, relative, and 65C02-specific modes.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "test_helpers.hpp"

// ============================================================================
// Immediate Addressing
// ============================================================================

TEST_CASE("Immediate addressing mode", "[cpu][addressing]") {

    SECTION("LDA immediate reads operand byte directly") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xAB}); // LDA #$AB
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xAB);
        REQUIRE(f.cpu->getPC() == 0x0402);
    }

    SECTION("LDX immediate reads operand byte directly") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0xCD}); // LDX #$CD
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getX() == 0xCD);
        REQUIRE(f.cpu->getPC() == 0x0402);
    }
}

// ============================================================================
// Zero Page Addressing
// ============================================================================

TEST_CASE("Zero page addressing mode", "[cpu][addressing]") {

    SECTION("LDA zero page reads from page zero") {
        test::CPUTestFixture f;
        f.mem[0x42] = 0xBE;
        f.loadAndReset(0x0400, {0xA5, 0x42}); // LDA $42
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xBE);
        REQUIRE(f.cpu->getPC() == 0x0402);
    }

    SECTION("STA zero page writes to page zero") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xDD, 0x85, 0x50}); // LDA #$DD; STA $50
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x50] == 0xDD);
    }
}

// ============================================================================
// Zero Page,X Addressing
// ============================================================================

TEST_CASE("Zero page,X addressing mode", "[cpu][addressing]") {

    SECTION("LDA zp,X adds X to zero page address") {
        test::CPUTestFixture f;
        f.mem[0x15] = 0xAA;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0xB5, 0x10}); // LDX #$05; LDA $10,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xAA);
    }

    SECTION("LDA zp,X wraps within page zero") {
        test::CPUTestFixture f;
        // $FF + $02 should wrap to $01 (not $0101)
        f.mem[0x01] = 0xBB;
        f.loadAndReset(0x0400, {0xA2, 0x02, 0xB5, 0xFF}); // LDX #$02; LDA $FF,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xBB);
    }
}

// ============================================================================
// Zero Page,Y Addressing
// ============================================================================

TEST_CASE("Zero page,Y addressing mode", "[cpu][addressing]") {

    SECTION("LDX zp,Y adds Y to zero page address") {
        test::CPUTestFixture f;
        f.mem[0x18] = 0xCC;
        f.loadAndReset(0x0400, {0xA0, 0x08, 0xB6, 0x10}); // LDY #$08; LDX $10,Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0xCC);
    }

    SECTION("LDX zp,Y wraps within page zero") {
        test::CPUTestFixture f;
        f.mem[0x03] = 0xDD;
        f.loadAndReset(0x0400, {0xA0, 0x04, 0xB6, 0xFF}); // LDY #$04; LDX $FF,Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0xDD);
    }
}

// ============================================================================
// Absolute Addressing
// ============================================================================

TEST_CASE("Absolute addressing mode", "[cpu][addressing]") {

    SECTION("LDA absolute reads from 16-bit address") {
        test::CPUTestFixture f;
        f.mem[0x1234] = 0xEE;
        f.loadAndReset(0x0400, {0xAD, 0x34, 0x12}); // LDA $1234
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xEE);
        REQUIRE(f.cpu->getPC() == 0x0403);
    }

    SECTION("STA absolute writes to 16-bit address") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x77, 0x8D, 0x00, 0x30}); // LDA #$77; STA $3000
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x3000] == 0x77);
    }
}

// ============================================================================
// Absolute,X Addressing
// ============================================================================

TEST_CASE("Absolute,X addressing mode", "[cpu][addressing]") {

    SECTION("LDA abs,X adds X to address") {
        test::CPUTestFixture f;
        f.mem[0x1237] = 0x11;
        f.loadAndReset(0x0400, {0xA2, 0x03, 0xBD, 0x34, 0x12}); // LDX #$03; LDA $1234,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x11);
    }

    SECTION("LDA abs,X page cross works correctly") {
        test::CPUTestFixture f;
        f.mem[0x1300] = 0x22;
        // $12FF + X=$01 = $1300 (page boundary cross)
        f.loadAndReset(0x0400, {0xA2, 0x01, 0xBD, 0xFF, 0x12}); // LDX #$01; LDA $12FF,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x22);
    }

    SECTION("STA abs,X always takes same cycles regardless of page cross") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x33, 0xA2, 0x01, 0x9D, 0xFF, 0x12}); // LDA #$33; LDX #$01; STA $12FF,X
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.mem[0x1300] == 0x33);
    }
}

// ============================================================================
// Absolute,Y Addressing
// ============================================================================

TEST_CASE("Absolute,Y addressing mode", "[cpu][addressing]") {

    SECTION("LDA abs,Y adds Y to address") {
        test::CPUTestFixture f;
        f.mem[0x1239] = 0x44;
        f.loadAndReset(0x0400, {0xA0, 0x05, 0xB9, 0x34, 0x12}); // LDY #$05; LDA $1234,Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x44);
    }

    SECTION("LDA abs,Y page cross works correctly") {
        test::CPUTestFixture f;
        f.mem[0x1300] = 0x55;
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB9, 0xFF, 0x12}); // LDY #$01; LDA $12FF,Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x55);
    }
}

// ============================================================================
// (Indirect,X) Addressing
// ============================================================================

TEST_CASE("Indexed indirect (indirect,X) addressing mode", "[cpu][addressing]") {

    SECTION("LDA (zp,X) reads through zero-page pointer") {
        test::CPUTestFixture f;
        // Pointer at $12,$13 -> $2000; data at $2000 = $AA
        f.mem[0x12] = 0x00;
        f.mem[0x13] = 0x20;
        f.mem[0x2000] = 0xAA;
        f.loadAndReset(0x0400, {0xA2, 0x02, 0xA1, 0x10}); // LDX #$02; LDA ($10,X)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xAA);
    }

    SECTION("LDA (zp,X) wraps pointer lookup within page zero") {
        test::CPUTestFixture f;
        // X=$01, base=$FF -> effective ZP addr = $00 (wraps)
        f.mem[0x00] = 0x00;
        f.mem[0x01] = 0x30;
        f.mem[0x3000] = 0xBB;
        f.loadAndReset(0x0400, {0xA2, 0x01, 0xA1, 0xFF}); // LDX #$01; LDA ($FF,X)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xBB);
    }
}

// ============================================================================
// (Indirect),Y Addressing
// ============================================================================

TEST_CASE("Indirect indexed (indirect),Y addressing mode", "[cpu][addressing]") {

    SECTION("LDA (zp),Y reads through zero-page pointer and adds Y") {
        test::CPUTestFixture f;
        // Pointer at $10,$11 -> $2000; data at $2003 = $CC
        f.mem[0x10] = 0x00;
        f.mem[0x11] = 0x20;
        f.mem[0x2003] = 0xCC;
        f.loadAndReset(0x0400, {0xA0, 0x03, 0xB1, 0x10}); // LDY #$03; LDA ($10),Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xCC);
    }

    SECTION("LDA (zp),Y page cross works correctly") {
        test::CPUTestFixture f;
        // Pointer at $10,$11 -> $20FF; Y=$01 -> effective $2100 (page cross)
        f.mem[0x10] = 0xFF;
        f.mem[0x11] = 0x20;
        f.mem[0x2100] = 0xDD;
        f.loadAndReset(0x0400, {0xA0, 0x01, 0xB1, 0x10}); // LDY #$01; LDA ($10),Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xDD);
    }
}

// ============================================================================
// Relative Addressing (Branches)
// ============================================================================

TEST_CASE("Relative addressing mode", "[cpu][addressing]") {

    SECTION("Branch forward with positive offset") {
        test::CPUTestFixture f;
        // BRA +$04 at $0400 -> PC after fetch = $0402, target = $0406
        f.loadAndReset(0x0400, {0x80, 0x04}); // BRA +4
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x0406);
    }

    SECTION("Branch backward with negative offset") {
        test::CPUTestFixture f;
        // At $0410: BRA -$04 -> PC after fetch = $0412, target = $0412 + (-4) = $040E
        f.mem.loadProgram(0x0410, {0x80, 0xFC}); // BRA -4 (0xFC = -4 signed)
        f.mem.setResetVector(0x0410);
        f.cpu->reset();
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x040E);
    }

    SECTION("Branch not taken does not change PC beyond instruction") {
        test::CPUTestFixture f;
        // LDA #$01 (clears Z); BEQ +$04 -> should not branch
        f.loadAndReset(0x0400, {0xA9, 0x01, 0xF0, 0x04});
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getPC() == 0x0404); // Past the BEQ instruction
    }

    SECTION("Branch taken crosses page boundary") {
        test::CPUTestFixture f;
        // Place BRA at $04FE -> PC after fetch = $0500, branch +$10 -> $0510
        f.mem.loadProgram(0x04FE, {0x80, 0x10}); // BRA +$10
        f.mem.setResetVector(0x04FE);
        f.cpu->reset();
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x0510);
    }
}

// ============================================================================
// Indirect Addressing (JMP)
// ============================================================================

TEST_CASE("Indirect addressing for JMP", "[cpu][addressing]") {

    SECTION("JMP indirect reads 16-bit address from pointer") {
        test::CPUTestFixture f;
        f.mem[0x3000] = 0x00;
        f.mem[0x3001] = 0x50;
        f.loadAndReset(0x0400, {0x6C, 0x00, 0x30}); // JMP ($3000)
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x5000);
    }

    SECTION("JMP indirect on 65C02 does NOT have NMOS page boundary bug") {
        test::CPUTestFixture f;
        // On NMOS 6502, JMP ($10FF) reads low from $10FF and high from $1000
        // On 65C02, it correctly reads low from $10FF and high from $1100
        f.mem[0x10FF] = 0x34;
        f.mem[0x1100] = 0x12; // correct high byte (65C02)
        f.mem[0x1000] = 0xFF; // NMOS would incorrectly read this
        f.loadAndReset(0x0400, {0x6C, 0xFF, 0x10}); // JMP ($10FF)
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getPC() == 0x1234); // 65C02 correct behavior
    }
}

// ============================================================================
// 65C02: (ZP) Indirect Addressing
// ============================================================================

TEST_CASE("65C02 (zp) indirect addressing mode", "[cpu][addressing][65c02]") {

    SECTION("LDA (zp) reads through zero-page pointer without index") {
        test::CPUTestFixture f;
        f.mem[0x20] = 0x00;
        f.mem[0x21] = 0x40;
        f.mem[0x4000] = 0xEE;
        f.loadAndReset(0x0400, {0xB2, 0x20}); // LDA ($20)
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xEE);
    }

    SECTION("STA (zp) writes through zero-page pointer") {
        test::CPUTestFixture f;
        f.mem[0x20] = 0x00;
        f.mem[0x21] = 0x40;
        f.loadAndReset(0x0400, {0xA9, 0x55, 0x92, 0x20}); // LDA #$55; STA ($20)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x4000] == 0x55);
    }
}

// ============================================================================
// 65C02: (Absolute,X) Indirect Addressing (JMP)
// ============================================================================

TEST_CASE("65C02 (abs,X) indirect addressing for JMP", "[cpu][addressing][65c02]") {

    SECTION("JMP (abs,X) reads pointer at absolute+X") {
        test::CPUTestFixture f;
        // Jump table at $3000: entry 0=$1000, entry 1=$2000, entry 2=$5000
        f.mem[0x3004] = 0x00;
        f.mem[0x3005] = 0x50;
        f.loadAndReset(0x0400, {0xA2, 0x04, 0x7C, 0x00, 0x30}); // LDX #$04; JMP ($3000,X)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getPC() == 0x5000);
    }

    SECTION("JMP (abs,X) with X=0 reads from base address") {
        test::CPUTestFixture f;
        f.mem[0x3000] = 0x34;
        f.mem[0x3001] = 0x12;
        f.loadAndReset(0x0400, {0xA2, 0x00, 0x7C, 0x00, 0x30}); // LDX #$00; JMP ($3000,X)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getPC() == 0x1234);
    }
}
