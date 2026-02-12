/*
 * test_emulator.cpp - Integration tests for the Emulator class
 *
 * Tests the full emulator coordinator including initialization, reset,
 * execution, memory access, video, beam position, soft switches,
 * slot management, screen text, disassembly, and speed control.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

using namespace a2e;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

TEST_CASE("Emulator init does not crash", "[emulator][init]") {
    Emulator emu;
    REQUIRE_NOTHROW(emu.init());
}

TEST_CASE("Emulator PC points to reset vector destination after init", "[emulator][init]") {
    Emulator emu;
    emu.init();

    // After reset, the 65C02 reads the reset vector at $FFFC/$FFFD and sets
    // PC to that address.  The Apple IIe ROM reset vector points into the
    // $FA00-$FFFF range (monitor / reset handler).
    uint16_t pc = emu.getPC();
    REQUIRE(pc >= 0xC000);
    REQUIRE(pc <= 0xFFFF);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("Emulator reset returns PC to reset vector", "[emulator][reset]") {
    Emulator emu;
    emu.init();

    uint16_t pcAfterInit = emu.getPC();

    // Run some cycles to move PC away from the reset address
    emu.runCycles(1000);
    REQUIRE(emu.getPC() != pcAfterInit);

    // Reset should return PC to the same reset vector destination
    emu.reset();
    REQUIRE(emu.getPC() == pcAfterInit);
}

TEST_CASE("Emulator warmReset preserves memory but resets PC", "[emulator][reset]") {
    Emulator emu;
    emu.init();

    uint16_t pcAfterInit = emu.getPC();

    // Write a known value into low RAM
    emu.writeMemory(0x0300, 0xAB);
    REQUIRE(emu.readMemory(0x0300) == 0xAB);

    // Run some cycles to move PC
    emu.runCycles(1000);

    // Warm reset: memory preserved, PC returns to reset vector
    emu.warmReset();
    REQUIRE(emu.getPC() == pcAfterInit);
    REQUIRE(emu.readMemory(0x0300) == 0xAB);
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

TEST_CASE("Emulator runCycles advances cycles and PC", "[emulator][execution]") {
    Emulator emu;
    emu.init();

    uint16_t pcBefore = emu.getPC();
    uint64_t cyclesBefore = emu.getTotalCycles();

    emu.runCycles(100);

    REQUIRE(emu.getTotalCycles() > cyclesBefore);
    REQUIRE(emu.getPC() != pcBefore);
}

TEST_CASE("Emulator stepInstruction executes one instruction", "[emulator][execution]") {
    Emulator emu;
    emu.init();

    uint64_t cyclesBefore = emu.getTotalCycles();
    uint16_t pcBefore = emu.getPC();

    emu.stepInstruction();

    // At least 2 cycles for the shortest 65C02 instruction, PC should have moved
    REQUIRE(emu.getTotalCycles() > cyclesBefore);
    REQUIRE(emu.getTotalCycles() <= cyclesBefore + 7); // max 7 cycles for any instruction
    REQUIRE(emu.getPC() != pcBefore);
}

// ---------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------

TEST_CASE("Emulator setPaused and isPaused", "[emulator][pause]") {
    Emulator emu;
    emu.init();

    REQUIRE_FALSE(emu.isPaused());

    emu.setPaused(true);
    REQUIRE(emu.isPaused());

    // runCycles should not advance when paused
    uint64_t cyclesBefore = emu.getTotalCycles();
    emu.runCycles(1000);
    REQUIRE(emu.getTotalCycles() == cyclesBefore);

    emu.setPaused(false);
    REQUIRE_FALSE(emu.isPaused());
}

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------

TEST_CASE("Emulator writeMemory and readMemory round-trip", "[emulator][memory]") {
    Emulator emu;
    emu.init();

    emu.writeMemory(0x0400, 0x42);
    REQUIRE(emu.readMemory(0x0400) == 0x42);
}

TEST_CASE("Emulator peekMemory reads same as readMemory for normal RAM", "[emulator][memory]") {
    Emulator emu;
    emu.init();

    emu.writeMemory(0x0400, 0x55);
    REQUIRE(emu.peekMemory(0x0400) == emu.readMemory(0x0400));
}

// ---------------------------------------------------------------------------
// Video / Framebuffer
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getFramebuffer returns non-null after init", "[emulator][video]") {
    Emulator emu;
    emu.init();

    const uint8_t* fb = emu.getFramebuffer();
    REQUIRE(fb != nullptr);
}

TEST_CASE("Emulator getFramebufferSize equals expected RGBA buffer size", "[emulator][video]") {
    Emulator emu;
    emu.init();

    // 560 * 384 * 4 (RGBA) = 860160
    REQUIRE(emu.getFramebufferSize() == 860160);
}

// ---------------------------------------------------------------------------
// Beam position
// ---------------------------------------------------------------------------

TEST_CASE("Emulator beam position returns valid scanline and hPos", "[emulator][beam]") {
    Emulator emu;
    emu.init();

    // Run a few cycles so beam position is somewhere meaningful
    emu.runCycles(200);

    int scanline = emu.getBeamScanline();
    int hPos = emu.getBeamHPos();

    REQUIRE(scanline >= 0);
    REQUIRE(scanline < 262);  // 262 scanlines per NTSC frame
    REQUIRE(hPos >= 0);
    REQUIRE(hPos < 65);       // 65 cycles per scanline
}

// ---------------------------------------------------------------------------
// Soft switches
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getSoftSwitchState returns packed state", "[emulator][softswitch]") {
    Emulator emu;
    emu.init();

    // getSoftSwitchState returns a 64-bit packed value; just verify it is callable
    // and produces a reasonable value (all zeros except TEXT mode which is set by default)
    uint64_t state = emu.getSoftSwitchState();
    // TEXT mode bit (bit 0) should be set on a fresh init
    REQUIRE((state & 0x01) == 0x01);
}

// ---------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------

TEST_CASE("Emulator slot management: isSlotEmpty and getSlotCardName", "[emulator][slots]") {
    Emulator emu;
    emu.init();

    SECTION("Slot 3 is never empty (built-in 80-column)") {
        REQUIRE_FALSE(emu.isSlotEmpty(3));
        REQUIRE(std::string(emu.getSlotCardName(3)) == "80col");
    }

    SECTION("Slot 6 has Disk II by default") {
        REQUIRE_FALSE(emu.isSlotEmpty(6));
        REQUIRE(std::string(emu.getSlotCardName(6)) == "disk2");
    }

    SECTION("Slot 4 has Mockingboard by default") {
        REQUIRE_FALSE(emu.isSlotEmpty(4));
        REQUIRE(std::string(emu.getSlotCardName(4)) == "mockingboard");
    }

    SECTION("Slot 1 is empty by default") {
        REQUIRE(emu.isSlotEmpty(1));
    }
}

// ---------------------------------------------------------------------------
// Screen text
// ---------------------------------------------------------------------------

TEST_CASE("Emulator readScreenText returns a string", "[emulator][screen]") {
    Emulator emu;
    emu.init();

    // Read full screen (24 rows x 40 cols)
    const char* text = emu.readScreenText(0, 0, 23, 39);
    REQUIRE(text != nullptr);
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

TEST_CASE("Emulator disassembleAt returns non-empty string", "[emulator][disasm]") {
    Emulator emu;
    emu.init();

    // Disassemble at current PC (should be in ROM after reset)
    const char* disasm = emu.disassembleAt(emu.getPC());
    REQUIRE(disasm != nullptr);
    REQUIRE(std::string(disasm).length() > 0);
}

// ---------------------------------------------------------------------------
// Speed control
// ---------------------------------------------------------------------------

TEST_CASE("Emulator speed multiplier set and get", "[emulator][speed]") {
    Emulator emu;
    emu.init();

    REQUIRE(emu.getSpeedMultiplier() == 1);

    emu.setSpeedMultiplier(2);
    REQUIRE(emu.getSpeedMultiplier() == 2);

    emu.setSpeedMultiplier(8);
    REQUIRE(emu.getSpeedMultiplier() == 8);

    // Values are clamped to 1-8
    emu.setSpeedMultiplier(0);
    REQUIRE(emu.getSpeedMultiplier() == 1);

    emu.setSpeedMultiplier(100);
    REQUIRE(emu.getSpeedMultiplier() == 8);
}

// ---------------------------------------------------------------------------
// screenCodeToAscii
// ---------------------------------------------------------------------------

TEST_CASE("Emulator screenCodeToAscii converts known codes", "[emulator][screen]") {
    // Normal ASCII 'A' = 0xC1 in Apple II screen memory
    int result = Emulator::screenCodeToAscii(0xC1);
    REQUIRE(result == 'A');
}
