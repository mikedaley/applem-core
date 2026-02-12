/*
 * test_cpu6502.cpp - Comprehensive 65C02 CPU instruction tests
 *
 * Tests all major instruction groups: loads, stores, arithmetic,
 * logic, shifts, compares, branches, jumps, stack, flags, transfers,
 * and 65C02-specific instructions.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "test_helpers.hpp"

// ============================================================================
// LDA - Load Accumulator
// ============================================================================

TEST_CASE("LDA instructions", "[cpu][load]") {

    SECTION("LDA immediate loads value") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42}); // LDA #$42
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("LDA immediate sets zero flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00}); // LDA #$00
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("LDA immediate sets negative flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x80}); // LDA #$80
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0x80);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
    }

    SECTION("LDA zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x77;
        f.loadAndReset(0x0400, {0xA5, 0x10}); // LDA $10
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0x77);
    }

    SECTION("LDA absolute") {
        test::CPUTestFixture f;
        f.mem[0x1234] = 0xAB;
        f.loadAndReset(0x0400, {0xAD, 0x34, 0x12}); // LDA $1234
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xAB);
    }

    SECTION("LDA zero page,X") {
        test::CPUTestFixture f;
        f.mem[0x15] = 0x99;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0xB5, 0x10}); // LDX #$05; LDA $10,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x99);
    }

    SECTION("LDA absolute,X") {
        test::CPUTestFixture f;
        f.mem[0x1237] = 0xCD;
        f.loadAndReset(0x0400, {0xA2, 0x03, 0xBD, 0x34, 0x12}); // LDX #$03; LDA $1234,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xCD);
    }

    SECTION("LDA absolute,Y") {
        test::CPUTestFixture f;
        f.mem[0x1236] = 0xEF;
        f.loadAndReset(0x0400, {0xA0, 0x02, 0xB9, 0x34, 0x12}); // LDY #$02; LDA $1234,Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xEF);
    }

    SECTION("LDA (indirect,X)") {
        test::CPUTestFixture f;
        f.mem[0x12] = 0x00;
        f.mem[0x13] = 0x20;
        f.mem[0x2000] = 0xBB;
        f.loadAndReset(0x0400, {0xA2, 0x02, 0xA1, 0x10}); // LDX #$02; LDA ($10,X)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xBB);
    }

    SECTION("LDA (indirect),Y") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x00;
        f.mem[0x11] = 0x20;
        f.mem[0x2003] = 0xCC;
        f.loadAndReset(0x0400, {0xA0, 0x03, 0xB1, 0x10}); // LDY #$03; LDA ($10),Y
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xCC);
    }
}

// ============================================================================
// LDX - Load X Register
// ============================================================================

TEST_CASE("LDX instructions", "[cpu][load]") {

    SECTION("LDX immediate loads value") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x33}); // LDX #$33
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getX() == 0x33);
    }

    SECTION("LDX immediate sets zero flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x00}); // LDX #$00
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("LDX immediate sets negative flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0xFE}); // LDX #$FE
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("LDX zero page") {
        test::CPUTestFixture f;
        f.mem[0x20] = 0x44;
        f.loadAndReset(0x0400, {0xA6, 0x20}); // LDX $20
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getX() == 0x44);
    }

    SECTION("LDX absolute") {
        test::CPUTestFixture f;
        f.mem[0x3000] = 0x55;
        f.loadAndReset(0x0400, {0xAE, 0x00, 0x30}); // LDX $3000
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getX() == 0x55);
    }
}

// ============================================================================
// LDY - Load Y Register
// ============================================================================

TEST_CASE("LDY instructions", "[cpu][load]") {

    SECTION("LDY immediate loads value") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x66}); // LDY #$66
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getY() == 0x66);
    }

    SECTION("LDY immediate sets zero flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x00}); // LDY #$00
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("LDY immediate sets negative flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x81}); // LDY #$81
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("LDY zero page") {
        test::CPUTestFixture f;
        f.mem[0x30] = 0x77;
        f.loadAndReset(0x0400, {0xA4, 0x30}); // LDY $30
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getY() == 0x77);
    }

    SECTION("LDY absolute") {
        test::CPUTestFixture f;
        f.mem[0x4000] = 0x88;
        f.loadAndReset(0x0400, {0xAC, 0x00, 0x40}); // LDY $4000
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getY() == 0x88);
    }
}

// ============================================================================
// STA/STX/STY - Store operations
// ============================================================================

TEST_CASE("Store instructions", "[cpu][store]") {

    SECTION("STA zero page") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0x85, 0x10}); // LDA #$42; STA $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0x42);
    }

    SECTION("STA absolute") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xAB, 0x8D, 0x00, 0x20}); // LDA #$AB; STA $2000
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x2000] == 0xAB);
    }

    SECTION("STA absolute,X") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xCD, 0xA2, 0x05, 0x9D, 0x00, 0x20}); // LDA #$CD; LDX #$05; STA $2000,X
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.mem[0x2005] == 0xCD);
    }

    SECTION("STA absolute,Y") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xEF, 0xA0, 0x03, 0x99, 0x00, 0x20}); // LDA #$EF; LDY #$03; STA $2000,Y
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.mem[0x2003] == 0xEF);
    }

    SECTION("STX zero page") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x55, 0x86, 0x20}); // LDX #$55; STX $20
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x20] == 0x55);
    }

    SECTION("STX absolute") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x77, 0x8E, 0x00, 0x30}); // LDX #$77; STX $3000
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x3000] == 0x77);
    }

    SECTION("STY zero page") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x66, 0x84, 0x30}); // LDY #$66; STY $30
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x30] == 0x66);
    }

    SECTION("STY absolute") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x88, 0x8C, 0x00, 0x40}); // LDY #$88; STY $4000
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x4000] == 0x88);
    }
}

// ============================================================================
// ADC - Add with Carry
// ============================================================================

TEST_CASE("ADC instructions", "[cpu][arithmetic]") {

    SECTION("ADC immediate simple addition") {
        test::CPUTestFixture f;
        // CLC; LDA #$10; ADC #$20
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x10, 0x69, 0x20});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x30);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("ADC with carry in") {
        test::CPUTestFixture f;
        // SEC; LDA #$10; ADC #$20
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x10, 0x69, 0x20});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x31);
    }

    SECTION("ADC produces carry out") {
        test::CPUTestFixture f;
        // CLC; LDA #$FF; ADC #$01
        f.loadAndReset(0x0400, {0x18, 0xA9, 0xFF, 0x69, 0x01});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("ADC sets overflow flag: positive + positive = negative") {
        test::CPUTestFixture f;
        // CLC; LDA #$50; ADC #$50
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x50, 0x69, 0x50});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0xA0);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("ADC sets overflow flag: negative + negative = positive") {
        test::CPUTestFixture f;
        // CLC; LDA #$D0; ADC #$90
        f.loadAndReset(0x0400, {0x18, 0xA9, 0xD0, 0x69, 0x90});
        test::runInstructions(*f.cpu, 3);
        // 0xD0 + 0x90 = 0x160, A = 0x60
        REQUIRE(f.cpu->getA() == 0x60);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("ADC zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x25;
        // CLC; LDA #$10; ADC $10
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x10, 0x65, 0x10});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x35);
    }
}

// ============================================================================
// SBC - Subtract with Carry (borrow)
// ============================================================================

TEST_CASE("SBC instructions", "[cpu][arithmetic]") {

    SECTION("SBC immediate simple subtraction") {
        test::CPUTestFixture f;
        // SEC; LDA #$50; SBC #$20
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x50, 0xE9, 0x20});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x30);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true); // no borrow
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("SBC with borrow (carry clear)") {
        test::CPUTestFixture f;
        // CLC; LDA #$50; SBC #$20
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x50, 0xE9, 0x20});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x2F); // 0x50 - 0x20 - 1 = 0x2F
    }

    SECTION("SBC produces borrow (carry clear result)") {
        test::CPUTestFixture f;
        // SEC; LDA #$10; SBC #$20
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x10, 0xE9, 0x20});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0xF0);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false); // borrow occurred
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("SBC result is zero") {
        test::CPUTestFixture f;
        // SEC; LDA #$30; SBC #$30
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x30, 0xE9, 0x30});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("SBC sets overflow: positive - negative = negative") {
        test::CPUTestFixture f;
        // SEC; LDA #$50; SBC #$B0
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x50, 0xE9, 0xB0});
        test::runInstructions(*f.cpu, 3);
        // signed: 80 - (-80) = 160, wraps -> 0xA0
        REQUIRE(f.cpu->getA() == 0xA0);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == true);
    }
}

// ============================================================================
// AND / ORA / EOR - Logical Operations
// ============================================================================

TEST_CASE("Logical instructions", "[cpu][logic]") {

    SECTION("AND immediate") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xFF, 0x29, 0x0F}); // LDA #$FF; AND #$0F
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x0F);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("AND resulting in zero") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xAA, 0x29, 0x55}); // LDA #$AA; AND #$55
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("AND zero page") {
        test::CPUTestFixture f;
        f.mem[0x20] = 0xF0;
        f.loadAndReset(0x0400, {0xA9, 0x3F, 0x25, 0x20}); // LDA #$3F; AND $20
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x30);
    }

    SECTION("ORA immediate") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x09, 0xF0}); // LDA #$0F; ORA #$F0
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("ORA resulting in zero") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00, 0x09, 0x00}); // LDA #$00; ORA #$00
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("EOR immediate") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xFF, 0x49, 0x0F}); // LDA #$FF; EOR #$0F
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xF0);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("EOR self produces zero") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xAA, 0x49, 0xAA}); // LDA #$AA; EOR #$AA
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }
}

// ============================================================================
// INC / DEC / INX / INY / DEX / DEY
// ============================================================================

TEST_CASE("Increment and decrement instructions", "[cpu][incdec]") {

    SECTION("INC zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x41;
        f.loadAndReset(0x0400, {0xE6, 0x10}); // INC $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0x42);
    }

    SECTION("INC zero page wraps from FF to 00") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xFF;
        f.loadAndReset(0x0400, {0xE6, 0x10}); // INC $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("INC absolute") {
        test::CPUTestFixture f;
        f.mem[0x2000] = 0x7F;
        f.loadAndReset(0x0400, {0xEE, 0x00, 0x20}); // INC $2000
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x2000] == 0x80);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("DEC zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x42;
        f.loadAndReset(0x0400, {0xC6, 0x10}); // DEC $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0x41);
    }

    SECTION("DEC zero page wraps from 00 to FF") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x00;
        f.loadAndReset(0x0400, {0xC6, 0x10}); // DEC $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("INX increments X register") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0xE8}); // LDX #$05; INX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0x06);
    }

    SECTION("INX wraps from FF to 00") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0xFF, 0xE8}); // LDX #$FF; INX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("INY increments Y register") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x0A, 0xC8}); // LDY #$0A; INY
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getY() == 0x0B);
    }

    SECTION("DEX decrements X register") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0xCA}); // LDX #$05; DEX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0x04);
    }

    SECTION("DEX wraps from 00 to FF") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x00, 0xCA}); // LDX #$00; DEX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("DEY decrements Y register") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x01, 0x88}); // LDY #$01; DEY
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getY() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }
}

// ============================================================================
// ASL / LSR / ROL / ROR - Shifts and Rotates
// ============================================================================

TEST_CASE("Shift and rotate instructions", "[cpu][shift]") {

    SECTION("ASL accumulator shifts left") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x41, 0x0A}); // LDA #$41; ASL A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x82);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("ASL accumulator sets carry from bit 7") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x80, 0x0A}); // LDA #$80; ASL A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("ASL zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x55;
        f.loadAndReset(0x0400, {0x06, 0x10}); // ASL $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0xAA);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("LSR accumulator shifts right") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x82, 0x4A}); // LDA #$82; LSR A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x41);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("LSR accumulator sets carry from bit 0") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x01, 0x4A}); // LDA #$01; LSR A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("LSR zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xAA;
        f.loadAndReset(0x0400, {0x46, 0x10}); // LSR $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0x55);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("ROL accumulator rotates left through carry") {
        test::CPUTestFixture f;
        // SEC; LDA #$40; ROL A -> bit 7=0, bit 0=carry(1) -> 0x81, carry=0
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x40, 0x2A});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x81);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("ROL accumulator sets carry from bit 7") {
        test::CPUTestFixture f;
        // CLC; LDA #$80; ROL A -> 0x00, carry=1
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x80, 0x2A});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("ROR accumulator rotates right through carry") {
        test::CPUTestFixture f;
        // SEC; LDA #$02; ROR A -> carry goes to bit 7, bit 0 goes to carry
        f.loadAndReset(0x0400, {0x38, 0xA9, 0x02, 0x6A});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x81);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("ROR accumulator sets carry from bit 0") {
        test::CPUTestFixture f;
        // CLC; LDA #$01; ROR A -> 0x00, carry=1
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x01, 0x6A});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("ROL zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x55;
        // CLC; ROL $10
        f.loadAndReset(0x0400, {0x18, 0x26, 0x10});
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0xAA);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("ROR zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xAA;
        // CLC; ROR $10
        f.loadAndReset(0x0400, {0x18, 0x66, 0x10});
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0x55);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }
}

// ============================================================================
// CMP / CPX / CPY - Compare Operations
// ============================================================================

TEST_CASE("Compare instructions", "[cpu][compare]") {

    SECTION("CMP immediate equal") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0xC9, 0x42}); // LDA #$42; CMP #$42
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("CMP immediate A > operand") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x50, 0xC9, 0x30}); // LDA #$50; CMP #$30
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false);
    }

    SECTION("CMP immediate A < operand") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x30, 0xC9, 0x50}); // LDA #$30; CMP #$50
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("CMP zero page") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x42;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0xC5, 0x10}); // LDA #$42; CMP $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("CPX immediate equal") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x10, 0xE0, 0x10}); // LDX #$10; CPX #$10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("CPX immediate X < operand") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0xE0, 0x10}); // LDX #$05; CPX #$10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("CPY immediate equal") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x20, 0xC0, 0x20}); // LDY #$20; CPY #$20
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("CPY immediate Y > operand") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x30, 0xC0, 0x10}); // LDY #$30; CPY #$10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
    }
}

// ============================================================================
// BIT - Test Bits
// ============================================================================

TEST_CASE("BIT instruction", "[cpu][bit]") {

    SECTION("BIT zero page sets Z when AND result is zero") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xF0;
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x24, 0x10}); // LDA #$0F; BIT $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);  // bit 7 of memory
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == true);   // bit 6 of memory
    }

    SECTION("BIT zero page clears Z when AND result is nonzero") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x3F;
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x24, 0x10}); // LDA #$0F; BIT $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == false); // bit 7 of memory is 0
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == false);  // bit 6 of memory is 0
    }

    SECTION("BIT absolute") {
        test::CPUTestFixture f;
        f.mem[0x2000] = 0xC0;
        f.loadAndReset(0x0400, {0xA9, 0x40, 0x2C, 0x00, 0x20}); // LDA #$40; BIT $2000
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false); // $40 AND $C0 = $40 != 0
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == true);
    }
}

// ============================================================================
// Branch Instructions
// ============================================================================

TEST_CASE("Branch instructions", "[cpu][branch]") {

    SECTION("BEQ taken when Z set") {
        test::CPUTestFixture f;
        // LDA #$00 (sets Z); BEQ +2; LDA #$FF; (target): LDA #$42
        f.loadAndReset(0x0400, {0xA9, 0x00, 0xF0, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 3); // LDA, BEQ, LDA at target
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BEQ not taken when Z clear") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x01, 0xF0, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0xFF);
    }

    SECTION("BNE taken when Z clear") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x01, 0xD0, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BNE not taken when Z set") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00, 0xD0, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0xFF);
    }

    SECTION("BCC taken when carry clear") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x18, 0x90, 0x02, 0xA9, 0xFF, 0xA9, 0x42}); // CLC; BCC +2
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BCS taken when carry set") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x38, 0xB0, 0x02, 0xA9, 0xFF, 0xA9, 0x42}); // SEC; BCS +2
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BMI taken when N set") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x80, 0x30, 0x02, 0xA9, 0xFF, 0xA9, 0x42}); // LDA #$80; BMI +2
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BPL taken when N clear") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x01, 0x10, 0x02, 0xA9, 0xFF, 0xA9, 0x42}); // LDA #$01; BPL +2
        test::runInstructions(*f.cpu, 3);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BVS taken when V set") {
        test::CPUTestFixture f;
        // CLC; LDA #$50; ADC #$50 (sets V); BVS +2; LDA #$FF; LDA #$42
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x50, 0x69, 0x50, 0x70, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 5);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("BVC taken when V clear") {
        test::CPUTestFixture f;
        // CLV; LDA #$01; BVC +2; LDA #$FF; LDA #$42
        f.loadAndReset(0x0400, {0xB8, 0xA9, 0x01, 0x50, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("Branch backward") {
        test::CPUTestFixture f;
        // 0400: LDX #$03
        // 0402: DEX       <- branch target
        // 0403: BNE $FD   (branch back to 0402)
        // 0405: (end)
        f.loadAndReset(0x0400, {0xA2, 0x03, 0xCA, 0xD0, 0xFD});
        // LDX #$03, then loop DEX;BNE 3 times -> X goes 3->2->1->0
        test::runUntilPC(*f.cpu, 0x0405, 100);
        REQUIRE(f.cpu->getX() == 0x00);
    }

    SECTION("BRA always branches (65C02)") {
        test::CPUTestFixture f;
        // BRA +2; LDA #$FF; LDA #$42
        f.loadAndReset(0x0400, {0x80, 0x02, 0xA9, 0xFF, 0xA9, 0x42});
        test::runInstructions(*f.cpu, 2); // BRA, LDA at target
        REQUIRE(f.cpu->getA() == 0x42);
    }
}

// ============================================================================
// JMP / JSR / RTS
// ============================================================================

TEST_CASE("Jump instructions", "[cpu][jump]") {

    SECTION("JMP absolute") {
        test::CPUTestFixture f;
        f.mem[0x2000] = 0xA9; // LDA #$42 at $2000
        f.mem[0x2001] = 0x42;
        f.loadAndReset(0x0400, {0x4C, 0x00, 0x20}); // JMP $2000
        test::runInstructions(*f.cpu, 2); // JMP, LDA
        REQUIRE(f.cpu->getA() == 0x42);
        REQUIRE(f.cpu->getPC() == 0x2002);
    }

    SECTION("JMP indirect") {
        test::CPUTestFixture f;
        // Indirect pointer at $3000 points to $2000
        f.mem[0x3000] = 0x00;
        f.mem[0x3001] = 0x20;
        f.mem[0x2000] = 0xA9;
        f.mem[0x2001] = 0x77;
        f.loadAndReset(0x0400, {0x6C, 0x00, 0x30}); // JMP ($3000)
        test::runInstructions(*f.cpu, 2); // JMP, LDA
        REQUIRE(f.cpu->getA() == 0x77);
    }

    SECTION("JSR and RTS") {
        test::CPUTestFixture f;
        // 0400: JSR $0500
        // 0403: LDA #$42     <- return here
        // 0500: LDX #$10     <- subroutine
        // 0502: RTS
        f.mem.loadProgram(0x0400, {0x20, 0x00, 0x05, 0xA9, 0x42});
        f.mem.loadProgram(0x0500, {0xA2, 0x10, 0x60});
        f.mem.setResetVector(0x0400);
        f.cpu->reset();
        test::runInstructions(*f.cpu, 4); // JSR, LDX, RTS, LDA
        REQUIRE(f.cpu->getX() == 0x10);
        REQUIRE(f.cpu->getA() == 0x42);
        REQUIRE(f.cpu->getPC() == 0x0405);
    }

    SECTION("JSR pushes return address minus one") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x20, 0x00, 0x05}); // JSR $0500
        uint8_t spBefore = f.cpu->getSP();
        f.cpu->executeInstruction();
        // SP should have decremented by 2
        REQUIRE(f.cpu->getSP() == static_cast<uint8_t>(spBefore - 2));
        // Stack should contain return address - 1 = $0402
        // The 6502 pushes PCH first, then PCL
        // The return address on stack is PC-1 = 0x0402
        // Stack at [SP+1] = low, [SP+2] = high
        uint8_t retLo = f.mem[0x0100 + f.cpu->getSP() + 1];
        uint8_t retHi = f.mem[0x0100 + f.cpu->getSP() + 2];
        uint16_t retAddr = (retHi << 8) | retLo;
        REQUIRE(retAddr == 0x0402);
    }
}

// ============================================================================
// Stack Instructions
// ============================================================================

TEST_CASE("Stack instructions", "[cpu][stack]") {

    SECTION("PHA pushes accumulator") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0x48}); // LDA #$42; PHA
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x01FD] == 0x42); // SP starts at $FD after reset, push writes to $01FD
    }

    SECTION("PLA pulls into accumulator") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0x48, 0xA9, 0x00, 0x68}); // LDA #$42; PHA; LDA #$00; PLA
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("PLA sets Z flag when pulling zero") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00, 0x48, 0xA9, 0xFF, 0x68}); // LDA #$00; PHA; LDA #$FF; PLA
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("PLA sets N flag when pulling negative") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x80, 0x48, 0xA9, 0x00, 0x68}); // LDA #$80; PHA; LDA #$00; PLA
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x80);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }

    SECTION("PHP pushes status with B and U flags set") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x08}); // PHP
        f.cpu->executeInstruction();
        uint8_t pushed = f.mem[0x01FD]; // SP starts at $FD after reset, push writes to $01FD
        // PHP pushes P with B (0x10) and U (0x20) always set
        REQUIRE((pushed & a2e::FLAG_B) != 0);
        REQUIRE((pushed & a2e::FLAG_U) != 0);
    }

    SECTION("PLP restores status register") {
        test::CPUTestFixture f;
        // SEC; SED; PHP; CLC; CLD; PLP
        f.loadAndReset(0x0400, {0x38, 0xF8, 0x08, 0x18, 0xD8, 0x28});
        test::runInstructions(*f.cpu, 6);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == true);
    }

    SECTION("PHX pushes X register (65C02)") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x99, 0xDA}); // LDX #$99; PHX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x01FD] == 0x99); // SP starts at $FD after reset, push writes to $01FD
    }

    SECTION("PLX pulls into X register (65C02)") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x55, 0xDA, 0xA2, 0x00, 0xFA}); // LDX #$55; PHX; LDX #$00; PLX
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getX() == 0x55);
    }

    SECTION("PHY pushes Y register (65C02)") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0xAA, 0x5A}); // LDY #$AA; PHY
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x01FD] == 0xAA); // SP starts at $FD after reset, push writes to $01FD
    }

    SECTION("PLY pulls into Y register (65C02)") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x77, 0x5A, 0xA0, 0x00, 0x7A}); // LDY #$77; PHY; LDY #$00; PLY
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getY() == 0x77);
    }
}

// ============================================================================
// Flag Instructions
// ============================================================================

TEST_CASE("Flag instructions", "[cpu][flags]") {

    SECTION("SEC sets carry") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x38}); // SEC
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("CLC clears carry") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x38, 0x18}); // SEC; CLC
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("SED sets decimal") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8}); // SED
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == true);
    }

    SECTION("CLD clears decimal") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0xD8}); // SED; CLD
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == false);
    }

    SECTION("SEI sets interrupt disable") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x78}); // SEI
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getFlag(a2e::FLAG_I) == true);
    }

    SECTION("CLI clears interrupt disable") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0x78, 0x58}); // SEI; CLI
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_I) == false);
    }

    SECTION("CLV clears overflow") {
        test::CPUTestFixture f;
        // Set overflow via ADC, then clear it
        // CLC; LDA #$50; ADC #$50 (sets V); CLV
        f.loadAndReset(0x0400, {0x18, 0xA9, 0x50, 0x69, 0x50, 0xB8});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_V) == false);
    }
}

// ============================================================================
// Transfer Instructions
// ============================================================================

TEST_CASE("Transfer instructions", "[cpu][transfer]") {

    SECTION("TAX transfers A to X") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0xAA}); // LDA #$42; TAX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getX() == 0x42);
    }

    SECTION("TAX sets zero flag") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00, 0xAA}); // LDA #$00; TAX
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("TXA transfers X to A") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x55, 0x8A}); // LDX #$55; TXA
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x55);
    }

    SECTION("TAY transfers A to Y") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x33, 0xA8}); // LDA #$33; TAY
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getY() == 0x33);
    }

    SECTION("TYA transfers Y to A") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA0, 0x77, 0x98}); // LDY #$77; TYA
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x77);
    }

    SECTION("TSX transfers SP to X") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xBA}); // TSX
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getX() == f.cpu->getSP());
    }

    SECTION("TXS transfers X to SP") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA2, 0x80, 0x9A}); // LDX #$80; TXS
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getSP() == 0x80);
    }

    SECTION("TXS does not affect flags") {
        test::CPUTestFixture f;
        // LDA #$01 (clears Z, clears N); LDX #$00; TXS
        f.loadAndReset(0x0400, {0xA9, 0x01, 0xA2, 0x00, 0x9A});
        test::runInstructions(*f.cpu, 3);
        // TXS does not modify N/Z flags (unlike other transfers)
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true); // Set by LDX #$00
    }
}

// ============================================================================
// NOP and BRK
// ============================================================================

TEST_CASE("NOP instruction", "[cpu][misc]") {

    SECTION("NOP does nothing") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0xEA, 0xEA, 0xEA}); // LDA #$42; NOP; NOP; NOP
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x42);
        REQUIRE(f.cpu->getPC() == 0x0405);
    }
}

TEST_CASE("BRK instruction", "[cpu][interrupt]") {

    SECTION("BRK pushes PC+2 and P, jumps to IRQ vector") {
        test::CPUTestFixture f;
        f.mem.setIRQVector(0x1000);
        f.mem[0x1000] = 0xA9; // LDA #$77 at IRQ handler
        f.mem[0x1001] = 0x77;
        f.loadAndReset(0x0400, {0x00, 0x00}); // BRK; padding byte
        f.cpu->setFlag(a2e::FLAG_I, false); // Clear I flag to start
        f.cpu->executeInstruction(); // BRK
        REQUIRE(f.cpu->getPC() == 0x1000);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_I) == true); // I flag set by BRK
    }
}

// ============================================================================
// 65C02 Specific Instructions
// ============================================================================

TEST_CASE("65C02 STZ instruction", "[cpu][65c02]") {

    SECTION("STZ zero page stores zero") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xFF;
        f.loadAndReset(0x0400, {0x64, 0x10}); // STZ $10
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x10] == 0x00);
    }

    SECTION("STZ absolute stores zero") {
        test::CPUTestFixture f;
        f.mem[0x2000] = 0xFF;
        f.loadAndReset(0x0400, {0x9C, 0x00, 0x20}); // STZ $2000
        f.cpu->executeInstruction();
        REQUIRE(f.mem[0x2000] == 0x00);
    }

    SECTION("STZ zero page,X") {
        test::CPUTestFixture f;
        f.mem[0x15] = 0xFF;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0x74, 0x10}); // LDX #$05; STZ $10,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x15] == 0x00);
    }

    SECTION("STZ absolute,X") {
        test::CPUTestFixture f;
        f.mem[0x2005] = 0xFF;
        f.loadAndReset(0x0400, {0xA2, 0x05, 0x9E, 0x00, 0x20}); // LDX #$05; STZ $2000,X
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x2005] == 0x00);
    }
}

TEST_CASE("65C02 TRB instruction", "[cpu][65c02]") {

    SECTION("TRB zero page resets tested bits") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xFF;
        // LDA #$0F; TRB $10 -> mem = mem AND NOT(A) = 0xFF & 0xF0 = 0xF0
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x14, 0x10});
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0xF0);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false); // A AND mem(original) = 0x0F != 0
    }

    SECTION("TRB sets Z when no bits in common") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xF0;
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x14, 0x10}); // LDA #$0F; TRB $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0xF0); // no bits to clear
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true); // A AND mem = 0x00
    }
}

TEST_CASE("65C02 TSB instruction", "[cpu][65c02]") {

    SECTION("TSB zero page sets tested bits") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xF0;
        // LDA #$0F; TSB $10 -> mem = mem OR A = 0xF0 | 0x0F = 0xFF
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x04, 0x10});
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true); // A AND mem(original) = 0x0F & 0xF0 = 0x00
    }

    SECTION("TSB clears Z when bits in common") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0xFF;
        f.loadAndReset(0x0400, {0xA9, 0x0F, 0x04, 0x10}); // LDA #$0F; TSB $10
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x10] == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == false); // A AND mem = 0x0F != 0
    }
}

TEST_CASE("65C02 INC A / DEC A", "[cpu][65c02]") {

    SECTION("INC A increments accumulator") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x41, 0x1A}); // LDA #$41; INC A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x42);
    }

    SECTION("INC A wraps from FF to 00") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0xFF, 0x1A}); // LDA #$FF; INC A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("DEC A decrements accumulator") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x42, 0x3A}); // LDA #$42; DEC A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0x41);
    }

    SECTION("DEC A wraps from 00 to FF") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xA9, 0x00, 0x3A}); // LDA #$00; DEC A
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.cpu->getA() == 0xFF);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_N) == true);
    }
}

TEST_CASE("65C02 (zp) indirect addressing", "[cpu][65c02]") {

    SECTION("LDA (zp) loads from indirect zero page address") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x00;
        f.mem[0x11] = 0x20;
        f.mem[0x2000] = 0xBB;
        f.loadAndReset(0x0400, {0xB2, 0x10}); // LDA ($10)
        f.cpu->executeInstruction();
        REQUIRE(f.cpu->getA() == 0xBB);
    }

    SECTION("STA (zp) stores to indirect zero page address") {
        test::CPUTestFixture f;
        f.mem[0x10] = 0x00;
        f.mem[0x11] = 0x20;
        f.loadAndReset(0x0400, {0xA9, 0xCC, 0x92, 0x10}); // LDA #$CC; STA ($10)
        test::runInstructions(*f.cpu, 2);
        REQUIRE(f.mem[0x2000] == 0xCC);
    }
}
