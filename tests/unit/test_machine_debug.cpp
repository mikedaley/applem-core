/*
 * test_machine_debug.cpp - Execution ranges and stack pointer breakpoints
 *
 * Both are ranges that fire on *entry*: the check before each instruction
 * stops the machine when the value moves from outside a range to inside it,
 * and not again while it stays there. These are asked of MachineDebug alone,
 * one simulated instruction at a time; test_emulator_debug.cpp and
 * test_iigs_debug.cpp ask the same of a running machine.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "debug/machine_debug.hpp"

using namespace a2e;

namespace {
constexpr uint32_t SP = 0xFF; // somewhere for the stack pointer to sit
}

TEST_CASE("An execution range fires when the PC enters it",
          "[debug][breakpoint][range]") {
  MachineDebug debug;
  debug.addBreakpointRange(0x2000, 0x20FF);

  REQUIRE_FALSE(debug.shouldBreakBefore(0x1FFE, SP));
  REQUIRE(debug.shouldBreakBefore(0x2010, SP)); // a jump into the middle
  REQUIRE(debug.isBreakpointHit());
  REQUIRE(debug.breakpointAddress() == 0x2010);

  // Still inside: running on does not stop on every instruction.
  debug.clearHits();
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2012, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x20FF, SP));

  // Out and back in is another entry.
  REQUIRE_FALSE(debug.shouldBreakBefore(0x3000, SP));
  REQUIRE(debug.shouldBreakBefore(0x2000, SP));
}

TEST_CASE("A range added around the paused PC waits for the next entry",
          "[debug][breakpoint][range]") {
  // The machine is paused inside the code the range is put around. Resuming
  // enters nothing, so it must not stop at once.
  MachineDebug debug;
  debug.addBreakpointRange(0x2000, 0x20FF);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2050, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2052, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));
  REQUIRE(debug.shouldBreakBefore(0x2000, SP));
}

TEST_CASE("A disabled or removed range does not fire",
          "[debug][breakpoint][range]") {
  MachineDebug debug;
  debug.addBreakpointRange(0x2000, 0x20FF);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));

  debug.enableBreakpointRange(0x2000, false);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2000, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));

  debug.enableBreakpointRange(0x2000, true);
  REQUIRE(debug.shouldBreakBefore(0x2000, SP));

  debug.removeBreakpointRange(0x2000);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2000, SP));
}

TEST_CASE("A range written backwards is the same range",
          "[debug][breakpoint][range]") {
  MachineDebug debug;
  debug.addBreakpointRange(0x20FF, 0x2000);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));
  REQUIRE(debug.shouldBreakBefore(0x2080, SP));
}

TEST_CASE("A range carries a bank", "[debug][breakpoint][range]") {
  MachineDebug debug;
  debug.addBreakpointRange(0x022000, 0x0220FF);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x001000, SP));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x032000, SP)); // same offset, other bank
  REQUIRE(debug.shouldBreakBefore(0x022000, SP));
}

TEST_CASE("A stack pointer breakpoint fires when SP enters its range",
          "[debug][breakpoint][stack]") {
  MachineDebug debug;
  debug.addStackBreakpoint(0x00, 0x3F); // the stack running away

  REQUIRE_FALSE(debug.shouldBreakBefore(0x0300, 0x41));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0301, 0x40));
  REQUIRE(debug.shouldBreakBefore(0x0302, 0x3F));
  REQUIRE(debug.isStackBreakpointHit());
  REQUIRE(debug.stackBreakpointHitLow() == 0x00);
  // A stack hit is not a PC breakpoint: the host looks those up by address.
  REQUIRE_FALSE(debug.isBreakpointHit());

  debug.clearHits();
  REQUIRE_FALSE(debug.isStackBreakpointHit());
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0303, 0x3E)); // deeper is still inside
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0304, 0x80)); // back out
  REQUIRE(debug.shouldBreakBefore(0x0305, 0x20));        // and in again
}

TEST_CASE("A single stack value fires when SP reaches it",
          "[debug][breakpoint][stack]") {
  MachineDebug debug;
  debug.addStackBreakpoint(0xF0, 0xF0);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0300, 0xF2));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0301, 0xF1));
  REQUIRE(debug.shouldBreakBefore(0x0302, 0xF0));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0303, 0xEF)); // passed through
}

TEST_CASE("A stack breakpoint is sixteen bits for a 65816",
          "[debug][breakpoint][stack]") {
  MachineDebug debug;
  debug.addStackBreakpoint(0x0100, 0x01FF); // emulation mode's page one
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0300, 0x1FFF));
  REQUIRE(debug.shouldBreakBefore(0x0301, 0x01FF));
}

TEST_CASE("A disabled or removed stack breakpoint does not fire",
          "[debug][breakpoint][stack]") {
  MachineDebug debug;
  debug.addStackBreakpoint(0x00, 0x3F);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0300, 0xFF));
  debug.enableStackBreakpoint(0x00, false);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0301, 0x10));
  debug.removeStackBreakpoint(0x00);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0302, 0xFF));
  REQUIRE_FALSE(debug.shouldBreakBefore(0x0303, 0x10));
}

TEST_CASE("clearBreakpoints takes ranges and stack breakpoints too",
          "[debug][breakpoint]") {
  MachineDebug debug;
  debug.addBreakpointRange(0x2000, 0x20FF);
  debug.addStackBreakpoint(0x00, 0x3F);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, 0xFF));
  debug.clearBreakpoints();
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2000, 0x10));
}
