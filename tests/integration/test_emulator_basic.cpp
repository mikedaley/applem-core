/*
 * test_emulator_basic.cpp - Integration tests for Emulator BASIC debugging
 *
 * Tests BASIC program tracking, BASIC breakpoints, BASIC error state,
 * and related BASIC debugging API through the Emulator facade.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <cstdint>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// BASIC program running state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator isBasicProgramRunning is false after init", "[emulator][basic]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.isBasicProgramRunning());
}

// ---------------------------------------------------------------------------
// BASIC breakpoints
// ---------------------------------------------------------------------------

TEST_CASE("Emulator addBasicBreakpoint makes hasBasicBreakpoints true", "[emulator][basic][breakpoint]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.hasBasicBreakpoints());

    emu.addBasicBreakpoint(10, -1); // Line 10, whole line
    REQUIRE(emu.hasBasicBreakpoints());
}

TEST_CASE("Emulator clearBasicBreakpoints removes all BASIC breakpoints", "[emulator][basic][breakpoint]") {
    Emulator emu;
    emu.init();

    emu.addBasicBreakpoint(10, -1);
    emu.addBasicBreakpoint(20, 0);
    REQUIRE(emu.hasBasicBreakpoints());

    emu.clearBasicBreakpoints();
    REQUIRE_FALSE(emu.hasBasicBreakpoints());
}

TEST_CASE("Emulator removeBasicBreakpoint removes specific breakpoint", "[emulator][basic][breakpoint]") {
    Emulator emu;
    emu.init();

    emu.addBasicBreakpoint(10, -1);
    emu.removeBasicBreakpoint(10, -1);

    REQUIRE_FALSE(emu.hasBasicBreakpoints());
}

// ---------------------------------------------------------------------------
// BASIC breakpoint hit state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator isBasicBreakpointHit is initially false", "[emulator][basic][breakpoint]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.isBasicBreakpointHit());
}

TEST_CASE("Emulator clearBasicBreakpointHit clears hit state", "[emulator][basic][breakpoint]") {
    Emulator emu;
    emu.init();

    // Hit state should already be false; clearing it should not crash
    emu.clearBasicBreakpointHit();
    REQUIRE_FALSE(emu.isBasicBreakpointHit());
}

// ---------------------------------------------------------------------------
// BASIC error state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator isBasicErrorHit is initially false", "[emulator][basic][error]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.isBasicErrorHit());
}

// ---------------------------------------------------------------------------
// Statement geometry
//
// These back the BASIC debugger's statement highlighting. The same colon scan
// decides which statement a breakpoint fires on, so the two cannot disagree.
// ---------------------------------------------------------------------------

namespace {

/**
 * Write one tokenized program line into memory and point TXTTAB at it.
 * Layout per line: [next-ptr:2][line-num:2][tokens...][00], then [00][00].
 * Returns the address of the line's tokenized text.
 */
uint16_t layOutSingleLine(Emulator& emu, uint16_t lineNumber,
                          const std::vector<uint8_t>& tokens) {
    constexpr uint16_t TXTTAB = 0x0801;

    const uint16_t lineSize = static_cast<uint16_t>(2 + 2 + tokens.size() + 1);
    const uint16_t nextAddr = static_cast<uint16_t>(TXTTAB + lineSize);

    emu.writeMemory(TXTTAB, nextAddr & 0xFF);
    emu.writeMemory(TXTTAB + 1, (nextAddr >> 8) & 0xFF);
    emu.writeMemory(TXTTAB + 2, lineNumber & 0xFF);
    emu.writeMemory(TXTTAB + 3, (lineNumber >> 8) & 0xFF);

    for (size_t i = 0; i < tokens.size(); i++) {
        emu.writeMemory(static_cast<uint16_t>(TXTTAB + 4 + i), tokens[i]);
    }
    emu.writeMemory(static_cast<uint16_t>(TXTTAB + 4 + tokens.size()), 0x00);

    // End-of-program marker
    emu.writeMemory(nextAddr, 0x00);
    emu.writeMemory(nextAddr + 1, 0x00);

    // TXTTAB pointer
    emu.writeMemory(0x67, TXTTAB & 0xFF);
    emu.writeMemory(0x68, (TXTTAB >> 8) & 0xFF);

    return TXTTAB + 4;
}

constexpr uint8_t T_PRINT = 0xBA;
constexpr uint8_t T_REM   = 0xB2;
constexpr uint8_t T_DATA  = 0x83;
constexpr uint8_t COLON   = 0x3A;
constexpr uint8_t QUOTE   = 0x22;

} // namespace

TEST_CASE("getBasicStatementCountForLine counts colon-separated statements",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    // 10 PRINT : PRINT : PRINT  -> three statements
    layOutSingleLine(emu, 10, {T_PRINT, COLON, T_PRINT, COLON, T_PRINT});

    REQUIRE(emu.getBasicStatementCountForLine(10) == 3);
}

TEST_CASE("getBasicStatementCountForLine returns 1 for a single statement",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    layOutSingleLine(emu, 10, {T_PRINT});

    REQUIRE(emu.getBasicStatementCountForLine(10) == 1);
}

TEST_CASE("getBasicStatementCountForLine ignores colons inside strings",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    // 10 PRINT "A:B" -> one statement; the colon is string content
    layOutSingleLine(emu, 10, {T_PRINT, QUOTE, 'A', COLON, 'B', QUOTE});

    REQUIRE(emu.getBasicStatementCountForLine(10) == 1);
}

TEST_CASE("getBasicStatementCountForLine ignores colons after REM",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    // 10 REM A:B -> one statement; everything after REM is comment text
    layOutSingleLine(emu, 10, {T_REM, 'A', COLON, 'B'});

    REQUIRE(emu.getBasicStatementCountForLine(10) == 1);
}

TEST_CASE("getBasicStatementCountForLine treats a colon after DATA as a separator",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    // 10 DATA 1 : PRINT -> two statements. A colon ends the DATA statement,
    // which is what the breakpoint scan has always assumed.
    layOutSingleLine(emu, 10, {T_DATA, '1', COLON, T_PRINT});

    REQUIRE(emu.getBasicStatementCountForLine(10) == 2);
}

TEST_CASE("getBasicStatementCountForLine returns 1 for an unknown line",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    layOutSingleLine(emu, 10, {T_PRINT});

    REQUIRE(emu.getBasicStatementCountForLine(999) == 1);
}

TEST_CASE("getBasicStatementIndexForLine advances past each colon",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    // 10 PRINT : PRINT : PRINT
    const uint16_t text = layOutSingleLine(emu, 10, {T_PRINT, COLON, T_PRINT, COLON, T_PRINT});

    CHECK(emu.getBasicStatementIndexForLine(10, text) == 0);           // at PRINT
    CHECK(emu.getBasicStatementIndexForLine(10, text + 2) == 1);       // past first colon
    CHECK(emu.getBasicStatementIndexForLine(10, text + 4) == 2);       // past second colon
}

TEST_CASE("getBasicStatementIndexForLine clamps before the line text",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    const uint16_t text = layOutSingleLine(emu, 10, {T_PRINT, COLON, T_PRINT});

    // TXTPTR still on the line header counts as the first statement.
    REQUIRE(emu.getBasicStatementIndexForLine(10, text - 2) == 0);
}

TEST_CASE("getBasicStatementIndexForLine returns 0 for an unknown line",
          "[emulator][basic][statement]") {
    Emulator emu;
    emu.init();

    layOutSingleLine(emu, 10, {T_PRINT, COLON, T_PRINT});

    REQUIRE(emu.getBasicStatementIndexForLine(999, 0x0805) == 0);
}
