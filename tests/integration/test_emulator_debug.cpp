/*
 * test_emulator_debug.cpp - Integration tests for Emulator debug features
 *
 * Tests breakpoints, watchpoints, beam breakpoints, trace logging,
 * cycle profiling, step over, step out, and related debug state.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <memory>

using namespace a2e;

// Helper: create an initialized emulator ready for testing
static std::unique_ptr<Emulator> makeEmulator() {
    auto emu = std::make_unique<Emulator>();
    emu->init();
    return emu;
}

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

TEST_CASE("Breakpoint triggers when PC reaches address", "[emulator][debug][breakpoint]") {
    Emulator emu;
    emu.init();

    // Write a JMP $0400 loop at $0400: 4C 00 04
    emu.writeMemory(0x0400, 0x4C); // JMP
    emu.writeMemory(0x0401, 0x00); // low byte
    emu.writeMemory(0x0402, 0x04); // high byte

    // Set PC to $0400 and add breakpoint there
    emu.setPC(0x0400);
    emu.addBreakpoint(0x0400);

    // Need to unpause since breakpoints pause on hit
    // The first execution should immediately hit the breakpoint
    emu.runCycles(100);

    REQUIRE(emu.isBreakpointHit());
    REQUIRE(emu.getBreakpointAddress() == 0x0400);
}

TEST_CASE("removeBreakpoint prevents breakpoint from triggering", "[emulator][debug][breakpoint]") {
    Emulator emu;
    emu.init();

    // Write NOP sled at $0400
    for (int i = 0; i < 16; i++) {
        emu.writeMemory(0x0400 + i, 0xEA); // NOP
    }
    // End with JMP $0400
    emu.writeMemory(0x0410, 0x4C);
    emu.writeMemory(0x0411, 0x00);
    emu.writeMemory(0x0412, 0x04);

    emu.setPC(0x0400);
    emu.addBreakpoint(0x0408);

    // Run - breakpoint should hit at $0408
    emu.runCycles(100);
    REQUIRE(emu.isBreakpointHit());
    REQUIRE(emu.getBreakpointAddress() == 0x0408);

    // Remove it and resume
    emu.removeBreakpoint(0x0408);
    emu.setPaused(false);
    emu.runCycles(1000);

    // Should NOT hit a breakpoint now (will just loop)
    REQUIRE_FALSE(emu.isBreakpointHit());
}

TEST_CASE("enableBreakpoint(false) prevents breakpoint from triggering", "[emulator][debug][breakpoint]") {
    Emulator emu;
    emu.init();

    // NOP sled + JMP loop
    for (int i = 0; i < 16; i++) {
        emu.writeMemory(0x0400 + i, 0xEA);
    }
    emu.writeMemory(0x0410, 0x4C);
    emu.writeMemory(0x0411, 0x00);
    emu.writeMemory(0x0412, 0x04);

    emu.setPC(0x0400);
    emu.addBreakpoint(0x0408);
    emu.enableBreakpoint(0x0408, false); // disable it

    emu.runCycles(1000);
    REQUIRE_FALSE(emu.isBreakpointHit());
}

TEST_CASE("getBreakpointAddress returns correct address on hit", "[emulator][debug][breakpoint]") {
    Emulator emu;
    emu.init();

    emu.writeMemory(0x0500, 0x4C);
    emu.writeMemory(0x0501, 0x00);
    emu.writeMemory(0x0502, 0x05);

    emu.setPC(0x0500);
    emu.addBreakpoint(0x0500);
    emu.runCycles(100);

    REQUIRE(emu.isBreakpointHit());
    REQUIRE(emu.getBreakpointAddress() == 0x0500);
}

// ---------------------------------------------------------------------------
// Watchpoints
// ---------------------------------------------------------------------------

TEST_CASE("Watchpoint triggers on write to watched address", "[emulator][debug][watchpoint]") {
    Emulator emu;
    emu.init();

    // Add a write watchpoint on $0400
    emu.addWatchpoint(0x0400, 0x0400, Emulator::WP_WRITE);

    // Write a small program at $0300 that writes to $0400 then loops:
    //   LDA #$42      ; A9 42
    //   STA $0400     ; 8D 00 04
    //   JMP $0305     ; 4C 05 03
    emu.writeMemory(0x0300, 0xA9);
    emu.writeMemory(0x0301, 0x42);
    emu.writeMemory(0x0302, 0x8D);
    emu.writeMemory(0x0303, 0x00);
    emu.writeMemory(0x0304, 0x04);
    emu.writeMemory(0x0305, 0x4C);
    emu.writeMemory(0x0306, 0x05);
    emu.writeMemory(0x0307, 0x03);

    emu.setPC(0x0300);
    emu.runCycles(100);

    REQUIRE(emu.isWatchpointHit());
    REQUIRE(emu.getWatchpointAddress() == 0x0400);
}

TEST_CASE("clearWatchpoints removes all watchpoints", "[emulator][debug][watchpoint]") {
    Emulator emu;
    emu.init();

    emu.addWatchpoint(0x0400, 0x0400, Emulator::WP_WRITE);
    emu.clearWatchpoints();

    // Write to $0400 via a small program
    emu.writeMemory(0x0300, 0xA9); // LDA #$42
    emu.writeMemory(0x0301, 0x42);
    emu.writeMemory(0x0302, 0x8D); // STA $0400
    emu.writeMemory(0x0303, 0x00);
    emu.writeMemory(0x0304, 0x04);
    emu.writeMemory(0x0305, 0x4C); // JMP $0305
    emu.writeMemory(0x0306, 0x05);
    emu.writeMemory(0x0307, 0x03);

    emu.setPC(0x0300);
    emu.runCycles(100);

    REQUIRE_FALSE(emu.isWatchpointHit());
}

// ---------------------------------------------------------------------------
// Beam breakpoints
// ---------------------------------------------------------------------------

TEST_CASE("Beam breakpoint triggers at specified scanline", "[emulator][debug][beam]") {
    Emulator emu;
    emu.init();

    // Add beam breakpoint at scanline 100, any hPos (-1 = wildcard)
    int32_t id = emu.addBeamBreakpoint(100, -1);
    REQUIRE(id >= 0);

    // Run enough cycles for the beam to reach scanline 100
    // 100 scanlines * 65 cycles/scanline = 6500 cycles minimum
    emu.runCycles(17030); // One full frame

    REQUIRE(emu.isBeamBreakpointHit());
}

TEST_CASE("removeBeamBreakpoint prevents beam break from triggering", "[emulator][debug][beam]") {
    Emulator emu;
    emu.init();

    int32_t id = emu.addBeamBreakpoint(100, -1);
    emu.removeBeamBreakpoint(id);

    emu.runCycles(17030);

    REQUIRE_FALSE(emu.isBeamBreakpointHit());
}

TEST_CASE("clearAllBeamBreakpoints clears all beam breaks", "[emulator][debug][beam]") {
    Emulator emu;
    emu.init();

    emu.addBeamBreakpoint(50, -1);
    emu.addBeamBreakpoint(100, -1);
    emu.clearAllBeamBreakpoints();

    emu.runCycles(17030);

    REQUIRE_FALSE(emu.isBeamBreakpointHit());
}

// ---------------------------------------------------------------------------
// Trace log
// ---------------------------------------------------------------------------

TEST_CASE("Trace records entries when enabled", "[emulator][debug][trace]") {
    Emulator emu;
    emu.init();

    emu.setTraceEnabled(true);
    emu.runCycles(200);

    REQUIRE(emu.getTraceCount() > 0);
    REQUIRE(emu.getTraceBuffer() != nullptr);
    REQUIRE(emu.getTraceCapacity() > 0);
}

TEST_CASE("clearTrace resets trace count to zero", "[emulator][debug][trace]") {
    Emulator emu;
    emu.init();

    emu.setTraceEnabled(true);
    emu.runCycles(200);
    REQUIRE(emu.getTraceCount() > 0);

    emu.clearTrace();
    REQUIRE(emu.getTraceCount() == 0);
}

// ---------------------------------------------------------------------------
// Cycle profiling
// ---------------------------------------------------------------------------

TEST_CASE("Profiling records cycles when enabled", "[emulator][debug][profile]") {
    Emulator emu;
    emu.init();

    emu.setProfileEnabled(true);
    emu.clearProfile();
    emu.runCycles(1000);

    // Check that at least one address has non-zero cycle counts
    const uint32_t* profile = emu.getProfileCycles();
    REQUIRE(profile != nullptr);

    bool hasNonZero = false;
    for (int i = 0; i < 65536; i++) {
        if (profile[i] > 0) {
            hasNonZero = true;
            break;
        }
    }
    REQUIRE(hasNonZero);
}

TEST_CASE("clearProfile resets all profile cycle counts", "[emulator][debug][profile]") {
    Emulator emu;
    emu.init();

    emu.setProfileEnabled(true);
    emu.runCycles(1000);
    emu.clearProfile();

    const uint32_t* profile = emu.getProfileCycles();
    bool allZero = true;
    for (int i = 0; i < 65536; i++) {
        if (profile[i] != 0) {
            allZero = false;
            break;
        }
    }
    REQUIRE(allZero);
}

// ---------------------------------------------------------------------------
// Step Over
// ---------------------------------------------------------------------------

TEST_CASE("stepOver on JSR sets temp breakpoint after JSR", "[emulator][debug][stepover]") {
    Emulator emu;
    emu.init();

    // Write JSR $0500 at $0400
    emu.writeMemory(0x0400, 0x20); // JSR
    emu.writeMemory(0x0401, 0x00); // low byte
    emu.writeMemory(0x0402, 0x05); // high byte
    // Write NOP at $0403
    emu.writeMemory(0x0403, 0xEA);

    // Write RTS at $0500 so the subroutine returns
    emu.writeMemory(0x0500, 0x60); // RTS

    emu.setPC(0x0400);
    emu.setPaused(true);

    uint16_t tempBp = emu.stepOver();

    // stepOver on JSR should set temp breakpoint at PC+3 = $0403
    REQUIRE(tempBp == 0x0403);
}

// ---------------------------------------------------------------------------
// Step Out
// ---------------------------------------------------------------------------

TEST_CASE("stepOut sets temp breakpoint at return address from stack", "[emulator][debug][stepout]") {
    Emulator emu;
    emu.init();

    // Simulate being inside a subroutine:
    // Push a fake return address (e.g., $04FF, since RTS adds 1 -> $0500)
    // onto the stack.
    uint8_t sp = emu.getSP();

    // Push high byte then low byte (as JSR does)
    emu.writeMemory(0x0100 + sp, 0x04);   // PCH
    sp--;
    emu.writeMemory(0x0100 + sp, 0xFF);   // PCL
    sp--;
    emu.setSP(sp);

    // Write a NOP loop at current PC
    uint16_t pc = emu.getPC();
    emu.writeMemory(pc, 0xEA);     // NOP
    emu.writeMemory(pc + 1, 0xEA); // NOP

    emu.setPaused(true);

    uint16_t tempBp = emu.stepOut();

    // stepOut reads stack: (PCL,PCH) = ($FF,$04) -> $04FF + 1 = $0500
    REQUIRE(tempBp == 0x0500);
}
