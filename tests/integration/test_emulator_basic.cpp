/*
 * test_emulator_basic.cpp - Integration tests for Emulator BASIC debugging
 *
 * Tests BASIC program tracking, BASIC breakpoints, BASIC error state,
 * and related BASIC debugging API through the Emulator facade.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

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
