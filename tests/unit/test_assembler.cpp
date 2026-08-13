/*
 * test_assembler.cpp - Unit tests for 65C02 assembler
 *
 * Tests the assembler including:
 * - Simple instructions (NOP, LDA variants)
 * - Directives (ORG, DB/DFB, DW/DA, DS, ASC)
 * - Labels and forward references
 * - Branch instructions
 * - Symbol table
 * - Error reporting
 * - All addressing modes
 * - Multi-instruction programs
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "assembler.hpp"

#include <cstring>
#include <string>
#include <algorithm>

using namespace a2e;

// Helper to find a symbol by name in the result
static const AsmSymbol* findSymbol(const AsmResult& result, const char* name) {
    std::string upper(name);
    for (auto& c : upper) c = toupper(c);
    for (const auto& sym : result.symbols) {
        if (std::string(sym.name) == upper) return &sym;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Simple instructions
// ---------------------------------------------------------------------------

TEST_CASE("Assembler NOP produces correct output", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" NOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0xEA);
}

TEST_CASE("Assembler LDA immediate", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA #$42");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0xA9);
    CHECK(result.output[1] == 0x42);
}

TEST_CASE("Assembler LDA absolute", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $1234");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0xAD);
    CHECK(result.output[1] == 0x34);  // low byte
    CHECK(result.output[2] == 0x12);  // high byte
}

TEST_CASE("Assembler LDA zero page", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $42");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0xA5);
    CHECK(result.output[1] == 0x42);
}

TEST_CASE("Assembler STA absolute", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" STA $2000");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0x8D);
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x20);
}

TEST_CASE("Assembler RTS", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" RTS");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0x60);
}

// ---------------------------------------------------------------------------
// Directives
// ---------------------------------------------------------------------------

TEST_CASE("Assembler ORG directive sets origin", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $0800\n NOP");
    REQUIRE(result.success);
    CHECK(result.origin == 0x0800);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0xEA);
}

TEST_CASE("Assembler ORG directive at different address", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $2000\n NOP");
    REQUIRE(result.success);
    CHECK(result.origin == 0x2000);
}

TEST_CASE("Assembler DFB directive emits bytes", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DFB $01,$02,$03");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0x01);
    CHECK(result.output[1] == 0x02);
    CHECK(result.output[2] == 0x03);
}

TEST_CASE("Assembler DB directive emits bytes (alias for DFB)", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DB $FF,$00,$AA");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0xFF);
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0xAA);
}

TEST_CASE("Assembler DW directive emits little-endian word", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DW $1234");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0x34);  // low byte
    CHECK(result.output[1] == 0x12);  // high byte
}

TEST_CASE("Assembler DA directive emits little-endian word (alias for DW)", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DA $ABCD");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0xCD);
    CHECK(result.output[1] == 0xAB);
}

TEST_CASE("Assembler ASC directive emits characters", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ASC 'HI'");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 'H');
    CHECK(result.output[1] == 'I');
}

TEST_CASE("Assembler DS directive emits zero-filled space", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DS 3");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0x00);
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x00);
}

TEST_CASE("Assembler HEX directive emits hex data", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" HEX A0B0C0");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0xA0);
    CHECK(result.output[1] == 0xB0);
    CHECK(result.output[2] == 0xC0);
}

// ---------------------------------------------------------------------------
// Labels and forward references
// ---------------------------------------------------------------------------

TEST_CASE("Assembler labels resolve to correct address", "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble("LOOP NOP\n JMP LOOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 4);

    // NOP = 1 byte at origin (0x0800)
    CHECK(result.output[0] == 0xEA);

    // JMP LOOP = JMP $0800
    CHECK(result.output[1] == 0x4C);       // JMP absolute
    CHECK(result.output[2] == 0x00);       // low byte of $0800
    CHECK(result.output[3] == 0x08);       // high byte of $0800
}

TEST_CASE("Assembler forward reference resolves correctly", "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble(" JMP FWD\nFWD NOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 4);

    // JMP FWD at $0800 (3 bytes), FWD at $0803
    CHECK(result.output[0] == 0x4C);       // JMP absolute
    CHECK(result.output[1] == 0x03);       // low byte of $0803
    CHECK(result.output[2] == 0x08);       // high byte of $0803
    CHECK(result.output[3] == 0xEA);       // NOP at FWD
}

TEST_CASE("Assembler label with colon syntax", "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble("START: NOP\n JMP START");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 4);
    CHECK(result.output[1] == 0x4C);
    CHECK(result.output[2] == 0x00);
    CHECK(result.output[3] == 0x08);
}

// ---------------------------------------------------------------------------
// Branch instructions
// ---------------------------------------------------------------------------

TEST_CASE("Assembler short relative branch within range", "[asm][branch]") {
    Assembler asm_;
    auto result = asm_.assemble("LOOP NOP\n BNE LOOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);

    // NOP at $0800
    CHECK(result.output[0] == 0xEA);
    // BNE LOOP: from $0801, offset = $0800 - ($0801 + 2) = -3 = 0xFD
    CHECK(result.output[1] == 0xD0);   // BNE
    CHECK(result.output[2] == 0xFD);   // -3 relative offset
}

TEST_CASE("Assembler forward branch", "[asm][branch]") {
    Assembler asm_;
    auto result = asm_.assemble(" BEQ SKIP\n NOP\nSKIP NOP");
    REQUIRE(result.success);

    // BEQ at $0800 (2 bytes), NOP at $0802 (1 byte), SKIP at $0803
    // offset = $0803 - ($0800 + 2) = 1
    CHECK(result.output[0] == 0xF0);   // BEQ
    CHECK(result.output[1] == 0x01);   // +1 relative offset
}

TEST_CASE("Assembler BRA (65C02 unconditional branch)", "[asm][branch]") {
    Assembler asm_;
    auto result = asm_.assemble(" BRA DEST\nDEST NOP");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0x80);   // BRA opcode
}

TEST_CASE("Assembler zero-page operand does not inflate later labels",
          "[asm][branch][zeropage]") {
    Assembler asm_;
    // STA $06 is a 2-byte zero-page instruction. A pass-1 sizing bug that
    // defaulted every plain operand to 3-byte absolute pushed LOOP one byte
    // high, so the backward branch resolved to 0xFE (off by one) instead of
    // 0xFD, and the LOOP symbol reported $0803 instead of $0802.
    auto result = asm_.assemble(" STA $06\nLOOP NOP\n BNE LOOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 5);
    CHECK(result.output[0] == 0x85);   // STA zero-page (not 0x8D absolute)
    CHECK(result.output[1] == 0x06);
    CHECK(result.output[2] == 0xEA);   // NOP — LOOP, must be at $0802
    CHECK(result.output[3] == 0xD0);   // BNE
    CHECK(result.output[4] == 0xFD);   // -3 -> targets LOOP exactly

    const AsmSymbol* loop = findSymbol(result, "LOOP");
    REQUIRE(loop != nullptr);
    CHECK(loop->value == 0x0802);
}

TEST_CASE("Assembler EQU zero-page symbol sizes as 2 bytes in pass 1",
          "[asm][branch][zeropage]") {
    Assembler asm_;
    // Same drift via an EQU'd zero-page symbol defined before use, the common
    // idiom (GROW EQU $06 ... STA GROW). Must size as zero-page in pass 1.
    auto result = asm_.assemble("PTR EQU $06\n STA PTR\nLOOP NOP\n BNE LOOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 5);
    CHECK(result.output[0] == 0x85);   // STA zero-page
    CHECK(result.output[1] == 0x06);
    CHECK(result.output[4] == 0xFD);   // branch offset uncorrupted
}

// ---------------------------------------------------------------------------
// Symbols in result
// ---------------------------------------------------------------------------

TEST_CASE("Assembler symbols list contains defined labels", "[asm][symbols]") {
    Assembler asm_;
    auto result = asm_.assemble("START NOP\n RTS");
    REQUIRE(result.success);

    const AsmSymbol* sym = findSymbol(result, "START");
    REQUIRE(sym != nullptr);
    CHECK(sym->value == 0x0800);  // Default origin
}

TEST_CASE("Assembler EQU creates symbol with specified value", "[asm][symbols]") {
    Assembler asm_;
    auto result = asm_.assemble("SCREEN EQU $2000\n LDA SCREEN");
    REQUIRE(result.success);

    const AsmSymbol* sym = findSymbol(result, "SCREEN");
    REQUIRE(sym != nullptr);
    CHECK(sym->value == 0x2000);
}

TEST_CASE("Assembler multiple labels appear in symbols", "[asm][symbols]") {
    Assembler asm_;
    auto result = asm_.assemble("ONE NOP\nTWO NOP\nTHREE NOP");
    REQUIRE(result.success);

    CHECK(findSymbol(result, "ONE") != nullptr);
    CHECK(findSymbol(result, "TWO") != nullptr);
    CHECK(findSymbol(result, "THREE") != nullptr);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST_CASE("Assembler error on invalid mnemonic", "[asm][error]") {
    Assembler asm_;
    auto result = asm_.assemble(" XYZ");
    REQUIRE_FALSE(result.success);
    REQUIRE(result.errors.size() > 0);
}

TEST_CASE("Assembler error on undefined symbol", "[asm][error]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA UNDEFINED");
    REQUIRE_FALSE(result.success);
    REQUIRE(result.errors.size() > 0);
}

TEST_CASE("Assembler empty source returns success with no output", "[asm][edge]") {
    Assembler asm_;
    auto result = asm_.assemble("");
    REQUIRE(result.success);
    CHECK(result.output.empty());
}

TEST_CASE("Assembler comment-only lines are ignored", "[asm][edge]") {
    Assembler asm_;
    auto result = asm_.assemble("; This is a comment\n* Another comment\n NOP");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0xEA);
}

// ---------------------------------------------------------------------------
// Multiple instructions
// ---------------------------------------------------------------------------

TEST_CASE("Assembler multi-line program assembles correctly", "[asm][program]") {
    Assembler asm_;
    auto result = asm_.assemble(
        " ORG $0300\n"
        " LDA #$00\n"     // A9 00
        " STA $2000\n"    // 8D 00 20
        " RTS\n"          // 60
    );
    REQUIRE(result.success);
    CHECK(result.origin == 0x0300);
    REQUIRE(result.output.size() == 6);
    CHECK(result.output[0] == 0xA9);  // LDA #
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x8D);  // STA abs
    CHECK(result.output[3] == 0x00);
    CHECK(result.output[4] == 0x20);
    CHECK(result.output[5] == 0x60);  // RTS
}

TEST_CASE("Assembler endAddress is set after last instruction", "[asm][program]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $0300\n NOP\n NOP\n NOP");
    REQUIRE(result.success);
    CHECK(result.origin == 0x0300);
    CHECK(result.endAddress == 0x0303);
}

// ---------------------------------------------------------------------------
// All addressing modes
// ---------------------------------------------------------------------------

TEST_CASE("Assembler immediate addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDX #$10");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xA2);  // LDX #imm
    CHECK(result.output[1] == 0x10);
}

TEST_CASE("Assembler zero page addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $10");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xA5);
    CHECK(result.output[1] == 0x10);
}

TEST_CASE("Assembler zero page,X addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $10,X");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xB5);  // LDA zp,X
    CHECK(result.output[1] == 0x10);
}

TEST_CASE("Assembler zero page,Y addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDX $10,Y");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xB6);  // LDX zp,Y
    CHECK(result.output[1] == 0x10);
}

TEST_CASE("Assembler absolute addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $1000");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xAD);
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x10);
}

TEST_CASE("Assembler absolute,X addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $1000,X");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xBD);  // LDA abs,X
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x10);
}

TEST_CASE("Assembler absolute,Y addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA $1000,Y");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xB9);  // LDA abs,Y
    CHECK(result.output[1] == 0x00);
    CHECK(result.output[2] == 0x10);
}

TEST_CASE("Assembler indexed indirect (ind,X) addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA ($20,X)");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xA1);  // LDA (zp,X)
    CHECK(result.output[1] == 0x20);
}

TEST_CASE("Assembler indirect indexed (ind),Y addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA ($20),Y");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xB1);  // LDA (zp),Y
    CHECK(result.output[1] == 0x20);
}

TEST_CASE("Assembler zero page indirect (65C02) addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA ($20)");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xB2);  // LDA (zp) - 65C02
    CHECK(result.output[1] == 0x20);
}

TEST_CASE("Assembler relative addressing mode (branch)", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble("HERE BEQ HERE");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0xF0);  // BEQ
    CHECK(result.output[1] == 0xFE);  // -2 (branch to self)
}

TEST_CASE("Assembler implied addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" INX");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0xE8);
}

TEST_CASE("Assembler accumulator addressing mode", "[asm][addrmode]") {
    Assembler asm_;

    SECTION("ASL with no operand defaults to accumulator") {
        auto result = asm_.assemble(" ASL");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0x0A);
    }

    SECTION("ASL A explicit accumulator fails (operand evaluated as expression)") {
        // The assembler evaluates operands as expressions before checking
        // addressing modes, so "A" is treated as an undefined symbol.
        // Use the no-operand form (ASL) for accumulator mode instead.
        auto result = asm_.assemble(" ASL A");
        REQUIRE_FALSE(result.success);
        REQUIRE(result.errors.size() > 0);
    }
}

TEST_CASE("Assembler indirect (JMP) addressing mode", "[asm][addrmode]") {
    Assembler asm_;
    auto result = asm_.assemble(" JMP ($1234)");
    REQUIRE(result.success);
    CHECK(result.output[0] == 0x6C);  // JMP (abs)
    CHECK(result.output[1] == 0x34);
    CHECK(result.output[2] == 0x12);
}

TEST_CASE("Assembler JSR instruction", "[asm][instruction]") {
    Assembler asm_;
    auto result = asm_.assemble(" JSR $FFD2");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0x20);   // JSR
    CHECK(result.output[1] == 0xD2);
    CHECK(result.output[2] == 0xFF);
}

// ---------------------------------------------------------------------------
// Expression evaluation
// ---------------------------------------------------------------------------

TEST_CASE("Assembler handles decimal numbers", "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA #65");
    REQUIRE(result.success);
    CHECK(result.output[1] == 65);
}

TEST_CASE("Assembler handles binary numbers", "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA #%11001100");
    REQUIRE(result.success);
    CHECK(result.output[1] == 0xCC);
}

TEST_CASE("Assembler handles arithmetic in expressions", "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA #$10+$20");
    REQUIRE(result.success);
    CHECK(result.output[1] == 0x30);
}

// ---------------------------------------------------------------------------
// DSK directive - names the object file the assembled code is written to
// ---------------------------------------------------------------------------

TEST_CASE("Assembler DSK records the object filename", "[asm][directive][dsk]") {
    Assembler asm_;
    auto result = asm_.assemble(" DSK OBJ.FILE\n ORG $300\n NOP\n");

    REQUIRE(result.success);
    CHECK(result.hasObjectFile);
    CHECK(std::string(result.objectFilename) == "OBJ.FILE");
    CHECK(result.objectDrive == 1);
    // DSK emits nothing and does not move the program counter
    REQUIRE(result.output.size() == 1);
    CHECK(result.output[0] == 0xEA);
    CHECK(result.origin == 0x0300);
}

TEST_CASE("Assembler DSK accepts a drive qualifier", "[asm][directive][dsk]") {
    Assembler asm_;

    auto d2 = asm_.assemble(" ORG $300\n DSK OBJ,D2\n NOP\n");
    REQUIRE(d2.success);
    CHECK(d2.hasObjectFile);
    CHECK(std::string(d2.objectFilename) == "OBJ");
    CHECK(d2.objectDrive == 2);

    auto s6d1 = asm_.assemble(" ORG $300\n DSK OBJ,S6,D1\n NOP\n");
    REQUIRE(s6d1.success);
    CHECK(s6d1.objectDrive == 1);
}

TEST_CASE("Assembler DSK reports bad operands", "[asm][directive][dsk]") {
    Assembler asm_;

    SECTION("Missing filename") {
        auto result = asm_.assemble(" ORG $300\n DSK\n NOP\n");
        CHECK_FALSE(result.success);
        REQUIRE(result.errors.size() == 1);
        CHECK(std::string(result.errors[0].message).find("filename required")
              != std::string::npos);
        CHECK_FALSE(result.hasObjectFile);
    }

    SECTION("Filename too long") {
        auto result = asm_.assemble(
            " ORG $300\n DSK THIS.NAME.IS.MUCH.LONGER.THAN.THIRTY.CHARACTERS\n NOP\n");
        CHECK_FALSE(result.success);
        REQUIRE(result.errors.size() == 1);
        CHECK(std::string(result.errors[0].message).find("too long")
              != std::string::npos);
    }

    SECTION("Unsupported qualifier") {
        auto result = asm_.assemble(" ORG $300\n DSK OBJ,S5\n NOP\n");
        CHECK_FALSE(result.success);
        REQUIRE(result.errors.size() == 1);
        CHECK(std::string(result.errors[0].message).find("qualifier")
              != std::string::npos);
    }

    SECTION("Second DSK") {
        auto result = asm_.assemble(" ORG $300\n DSK ONE\n DSK TWO\n NOP\n");
        CHECK_FALSE(result.success);
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].lineNumber == 3);
        // The first name still stands
        CHECK(std::string(result.objectFilename) == "ONE");
    }
}

TEST_CASE("Assembler leaves hasObjectFile clear without DSK", "[asm][directive][dsk]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n NOP\n");
    REQUIRE(result.success);
    CHECK_FALSE(result.hasObjectFile);
    CHECK(std::string(result.objectFilename).empty());
}
