/*
 * test_pr0_trace_bug.cpp - Regression: writing Applesoft to memory from the
 * BASIC window must not leave Applesoft TRACE on ("#NN" spam).
 *
 * The BASIC window's "Load into memory" calls C++ a2e::loadBasicProgram(),
 * which injects a tokenized program directly, bypassing the ROM's program-entry
 * path. If a stale TRCFLG ($F2 bit7) is left set, RUN prints "#<line>" before
 * every line and NOTRACE/POKE only clear it transiently. loadBasicProgram must
 * clear $F2 so an injected program is never in trace mode.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"
#include "basic/basic_tokenizer.hpp"

#include <map>

using namespace a2e;

namespace {

void typeKey(Emulator& emu, int ascii) { emu.keyDown(ascii); emu.runCycles(60'000); }
void typeStr(Emulator& emu, const char* s) { for (; *s; ++s) typeKey(emu, (unsigned char)*s); }

void bootApplesoft(Emulator& emu) {
    emu.runCycles(2'000'000);
    emu.setPC(0xE000);
    emu.runCycles(4'000'000);
}

// Count '#' ($A3, high-bit) glyphs on the 40-col text page (the TRACE marker).
int countHashes(Emulator& emu) {
    int n = 0;
    for (uint16_t a = 0x0400; a < 0x0800; ++a)
        if (emu.peekMemory(a) == 0xA3) ++n;
    return n;
}

} // namespace

TEST_CASE("loadBasicProgram clears stale TRCFLG so RUN does not trace-spam",
          "[basic][trace][regression]") {
    Emulator emu;
    emu.init();
    emu.reset();
    bootApplesoft(emu);

    // Simulate the real-world buggy state: a stale trace flag set in $F2.
    emu.writeMemory(0x00F2, 0x80);

    // Window "Load into memory" path.
    auto rd = [&](uint16_t a) { return emu.readMemory(a); };
    auto wr = [&](uint16_t a, uint8_t v) { emu.writeMemory(a, v); };
    int lines = a2e::loadBasicProgram("10 PRINT \"A\"\n20 PRINT \"B\"\n30 END\n", rd, wr);
    REQUIRE(lines == 3);

    // Fix: the inject path must clear TRCFLG.
    REQUIRE(emu.peekMemory(0x00F2) == 0x00);

    // And RUN must not emit any "#NN" trace lines.
    int before = countHashes(emu);
    typeStr(emu, "RUN\r");
    emu.runCycles(2'000'000);
    REQUIRE(countHashes(emu) == before);
}

TEST_CASE("loadBasicProgram clears the BASIC.SYSTEM TRCFLG save slot ($BE41)",
          "[basic][trace][regression]") {
    // Under ProDOS BASIC.SYSTEM, $F2 is repurposed as an $A5 sentinel and the
    // live Applesoft TRCFLG is parked in BASIC.SYSTEM's save slot at $BE41.
    // Clearing $F2 alone is undone when BASIC.SYSTEM restores $BE41, so the inject
    // path must clear $BE41 when the sentinel is present.
    std::map<uint16_t, uint8_t> mem;
    auto rd = [&](uint16_t a) { return (uint8_t)(mem.count(a) ? mem[a] : 0); };
    auto wr = [&](uint16_t a, uint8_t v) { mem[a] = v; };

    mem[0x73] = 0x00; mem[0x74] = 0x96; // HIMEM
    // Simulate the stuck state: sentinel active in $F2, trace bit set in $BE41.
    mem[0x00F2] = 0xA5;
    mem[0xBE41] = 0x80;

    int n = a2e::loadBasicProgram("10 PRINT \"A\"\n20 END\n", rd, wr);
    REQUIRE(n == 2);
    REQUIRE(rd(0xBE41) == 0x00); // BASIC.SYSTEM's live TRCFLG must be cleared
    REQUIRE(rd(0x00F2) == 0x00);

    // When the sentinel is NOT present (raw Applesoft), $BE41 must be left alone.
    std::map<uint16_t, uint8_t> mem2;
    auto rd2 = [&](uint16_t a) { return (uint8_t)(mem2.count(a) ? mem2[a] : 0); };
    auto wr2 = [&](uint16_t a, uint8_t v) { mem2[a] = v; };
    mem2[0x73] = 0x00; mem2[0x74] = 0x96;
    mem2[0x00F2] = 0x80;  // raw Applesoft TRCFLG, not the sentinel
    mem2[0xBE41] = 0x42;  // unrelated memory under raw Applesoft
    REQUIRE(a2e::loadBasicProgram("10 END\n", rd2, wr2) == 1);
    REQUIRE(rd2(0x00F2) == 0x00); // raw TRCFLG cleared
    REQUIRE(rd2(0xBE41) == 0x42); // untouched (no BASIC.SYSTEM)
}
