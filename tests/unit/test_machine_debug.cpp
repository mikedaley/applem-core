/*
 * test_machine_debug.cpp - Execution ranges, stack pointer and soft switch
 * breakpoints, and the switch catalog
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
#include "debug/soft_switch_catalog.hpp"

#include <string>

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

TEST_CASE("Two ranges may begin at the same address when asked to",
          "[debug][breakpoint][range]") {
  // The native debugger's list holds $2000-$20FF and $2000-$2FFF as two
  // breakpoints, so the core must too, and removing one leaves the other.
  MachineDebug debug;
  debug.addBreakpointRange(0x2000, 0x20FF, false);
  debug.addBreakpointRange(0x2000, 0x2FFF, false);
  debug.removeBreakpointRange(0x2000, 0x20FF);
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));
  REQUIRE(debug.shouldBreakBefore(0x2800, SP)); // only the longer one has this

  SECTION("and the stack's the same") {
    MachineDebug stack;
    stack.addStackBreakpoint(0x00, 0x3F, false);
    stack.addStackBreakpoint(0x00, 0x7F, false);
    stack.removeStackBreakpoint(0x00, 0x3F);
    REQUIRE_FALSE(stack.shouldBreakBefore(0x1000, 0xFF));
    REQUIRE(stack.shouldBreakBefore(0x1000, 0x60));
  }
}

TEST_CASE("Resuming skips one instruction's breakpoint, and only one",
          "[debug][breakpoint]") {
  MachineDebug debug;
  // Armed with no breakpoints set, the skip is still spent on the next
  // instruction, rather than waiting to swallow one added later.
  debug.skipNextBreakpoint();
  REQUIRE_FALSE(debug.shouldBreakBefore(0x1000, SP));
  debug.addBreakpoint(0x2000);
  REQUIRE(debug.shouldBreakBefore(0x2000, SP));

  // Sitting on it, the skip lets it through once.
  debug.clearHits();
  debug.skipNextBreakpoint();
  REQUIRE_FALSE(debug.shouldBreakBefore(0x2000, SP));
  REQUIRE(debug.shouldBreakBefore(0x2000, SP));
}

TEST_CASE("A reset counts as a resume", "[debug][breakpoint]") {
  MachineDebug debug;
  const uint64_t before = debug.resumeCount();
  debug.noteResume();
  REQUIRE(debug.resumeCount() == before + 1);
  debug.reset();
  REQUIRE(debug.resumeCount() == before + 2);
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

// ============================================================================
// Soft switch breakpoints
//
// Checked after each instruction against what the machine reads back, so
// here a "machine" is a value the test moves by hand between checks.
// ============================================================================

namespace {
constexpr uint32_t FLAGS = MachineDebug::SWITCH_FLAGS;
constexpr uint64_t PAGE2 = 1ULL << 2;
constexpr uint64_t TEXT = 1ULL << 0;
using Cond = MachineDebug::SwitchCondition;
} // namespace

TEST_CASE("A switch breakpoint on a change fires when the switch moves",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint64_t flags = TEXT;
  auto read = [&](uint32_t) { return flags; };
  const int32_t id = debug.addSwitchBreakpoint(FLAGS, PAGE2, Cond::Changes, 0);
  REQUIRE(debug.hasSwitchBreakpoints());

  // The first check only records where things are.
  REQUIRE_FALSE(debug.checkSwitches(read, 0x2000));
  // A switch the breakpoint does not watch moves nothing.
  flags = 0;
  REQUIRE_FALSE(debug.checkSwitches(read, 0x2001));

  flags |= PAGE2;
  REQUIRE(debug.checkSwitches(read, 0x2004));
  REQUIRE(debug.isSwitchBreakpointHit());
  REQUIRE(debug.switchBreakpointHitId() == id);
  REQUIRE(debug.switchHitBefore() == 0);
  REQUIRE(debug.switchHitAfter() == PAGE2);
  REQUIRE(debug.switchHitPC() == 0x2004);
  REQUIRE(debug.switchHitSource() == FLAGS);
  REQUIRE(debug.switchHitMask() == PAGE2);

  // Resuming does not fire again while it holds, and does when it goes back.
  debug.clearHits();
  REQUIRE_FALSE(debug.checkSwitches(read, 0x2007));
  flags &= ~PAGE2;
  REQUIRE(debug.checkSwitches(read, 0x2008));
  REQUIRE(debug.switchHitAfter() == 0);
}

TEST_CASE("A switch breakpoint on a value fires when the value arrives",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint64_t flags = 0;
  auto read = [&](uint32_t) { return flags; };
  debug.addSwitchBreakpoint(FLAGS, TEXT, Cond::Equals, TEXT); // TEXT on

  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  flags = TEXT;
  REQUIRE(debug.checkSwitches(read, 0));
  debug.clearHits();
  // Held there, it does not stop every instruction; going off is not the value.
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  flags = 0;
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  flags = TEXT;
  REQUIRE(debug.checkSwitches(read, 0));
}

TEST_CASE("A switch already at its value does not stop the machine on resume",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint64_t flags = TEXT;
  auto read = [&](uint32_t) { return flags; };
  debug.addSwitchBreakpoint(FLAGS, TEXT, Cond::Equals, TEXT);
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
}

TEST_CASE("A register breakpoint reads its own source under its mask",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint8_t newVideo = 0x01;
  uint64_t flags = 0;
  int flagReads = 0;
  auto read = [&](uint32_t source) -> uint64_t {
    if (source == FLAGS) {
      flagReads++;
      return flags;
    }
    REQUIRE(source == 0xC029);
    return newVideo;
  };
  // Super Hi-Res on: bit 7 set, whatever the others hold.
  debug.addSwitchBreakpoint(0xC029, 0x80, Cond::Equals, 0xFF);
  const int32_t text = debug.addSwitchBreakpoint(FLAGS, TEXT, Cond::Changes, 0);
  debug.addSwitchBreakpoint(FLAGS, PAGE2, Cond::Changes, 0);

  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  // The word is read once per check however many breakpoints look at it.
  REQUIRE(flagReads == 1);

  newVideo = 0x41; // linear video, not Super Hi-Res
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  newVideo = 0xC1;
  REQUIRE(debug.checkSwitches(read, 0x2010));
  REQUIRE(debug.switchHitBefore() == 0x00);
  REQUIRE(debug.switchHitAfter() == 0x80);
  REQUIRE(debug.switchHitSource() == 0xC029);

  // Disabled, a breakpoint keeps up with the machine and does not fire.
  debug.clearHits();
  debug.enableSwitchBreakpoint(text, false);
  flags = TEXT;
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  debug.enableSwitchBreakpoint(text, true);
  REQUIRE_FALSE(debug.checkSwitches(read, 0)); // nothing moved since
}

TEST_CASE("A step notes a switch it moved without stopping on it next time",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint64_t flags = 0;
  auto read = [&](uint32_t) { return flags; };
  debug.addSwitchBreakpoint(FLAGS, PAGE2, Cond::Changes, 0);
  debug.checkSwitches(read, 0);

  flags = PAGE2;
  REQUIRE_FALSE(debug.checkSwitches(read, 0, false)); // the step
  REQUIRE_FALSE(debug.isSwitchBreakpointHit());
  REQUIRE_FALSE(debug.checkSwitches(read, 0)); // the Run after it
}

TEST_CASE("A reset starts every switch breakpoint afresh",
          "[debug][breakpoint][switch]") {
  MachineDebug debug;
  uint64_t flags = PAGE2;
  auto read = [&](uint32_t) { return flags; };
  const int32_t id = debug.addSwitchBreakpoint(FLAGS, PAGE2, Cond::Changes, 0);
  debug.checkSwitches(read, 0);

  // The machine putting its switches back is not the program moving them.
  debug.reset();
  flags = 0;
  REQUIRE_FALSE(debug.checkSwitches(read, 0));
  REQUIRE(debug.hasSwitchBreakpoints());

  flags = PAGE2;
  REQUIRE(debug.checkSwitches(read, 0));
  debug.removeSwitchBreakpoint(id);
  REQUIRE_FALSE(debug.isSwitchBreakpointHit());
  REQUIRE_FALSE(debug.hasSwitchBreakpoints());
}

// ============================================================================
// The catalog
// ============================================================================

TEST_CASE("Each machine lists the switches it has and no others",
          "[debug][switch][machine]") {
  auto has = [](MachineId id, const char *key) {
    const auto catalog = softSwitchCatalog(machineProfile(id));
    return findSoftSwitch(catalog, key) != nullptr;
  };

  for (const char *key : {"text", "mixed", "page2", "hires", "an0", "an3",
                          "lcbank2", "btn0", "keyavail"}) {
    INFO(key);
    REQUIRE(has(MachineId::AppleIIe, key));
    REQUIRE(has(MachineId::AppleIIPlus, key));
    REQUIRE(has(MachineId::AppleIIc, key));
    REQUIRE(has(MachineId::AppleIIgs, key));
  }

  // A II+ has no auxiliary bank, no 80 columns, no $C019 and no IOU.
  for (const char *key : {"col80", "altchar", "dhires", "store80", "ramrd",
                          "altzp", "intcxrom", "vblbar", "ioudis"}) {
    INFO(key);
    REQUIRE_FALSE(has(MachineId::AppleIIPlus, key));
    REQUIRE(has(MachineId::AppleIIe, key));
  }

  // Only a II+ and a //e have a cassette port.
  REQUIRE(has(MachineId::AppleIIPlus, "cassout"));
  REQUIRE(has(MachineId::AppleIIe, "cassin"));
  REQUIRE_FALSE(has(MachineId::AppleIIc, "cassout"));
  REQUIRE_FALSE(has(MachineId::AppleIIgs, "cassin"));

  // Only a IIgs has its registers.
  REQUIRE(has(MachineId::AppleIIgs, "newvideo"));
  REQUIRE(has(MachineId::AppleIIgs, "shadow"));
  REQUIRE_FALSE(has(MachineId::AppleIIe, "newvideo"));
}

TEST_CASE("A catalog's flags are the bits the packed word holds",
          "[debug][switch]") {
  for (const MachineProfile *each : MACHINE_PROFILES) {
    const MachineProfile &profile = *each;
    const auto catalog = softSwitchCatalog(profile);
    for (const SoftSwitchInfo &s : catalog) {
      INFO(profile.name << " " << s.key);
      if (s.isRegister()) {
        REQUIRE(s.source >= 0xC000);
        REQUIRE(s.source <= 0xC0FF);
        REQUIRE(s.mask() == 0xFF);
      } else {
        // Every switch sits in the low half, which is what the bindings pass.
        REQUIRE(s.bit < 32);
        REQUIRE(s.mask() == (1ULL << s.bit));
      }
      // Keys are unique: a saved breakpoint names one switch.
      int same = 0;
      for (const SoftSwitchInfo &t : catalog) same += std::string(t.key) == s.key;
      REQUIRE(same == 1);
    }
  }
}
