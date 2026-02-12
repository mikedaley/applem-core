/*
 * test_cpu_decimal.cpp - 65C02 BCD (decimal mode) arithmetic tests
 *
 * Tests ADC and SBC in decimal mode with various BCD inputs,
 * including carry/borrow, edge cases, and flag behavior.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "test_helpers.hpp"

// ============================================================================
// ADC in Decimal Mode
// ============================================================================

TEST_CASE("ADC in decimal mode", "[cpu][decimal]") {

    SECTION("Simple BCD addition: 15 + 27 = 42") {
        test::CPUTestFixture f;
        // SED; CLC; LDA #$15; ADC #$27
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x15, 0x69, 0x27});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x42);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == true);
    }

    SECTION("BCD addition: 25 + 35 = 60") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x25, 0x69, 0x35});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x60);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("BCD addition: 50 + 50 = 00 with carry (100 in BCD)") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x50, 0x69, 0x50});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("BCD addition with carry in: 15 + 27 + 1 = 43") {
        test::CPUTestFixture f;
        // SED; SEC; LDA #$15; ADC #$27
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x15, 0x69, 0x27});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x43);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("BCD addition: 99 + 01 = 00 with carry") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x99, 0x69, 0x01});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
    }

    SECTION("BCD addition: 99 + 99 = 98 with carry") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x99, 0x69, 0x99});
        test::runInstructions(*f.cpu, 4);
        // 99 + 99 = 198 in decimal -> 0x98 with carry
        REQUIRE(f.cpu->getA() == 0x98);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("BCD addition: 00 + 00 = 00") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x00, 0x69, 0x00});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }
}

// ============================================================================
// SBC in Decimal Mode
// ============================================================================

TEST_CASE("SBC in decimal mode", "[cpu][decimal]") {

    SECTION("Simple BCD subtraction: 42 - 15 = 27") {
        test::CPUTestFixture f;
        // SED; SEC; LDA #$42; SBC #$15
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x42, 0xE9, 0x15});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x27);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true); // no borrow
    }

    SECTION("BCD subtraction: 50 - 25 = 25") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x50, 0xE9, 0x25});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x25);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("BCD subtraction with borrow: 10 - 20 = 90 (with borrow)") {
        test::CPUTestFixture f;
        // SED; SEC; LDA #$10; SBC #$20
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x20});
        test::runInstructions(*f.cpu, 4);
        // 10 - 20 = -10 in decimal -> borrow -> result = 90
        REQUIRE(f.cpu->getA() == 0x90);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false); // borrow occurred
    }

    SECTION("BCD subtraction: 00 - 01 with borrow = 99") {
        test::CPUTestFixture f;
        // SED; SEC; LDA #$00; SBC #$01
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x00, 0xE9, 0x01});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x99);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == false);
    }

    SECTION("BCD subtraction: 42 - 42 = 00") {
        test::CPUTestFixture f;
        f.loadAndReset(0x0400, {0xF8, 0x38, 0xA9, 0x42, 0xE9, 0x42});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x00);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_Z) == true);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_C) == true);
    }

    SECTION("BCD subtraction with borrow in (carry clear): 42 - 15 - 1 = 26") {
        test::CPUTestFixture f;
        // SED; CLC; LDA #$42; SBC #$15
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x42, 0xE9, 0x15});
        test::runInstructions(*f.cpu, 4);
        REQUIRE(f.cpu->getA() == 0x26);
    }
}

// ============================================================================
// Decimal Mode Flag Preservation
// ============================================================================

TEST_CASE("Decimal mode flag preservation", "[cpu][decimal]") {

    SECTION("D flag remains set across multiple operations") {
        test::CPUTestFixture f;
        // SED; CLC; LDA #$10; ADC #$20; CLC; ADC #$05
        f.loadAndReset(0x0400, {0xF8, 0x18, 0xA9, 0x10, 0x69, 0x20, 0x18, 0x69, 0x05});
        test::runInstructions(*f.cpu, 6);
        // 10 + 20 = 30, then 30 + 05 = 35
        REQUIRE(f.cpu->getA() == 0x35);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == true);
    }

    SECTION("CLD clears decimal mode and subsequent ADC is binary") {
        test::CPUTestFixture f;
        // SED; CLD; CLC; LDA #$09; ADC #$01
        f.loadAndReset(0x0400, {0xF8, 0xD8, 0x18, 0xA9, 0x09, 0x69, 0x01});
        test::runInstructions(*f.cpu, 5);
        // Binary: $09 + $01 = $0A (not BCD $10)
        REQUIRE(f.cpu->getA() == 0x0A);
        REQUIRE(f.cpu->getFlag(a2e::FLAG_D) == false);
    }
}
