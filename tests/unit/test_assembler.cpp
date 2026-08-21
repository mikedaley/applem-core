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

    SECTION("ASL A is accepted as the accumulator") {
        auto result = asm_.assemble(" ASL A");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0x0A);
    }

    SECTION("A means the symbol when the source defines one") {
        // Merlin's accumulator form is a bare mnemonic, so a source that
        // defines a symbol called A means that symbol — reading it as the
        // accumulator would silently assemble a different instruction.
        auto result = asm_.assemble("A       EQU $10\n"
                                    "        LDA A\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[0] == 0xA5);
        CHECK(result.output[1] == 0x10);
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

// ===========================================================================
// Merlin expressions
//
// Merlin has no operator precedence: terms combine left to right in the order
// they are written. Sources written for Merlin depend on it.
// ===========================================================================

TEST_CASE("Assembler evaluates expressions left to right", "[asm][expression]") {
    Assembler asm_;

    SECTION("Addition before multiplication, because it comes first") {
        auto result = asm_.assemble(" LDA #1+2*3");
        REQUIRE(result.success);
        CHECK(result.output[1] == 9);
    }

    SECTION("Multiplication before addition, because it comes first") {
        auto result = asm_.assemble(" LDA #2*3+1");
        REQUIRE(result.success);
        CHECK(result.output[1] == 7);
    }

    SECTION("Division truncates") {
        auto result = asm_.assemble(" LDA #7/2");
        REQUIRE(result.success);
        CHECK(result.output[1] == 3);
    }
}

TEST_CASE("Assembler supports Merlin's logical operators", "[asm][expression]") {
    Assembler asm_;

    SECTION("'.' is OR") {
        auto result = asm_.assemble(" LDA #$F0.$0F");
        REQUIRE(result.success);
        CHECK(result.output[1] == 0xFF);
    }

    SECTION("'!' is exclusive OR") {
        auto result = asm_.assemble(" LDA #$FF!$0F");
        REQUIRE(result.success);
        CHECK(result.output[1] == 0xF0);
    }

    SECTION("'&' is AND") {
        auto result = asm_.assemble(" LDA #$FF&$0F");
        REQUIRE(result.success);
        CHECK(result.output[1] == 0x0F);
    }
}

TEST_CASE("Assembler byte selectors apply to the whole expression",
          "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble("TARGET  EQU $12FF\n"
                                "        LDA #>TARGET+1\n"
                                "        LDA #<TARGET+1\n");
    REQUIRE(result.success);
    // >($12FF+1) is $13, not >$12FF then +1
    CHECK(result.output[1] == 0x13);
    CHECK(result.output[3] == 0x00);
}

TEST_CASE("Assembler character constants take the high bit from the delimiter",
          "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble(" LDA #'A'\n LDA #\"A\"\n");
    REQUIRE(result.success);
    CHECK(result.output[1] == 0x41);
    CHECK(result.output[3] == 0xC1);
}

TEST_CASE("Assembler '*' is the address of the current line", "[asm][expression]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n LDA #0\n DA *\n");
    REQUIRE(result.success);
    CHECK(result.output[2] == 0x02);
    CHECK(result.output[3] == 0x03);
}

// ===========================================================================
// Comment field
// ===========================================================================

TEST_CASE("Assembler treats the field after the operand as a comment",
          "[asm][parse]") {
    Assembler asm_;
    // Merlin needs no semicolon: the fourth field is the comment.
    auto result = asm_.assemble(" LDA #$42 load the answer\n");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0xA9);
    CHECK(result.output[1] == 0x42);
}

// ===========================================================================
// Data directives
// ===========================================================================

TEST_CASE("Assembler ADR emits three bytes little-endian", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ADR $123456");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0x56);
    CHECK(result.output[1] == 0x34);
    CHECK(result.output[2] == 0x12);
}

TEST_CASE("Assembler ADRL emits four bytes little-endian", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ADRL $123456");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 4);
    CHECK(result.output[3] == 0x00);
}

TEST_CASE("Assembler DDB emits big-endian words", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DDB $1234");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 2);
    CHECK(result.output[0] == 0x12);
    CHECK(result.output[1] == 0x34);
}

TEST_CASE("Assembler DS accepts a fill value", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DS 3,$FF");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[0] == 0xFF);
    CHECK(result.output[2] == 0xFF);
}

TEST_CASE("Assembler DS backslash pads to the next page", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $08FE\n DS \\\n NOP\n");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[2] == 0xEA);
    CHECK(result.endAddress == 0x0901);
}

TEST_CASE("Assembler CHK emits the checksum of everything before it",
          "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" DFB $01,$02\n CHK\n");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 3);
    CHECK(result.output[2] == 0x03);
}

// ===========================================================================
// String directives
// ===========================================================================

TEST_CASE("Assembler string directives", "[asm][directive][string]") {
    Assembler asm_;

    SECTION("ASC with an apostrophe leaves the high bit clear") {
        auto result = asm_.assemble(" ASC 'AB'");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[0] == 0x41);
        CHECK(result.output[1] == 0x42);
    }

    SECTION("ASC with a quote sets the high bit") {
        auto result = asm_.assemble(" ASC \"AB\"");
        REQUIRE(result.success);
        CHECK(result.output[0] == 0xC1);
        CHECK(result.output[1] == 0xC2);
    }

    SECTION("ASC accepts trailing hex bytes") {
        auto result = asm_.assemble(" ASC \"AB\"8D00");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 4);
        CHECK(result.output[2] == 0x8D);
        CHECK(result.output[3] == 0x00);
    }

    SECTION("DCI flips the high bit of the last character") {
        auto result = asm_.assemble(" DCI \"ABC\"");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0xC1);
        CHECK(result.output[2] == 0x43);
    }

    SECTION("INV converts to the inverse character set") {
        auto result = asm_.assemble(" INV \"ABC\"");
        REQUIRE(result.success);
        CHECK(result.output[0] == 0x01);
        CHECK(result.output[2] == 0x03);
    }

    SECTION("FLS converts to the flashing character set") {
        auto result = asm_.assemble(" FLS \"ABC\"");
        REQUIRE(result.success);
        CHECK(result.output[0] == 0x41);
        CHECK(result.output[2] == 0x43);
    }

    SECTION("REV reverses the string") {
        auto result = asm_.assemble(" REV \"ABC\"");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0xC3);
        CHECK(result.output[2] == 0xC1);
    }

    SECTION("STR prefixes a length byte") {
        auto result = asm_.assemble(" STR \"ABC\"");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 4);
        CHECK(result.output[0] == 3);
        CHECK(result.output[1] == 0xC1);
    }

    SECTION("STRL prefixes a two-byte length") {
        auto result = asm_.assemble(" STRL \"ABC\"");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 5);
        CHECK(result.output[0] == 3);
        CHECK(result.output[1] == 0);
    }

    SECTION("A string may hold spaces") {
        auto result = asm_.assemble(" ASC \"A B\"");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[1] == 0xA0);
    }

    SECTION("Any character may delimit") {
        auto result = asm_.assemble(" ASC /AB/");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[0] == 0x41);
    }
}

// ===========================================================================
// Conditional assembly
// ===========================================================================

TEST_CASE("Assembler DO/ELSE/FIN selects a branch", "[asm][conditional]") {
    Assembler asm_;

    SECTION("True branch") {
        auto result = asm_.assemble(" DO 1\n LDA #1\n ELSE\n LDA #2\n FIN\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[1] == 1);
    }

    SECTION("False branch") {
        auto result = asm_.assemble(" DO 0\n LDA #1\n ELSE\n LDA #2\n FIN\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[1] == 2);
    }

    SECTION("A skipped branch defines no labels") {
        auto result = asm_.assemble(" DO 0\nGHOST LDA #1\n FIN\n LDA #2\n");
        REQUIRE(result.success);
        CHECK(findSymbol(result, "GHOST") == nullptr);
    }

    SECTION("Nested conditionals stay balanced") {
        auto result = asm_.assemble(" DO 0\n DO 1\n LDA #1\n FIN\n FIN\n LDA #9\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[1] == 9);
    }

    SECTION("An unterminated conditional is reported") {
        auto result = asm_.assemble(" DO 1\n LDA #1\n");
        CHECK_FALSE(result.success);
    }

    SECTION("FIN without DO is reported") {
        auto result = asm_.assemble(" FIN\n");
        CHECK_FALSE(result.success);
    }
}

TEST_CASE("Assembler IF compares the first character", "[asm][conditional]") {
    Assembler asm_;

    SECTION("Matching character assembles the block") {
        auto result = asm_.assemble(" IF \"Y\",YES\n LDA #1\n FIN\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
    }

    SECTION("Different character skips it") {
        auto result = asm_.assemble(" IF \"Y\",NO\n LDA #1\n FIN\n");
        REQUIRE(result.success);
        CHECK(result.output.empty());
    }
}

// ===========================================================================
// LUP
// ===========================================================================

TEST_CASE("Assembler LUP repeats a block", "[asm][lup]") {
    Assembler asm_;

    SECTION("Simple repeat") {
        auto result = asm_.assemble(" LUP 3\n NOP\n --^\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0xEA);
    }

    SECTION("ELUP also closes a loop") {
        auto result = asm_.assemble(" LUP 2\n NOP\n ELUP\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
    }

    SECTION("A zero count emits nothing but keeps going") {
        auto result = asm_.assemble(" LUP 0\n NOP\n --^\n INX\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0xE8);
    }

    SECTION("Nested loops multiply") {
        auto result = asm_.assemble(" LUP 2\n LUP 3\n NOP\n --^\n --^\n");
        REQUIRE(result.success);
        CHECK(result.output.size() == 6);
    }

    SECTION("An unterminated loop is reported") {
        auto result = asm_.assemble(" LUP 2\n NOP\n");
        CHECK_FALSE(result.success);
    }
}

// ===========================================================================
// Macros
// ===========================================================================

TEST_CASE("Assembler expands a macro", "[asm][macro]") {
    Assembler asm_;

    SECTION("Called by name") {
        auto result = asm_.assemble("PUSHA   MAC\n"
                                    "        PHA\n"
                                    "        EOM\n"
                                    "        PUSHA\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0x48);
    }

    SECTION("Called with >>>") {
        auto result = asm_.assemble("PUSHA   MAC\n"
                                    "        PHA\n"
                                    "        <<<\n"
                                    "        >>> PUSHA\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0x48);
    }

    SECTION("Called with PMC") {
        auto result = asm_.assemble("PUSHA   MAC\n"
                                    "        PHA\n"
                                    "        EOM\n"
                                    "        PMC PUSHA\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
    }

    SECTION("Parameters substitute into the body") {
        auto result = asm_.assemble("POKE    MAC\n"
                                    "        LDA #]2\n"
                                    "        STA ]1\n"
                                    "        EOM\n"
                                    "        >>> POKE;$20;$07\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 4);
        CHECK(result.output[0] == 0xA9);
        CHECK(result.output[1] == 0x07);
        CHECK(result.output[2] == 0x85);
        CHECK(result.output[3] == 0x20);
    }

    SECTION("A macro body is not assembled where it is defined") {
        auto result = asm_.assemble("NOTHING MAC\n"
                                    "        NOP\n"
                                    "        EOM\n"
                                    "        INX\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0xE8);
    }

    SECTION("Local labels are scoped to one expansion") {
        // Two calls to the same macro both define :SKIP, which would collide
        // if the expansions shared a scope.
        auto result = asm_.assemble("SKIPIT  MAC\n"
                                    "        BNE :SKIP\n"
                                    "        NOP\n"
                                    ":SKIP   RTS\n"
                                    "        EOM\n"
                                    "        >>> SKIPIT\n"
                                    "        >>> SKIPIT\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 8);
        CHECK(result.output[0] == 0xD0);
        CHECK(result.output[1] == 0x01);
    }

    SECTION("A macro may call another macro") {
        auto result = asm_.assemble("INNER   MAC\n"
                                    "        NOP\n"
                                    "        EOM\n"
                                    "OUTER   MAC\n"
                                    "        >>> INNER\n"
                                    "        INX\n"
                                    "        EOM\n"
                                    "        >>> OUTER\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[0] == 0xEA);
        CHECK(result.output[1] == 0xE8);
    }

    SECTION("An unknown macro is reported") {
        auto result = asm_.assemble(" >>> NOSUCH\n");
        CHECK_FALSE(result.success);
    }

    SECTION("A runaway recursive macro stops") {
        auto result = asm_.assemble("LOOPY   MAC\n"
                                    "        >>> LOOPY\n"
                                    "        EOM\n"
                                    "        >>> LOOPY\n");
        CHECK_FALSE(result.success);
    }

    SECTION("IF tests whether a parameter was given") {
        auto result = asm_.assemble("MAYBE   MAC\n"
                                    "        IF \"\",]1\n"
                                    "        NOP\n"
                                    "        ELSE\n"
                                    "        LDA #]1\n"
                                    "        FIN\n"
                                    "        EOM\n"
                                    "        >>> MAYBE\n"
                                    "        >>> MAYBE;7\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0xEA);
        CHECK(result.output[1] == 0xA9);
        CHECK(result.output[2] == 7);
    }
}

// ===========================================================================
// Local labels and variables
// ===========================================================================

TEST_CASE("Assembler local labels belong to the preceding global label",
          "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n"
                                "FIRST   NOP\n"
                                ":LOOP   NOP\n"
                                "        BNE :LOOP\n"
                                "SECOND  NOP\n"
                                ":LOOP   NOP\n"
                                "        BNE :LOOP\n");
    REQUIRE(result.success);
    // Each BNE reaches back to its own :LOOP, two bytes behind it
    CHECK(result.output[3] == 0xFD);
    CHECK(result.output[7] == 0xFD);
}

TEST_CASE("Assembler variables may be reassigned", "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble("]V      EQU 1\n"
                                "        LDA #]V\n"
                                "]V      EQU 2\n"
                                "        LDA #]V\n");
    REQUIRE(result.success);
    CHECK(result.output[1] == 1);
    CHECK(result.output[3] == 2);
}

TEST_CASE("Assembler reports a duplicate label", "[asm][labels]") {
    Assembler asm_;
    auto result = asm_.assemble("HERE    NOP\n"
                                "HERE    NOP\n");
    CHECK_FALSE(result.success);
}

TEST_CASE("Assembler VAR loads the numbered variables", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" VAR 3;4\n LDA #]1\n LDA #]2\n");
    REQUIRE(result.success);
    CHECK(result.output[1] == 3);
    CHECK(result.output[3] == 4);
}

// ===========================================================================
// Dummy sections
// ===========================================================================

TEST_CASE("Assembler DUM reserves addresses without emitting", "[asm][dummy]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n"
                                " DUM $10\n"
                                "COUNT   DS 1\n"
                                "TOTAL   DS 2\n"
                                " DEND\n"
                                " LDA COUNT\n"
                                " LDA TOTAL\n");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 4);
    CHECK(result.output[0] == 0xA5);
    CHECK(result.output[1] == 0x10);
    CHECK(result.output[3] == 0x11);
    const AsmSymbol* total = findSymbol(result, "TOTAL");
    REQUIRE(total != nullptr);
    CHECK(total->value == 0x11);
}

TEST_CASE("Assembler DEND without DUM is reported", "[asm][dummy]") {
    Assembler asm_;
    auto result = asm_.assemble(" DEND\n");
    CHECK_FALSE(result.success);
}

// ===========================================================================
// Segments and END
// ===========================================================================

TEST_CASE("Assembler records a segment per ORG", "[asm][segments]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n NOP\n ORG $400\n INX\n");
    REQUIRE(result.success);
    REQUIRE(result.segments.size() == 2);
    CHECK(result.segments[0].address == 0x300);
    CHECK(result.segments[0].length == 1);
    CHECK(result.segments[1].address == 0x400);
    CHECK(result.segments[1].offset == 1);
    CHECK(result.origin == 0x300);
}

TEST_CASE("Assembler END stops assembly", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" NOP\n END\n INX\n");
    REQUIRE(result.success);
    REQUIRE(result.output.size() == 1);
}

TEST_CASE("Assembler ERR fails when its expression is non-zero",
          "[asm][directive]") {
    Assembler asm_;

    SECTION("Zero passes") {
        auto result = asm_.assemble(" ERR 0\n NOP\n");
        CHECK(result.success);
    }

    SECTION("Non-zero fails") {
        auto result = asm_.assemble(" ORG $300\n NOP\n ERR *-$300-99\n");
        CHECK_FALSE(result.success);
    }
}

// ===========================================================================
// Forced addressing widths
// ===========================================================================

TEST_CASE("Assembler honours forced addressing widths", "[asm][addrmode]") {
    Assembler asm_;

    SECTION("'|' forces absolute") {
        auto result = asm_.assemble(" LDA |$10");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0xAD);
    }

    SECTION("Zero page is chosen by default") {
        auto result = asm_.assemble(" LDA $10");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[0] == 0xA5);
    }
}

// ===========================================================================
// Object file directives
// ===========================================================================

TEST_CASE("Assembler SAV names the object file", "[asm][directive][dsk]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n TYP $FF\n SAV MYPROG\n NOP\n");
    REQUIRE(result.success);
    CHECK(result.hasObjectFile);
    CHECK(std::string(result.objectFilename) == "MYPROG");
    CHECK(result.objectType == 0xFF);
}

TEST_CASE("Assembler defaults the object type to BIN", "[asm][directive][dsk]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n DSK MYPROG\n NOP\n");
    REQUIRE(result.success);
    CHECK(result.objectType == 0x06);
}

// ===========================================================================
// Unsupported directives
// ===========================================================================

TEST_CASE("Assembler reports relocatable directives as unsupported",
          "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" REL\n");
    CHECK_FALSE(result.success);
    REQUIRE(result.errors.size() == 1);
    CHECK_FALSE(result.errors[0].warning);
}

TEST_CASE("Assembler warns about KBD but still assembles", "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble("]N      KBD 5\n        LDA #]N\n");
    CHECK(result.success);
    REQUIRE(result.errors.size() == 1);
    CHECK(result.errors[0].warning);
    CHECK(result.output[1] == 5);
}

TEST_CASE("Assembler accepts listing directives without complaint",
          "[asm][directive]") {
    Assembler asm_;
    auto result = asm_.assemble(" LST OFF\n PAG\n TTL Title\n SKP 2\n"
                                " EXP ON\n DAT\n TR ON\n NOP\n");
    CHECK(result.success);
    REQUIRE(result.output.size() == 1);
}

// ===========================================================================
// PUT / USE
// ===========================================================================

TEST_CASE("Assembler PUT includes source from the host", "[asm][include]") {
    Assembler asm_;
    asm_.setIncludeProvider([](const std::string& name, std::string& out) {
        if (name == "EXTRA") { out = " INX\n INY\n"; return true; }
        return false;
    });

    SECTION("Included lines assemble in place") {
        auto result = asm_.assemble(" NOP\n PUT EXTRA\n DEX\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 4);
        CHECK(result.output[0] == 0xEA);
        CHECK(result.output[1] == 0xE8);
        CHECK(result.output[2] == 0xC8);
        CHECK(result.output[3] == 0xCA);
    }

    SECTION("A missing file is reported against the PUT line") {
        auto result = asm_.assemble(" NOP\n PUT GONE\n");
        CHECK_FALSE(result.success);
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].lineNumber == 2);
    }
}

TEST_CASE("Assembler PUT without a provider is reported", "[asm][include]") {
    Assembler asm_;
    auto result = asm_.assemble(" PUT ANYTHING\n");
    CHECK_FALSE(result.success);
}

TEST_CASE("Assembler stops a self-including file", "[asm][include]") {
    Assembler asm_;
    asm_.setIncludeProvider([](const std::string& name, std::string& out) {
        if (name == "SELF") { out = " PUT SELF\n"; return true; }
        return false;
    });
    auto result = asm_.assemble(" PUT SELF\n");
    CHECK_FALSE(result.success);
}

// ===========================================================================
// Listing and per-line records
// ===========================================================================

TEST_CASE("Assembler records what each line produced", "[asm][listing]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n LDA #$42\n RTS\n");
    REQUIRE(result.success);

    const AsmLineInfo* lda = nullptr;
    for (const auto& info : result.lines) {
        if (info.lineNumber == 2) lda = &info;
    }
    REQUIRE(lda != nullptr);
    CHECK(lda->address == 0x300);
    CHECK(lda->byteCount == 2);
    CHECK(lda->bytes[0] == 0xA9);
    CHECK(lda->bytes[1] == 0x42);
    CHECK(lda->cycles == 2);
}

TEST_CASE("Assembler produces a listing", "[asm][listing]") {
    Assembler asm_;
    auto result = asm_.assemble(" ORG $300\n LDA #$42\n");
    REQUIRE(result.success);
    CHECK(result.listing.find("0300: A9 42") != std::string::npos);
}

TEST_CASE("Assembler cycle counts match the opcode", "[asm][listing]") {
    CHECK(Assembler::cyclesForOpcode(0xEA) == 2);  // NOP
    CHECK(Assembler::cyclesForOpcode(0x20) == 6);  // JSR
    CHECK(Assembler::cyclesForOpcode(0x60) == 6);  // RTS
    CHECK(Assembler::cyclesForOpcode(0xAD) == 4);  // LDA abs
    CHECK(Assembler::cyclesForOpcode(0xA5) == 3);  // LDA zp
}

// ===========================================================================
// Sweet-16
// ===========================================================================

TEST_CASE("Assembler assembles Sweet-16 after SW", "[asm][sweet16]") {
    Assembler asm_;

    SECTION("SET loads a register") {
        auto result = asm_.assemble(" SW\n SET R1,$1234\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0x11);
        CHECK(result.output[1] == 0x34);
        CHECK(result.output[2] == 0x12);
    }

    SECTION("Register operations are one byte") {
        auto result = asm_.assemble(" SW\n LD R2\n ST R3\n ADD R4\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 3);
        CHECK(result.output[0] == 0x22);
        CHECK(result.output[1] == 0x33);
        CHECK(result.output[2] == 0xA4);
    }

    SECTION("The indirect form of LD and ST is a nibble higher") {
        auto result = asm_.assemble(" SW\n LD @R2\n ST @R3\n");
        REQUIRE(result.success);
        CHECK(result.output[0] == 0x42);
        CHECK(result.output[1] == 0x53);
    }

    SECTION("Branches are relative") {
        auto branch = asm_.assemble(" ORG $300\n SW\nTOP  BR TOP\n");
        REQUIRE(branch.success);
        REQUIRE(branch.output.size() == 2);
        CHECK(branch.output[0] == 0x01);
        CHECK(branch.output[1] == 0xFE);
    }

    SECTION("RTN is one byte") {
        auto result = asm_.assemble(" SW\n RTN\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 1);
        CHECK(result.output[0] == 0x00);
    }

    SECTION("Sweet-16 mnemonics are inert until SW") {
        auto result = asm_.assemble(" SET R1,$1234\n");
        CHECK_FALSE(result.success);
    }

    SECTION("SW OFF hands the mnemonics back") {
        auto result = asm_.assemble(" SW\n RTN\n SW OFF\n NOP\n");
        REQUIRE(result.success);
        REQUIRE(result.output.size() == 2);
        CHECK(result.output[1] == 0xEA);
    }
}

TEST_CASE("Assembler credits a macro's bytes to the call site", "[asm][macro]") {
    Assembler asm_;
    auto result = asm_.assemble("PUSH2   MAC\n"
                                "        PHA\n"
                                "        PHX\n"
                                "        EOM\n"
                                "        >>> PUSH2\n");
    REQUIRE(result.success);

    const AsmLineInfo* call = nullptr;
    for (const auto& info : result.lines) {
        if (info.lineNumber == 5) call = &info;
    }
    REQUIRE(call != nullptr);
    CHECK(call->byteCount == 2);
    CHECK(call->bytes[0] == 0x48);
    CHECK(call->bytes[1] == 0xDA);
}
