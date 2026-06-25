/*
 * test_parallel_pr1.cpp - Regression: PR#1 to the Apple Parallel Interface Card
 * must actually drive the printer, not hang the CPU.
 *
 * The 341-0005 "Parallel Printer" firmware busy-waits on the printer ACK via
 * PROMSEL ROM-address remapping: the card rewrites ROM address bit 6 on every
 * fetch from the ACK latch, so the CPU runs a different instruction stream while
 * a byte is in flight vs. once the printer is ready. Without that remap (a flat
 * readROM) the firmware fetches the wrong bytes and spins forever, so no byte
 * ever reaches the parallel port — the long-standing PR#1 hang.
 *
 * This test boots Applesoft, installs the parallel card in slot 1, captures the
 * Centronics output, hooks the firmware with PR#1, prints, and asserts that real
 * characters are clocked out of the port. Pre-fix this produced zero bytes.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <string>
#include <vector>

using namespace a2e;

namespace {

void typeKey(Emulator& emu, int ascii) { emu.keyDown(ascii); emu.runCycles(60'000); }
void typeStr(Emulator& emu, const char* s) { for (; *s; ++s) typeKey(emu, (unsigned char)*s); }

void bootApplesoft(Emulator& emu) {
    emu.runCycles(2'000'000);
    emu.setPC(0xE000);
    emu.runCycles(4'000'000);
}

} // namespace

TEST_CASE("PR#1 drives the parallel card instead of hanging the CPU",
          "[parallel][pr1][regression]") {
    Emulator emu;
    emu.init();
    emu.reset();

    REQUIRE(emu.setSlotCard(1, "parallel") == true);
    REQUIRE(emu.isParallelCardInstalled() == true);

    std::vector<uint8_t> out;
    emu.setParallelTxCallback([&](uint8_t b) { out.push_back(b); });

    bootApplesoft(emu);

    // Hook the slot-1 firmware as the active output device (CSW -> $C100),
    // print a known string, then return output to the screen.
    typeStr(emu, "PR#1\r");
    typeStr(emu, "PRINT \"HI\"\r");
    typeStr(emu, "PR#0\r");

    // The firmware must have clocked real characters out of the parallel port.
    // (Pre-fix the firmware spun in its PROMSEL busy-wait and emitted nothing.)
    REQUIRE(out.size() > 0);

    // 'H' immediately followed by 'I' must appear in the stream — it is emitted
    // both as keyboard echo of the PRINT command and as the PRINT output itself.
    // The video/printer firmware sets the high bit, so compare on 7-bit ASCII.
    std::string s;
    for (uint8_t b : out) s.push_back(static_cast<char>(b & 0x7F));
    REQUIRE(s.find("HI") != std::string::npos);
}
