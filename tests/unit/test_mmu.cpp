/*
 * test_mmu.cpp - Unit tests for MMU (Memory Management Unit)
 *
 * Tests the core memory management unit including basic read/write,
 * ROM access, zero page, stack, Language Card switching, auxiliary
 * memory, soft switches, callbacks, paddle input, memory tracking,
 * peek(), and reset().
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mmu/mmu.hpp"
#include "roms.cpp"

#include <array>
#include <memory>
#include <set>

using namespace a2e;

// Helper: create an MMU with ROMs loaded
static std::unique_ptr<MMU> createMMU() {
    auto mmu = std::make_unique<MMU>();
    mmu->loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                 roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
    return mmu;
}

// ============================================================================
// Basic read/write
// ============================================================================

TEST_CASE("MMU basic write and read back at $0400", "[mmu][basic]") {
    auto mmu = createMMU();

    mmu->write(0x0400, 0x42);
    REQUIRE(mmu->read(0x0400) == 0x42);
}

TEST_CASE("MMU write and read multiple locations", "[mmu][basic]") {
    auto mmu = createMMU();

    mmu->write(0x0400, 0xAA);
    mmu->write(0x0401, 0xBB);
    mmu->write(0x0402, 0xCC);

    CHECK(mmu->read(0x0400) == 0xAA);
    CHECK(mmu->read(0x0401) == 0xBB);
    CHECK(mmu->read(0x0402) == 0xCC);
}

// ============================================================================
// ROM read
// ============================================================================

TEST_CASE("MMU reads reset vector from ROM at $FFFC/$FFFD", "[mmu][rom]") {
    auto mmu = createMMU();

    // The reset vector is at the end of the 16KB ROM.
    // ROM offset for $FFFC = $FFFC - $C000 = $3FFC
    uint8_t lo = roms::ROM_SYSTEM[0x3FFC];
    uint8_t hi = roms::ROM_SYSTEM[0x3FFD];

    // Default is ROM read (lcram is false after reset)
    CHECK(mmu->read(0xFFFC) == lo);
    CHECK(mmu->read(0xFFFD) == hi);
}

TEST_CASE("MMU reads ROM correctly at $C100 range with INTCXROM", "[mmu][rom]") {
    auto mmu = createMMU();

    // Enable INTCXROM so $C100-$CFFF reads internal ROM
    mmu->write(0xC007, 0);  // INTCXROM on

    // Internal ROM byte at $C100 = systemROM_[$0100]
    uint8_t expected = roms::ROM_SYSTEM[0x0100];
    CHECK(mmu->read(0xC100) == expected);
}

// ============================================================================
// Zero page
// ============================================================================

TEST_CASE("MMU zero page write and read back", "[mmu][zeropage]") {
    auto mmu = createMMU();

    for (int i = 0; i < 256; ++i) {
        mmu->write(static_cast<uint16_t>(i), static_cast<uint8_t>(i ^ 0xA5));
    }

    for (int i = 0; i < 256; ++i) {
        INFO("Zero page address $" << std::hex << i);
        REQUIRE(mmu->read(static_cast<uint16_t>(i)) == static_cast<uint8_t>(i ^ 0xA5));
    }
}

// ============================================================================
// Stack page
// ============================================================================

TEST_CASE("MMU stack page write and read back", "[mmu][stack]") {
    auto mmu = createMMU();

    for (int i = 0; i < 256; ++i) {
        uint16_t addr = 0x0100 + i;
        mmu->write(addr, static_cast<uint8_t>(i));
    }

    for (int i = 0; i < 256; ++i) {
        uint16_t addr = 0x0100 + i;
        INFO("Stack page address $" << std::hex << addr);
        REQUIRE(mmu->read(addr) == static_cast<uint8_t>(i));
    }
}

// ============================================================================
// Language Card switches
// ============================================================================

TEST_CASE("Language Card: read $C08B twice enables LC write", "[mmu][lc]") {
    auto mmu = createMMU();

    // $C08B = $C080 | 0x0B => reg = 0x8B, op = 3, bank2 = false (bank 1)
    // op=3 => read RAM, write enable on second read
    mmu->read(0xC08B);  // First read: prewrite = true, write still false
    CHECK_FALSE(mmu->getSoftSwitches().lcwrite);

    mmu->read(0xC08B);  // Second read: write enabled
    CHECK(mmu->getSoftSwitches().lcwrite);
    CHECK(mmu->getSoftSwitches().lcram);  // Reading RAM enabled
}

TEST_CASE("Language Card: $C080 enables LC read RAM, disables write", "[mmu][lc]") {
    auto mmu = createMMU();

    // First enable write via double read of $C08B
    mmu->read(0xC08B);
    mmu->read(0xC08B);
    REQUIRE(mmu->getSoftSwitches().lcwrite);

    // $C080: op=0 => read RAM, write disabled
    mmu->read(0xC080);
    CHECK(mmu->getSoftSwitches().lcram);
    CHECK_FALSE(mmu->getSoftSwitches().lcwrite);
}

TEST_CASE("Language Card: write to LC RAM after enabling write", "[mmu][lc]") {
    auto mmu = createMMU();

    // Enable LC RAM read + write (bank 2): $C083 twice
    mmu->read(0xC083);
    mmu->read(0xC083);
    REQUIRE(mmu->getSoftSwitches().lcram);
    REQUIRE(mmu->getSoftSwitches().lcwrite);

    // Write to $E000 and read back
    mmu->write(0xE000, 0xDE);
    CHECK(mmu->read(0xE000) == 0xDE);
}

// ============================================================================
// LC bank switching
// ============================================================================

TEST_CASE("Language Card: $C088 selects bank 1, $C080 selects bank 2", "[mmu][lc][bank]") {
    auto mmu = createMMU();

    // $C088: reg=0x88, bank2 = !(0x88 & 0x08) = !(0x08) = false => bank 1
    mmu->read(0xC088);
    CHECK_FALSE(mmu->getSoftSwitches().lcram2);  // lcram2=false means bank 1

    // $C080: reg=0x80, bank2 = !(0x80 & 0x08) = !(0x00) = true => bank 2
    mmu->read(0xC080);
    CHECK(mmu->getSoftSwitches().lcram2);  // lcram2=true means bank 2
}

TEST_CASE("Language Card: bank 1 and bank 2 are independent", "[mmu][lc][bank]") {
    auto mmu = createMMU();

    // Enable LC RAM read + write bank 1: $C08B twice
    mmu->read(0xC08B);
    mmu->read(0xC08B);
    mmu->write(0xD000, 0xAA);

    // Switch to bank 2: $C083 twice
    mmu->read(0xC083);
    mmu->read(0xC083);
    mmu->write(0xD000, 0xBB);

    // Read bank 2
    CHECK(mmu->read(0xD000) == 0xBB);

    // Switch back to bank 1 and verify
    mmu->read(0xC08B);
    mmu->read(0xC08B);
    CHECK(mmu->read(0xD000) == 0xAA);
}

// ============================================================================
// Auxiliary memory (RAMWRT / RAMRD)
// ============================================================================

TEST_CASE("Aux memory: RAMWRT routes writes to aux, read stays main", "[mmu][aux]") {
    auto mmu = createMMU();

    // Write initial value to main memory
    mmu->write(0x0900, 0x11);
    CHECK(mmu->read(0x0900) == 0x11);

    // Enable RAMWRT (writes go to aux)
    mmu->write(0xC005, 0);  // RAMWRT on
    mmu->write(0x0900, 0x22);

    // Disable RAMWRT
    mmu->write(0xC004, 0);  // RAMWRT off

    // Main memory should still have old value
    CHECK(mmu->read(0x0900) == 0x11);

    // Aux memory should have new value
    CHECK(mmu->readRAM(0x0900, true) == 0x22);
}

TEST_CASE("Aux memory: RAMRD routes reads to aux", "[mmu][aux]") {
    auto mmu = createMMU();

    // Write different values to main and aux
    mmu->writeRAM(0x0900, 0xAA, false);
    mmu->writeRAM(0x0900, 0xBB, true);

    // Default: read from main
    CHECK(mmu->read(0x0900) == 0xAA);

    // Enable RAMRD
    mmu->write(0xC003, 0);  // RAMRD on
    CHECK(mmu->read(0x0900) == 0xBB);

    // Disable RAMRD
    mmu->write(0xC002, 0);  // RAMRD off
    CHECK(mmu->read(0x0900) == 0xAA);
}

// ============================================================================
// ALTZP
// ============================================================================

TEST_CASE("ALTZP: routes zero page to aux memory", "[mmu][altzp]") {
    auto mmu = createMMU();

    // Write to main zero page
    mmu->write(0x0010, 0xAA);

    // Write different value to aux zero page
    mmu->writeRAM(0x0010, 0xBB, true);

    // Default: main zero page
    CHECK(mmu->read(0x0010) == 0xAA);

    // Enable ALTZP
    mmu->write(0xC009, 0);
    CHECK(mmu->read(0x0010) == 0xBB);

    // Disable ALTZP
    mmu->write(0xC008, 0);
    CHECK(mmu->read(0x0010) == 0xAA);
}

// ============================================================================
// 80STORE + PAGE2
// ============================================================================

TEST_CASE("80STORE+PAGE2: writes to $0400-$07FF go to aux", "[mmu][80store]") {
    auto mmu = createMMU();

    // Write initial value to main text page
    mmu->write(0x0400, 0x11);

    // Enable 80STORE
    mmu->write(0xC001, 0);  // store80 on

    // Enable PAGE2
    mmu->write(0xC055, 0);  // page2 on (read or write toggles it)

    // Write to $0400 - should go to aux due to 80STORE+PAGE2
    mmu->write(0x0400, 0x22);

    // Disable PAGE2 and 80STORE to read main
    mmu->write(0xC054, 0);  // page2 off
    mmu->write(0xC000, 0);  // store80 off

    // Main should still have original value
    CHECK(mmu->read(0x0400) == 0x11);

    // Aux should have the new value
    CHECK(mmu->readRAM(0x0400, true) == 0x22);
}

// ============================================================================
// Display soft switches
// ============================================================================

TEST_CASE("Display switches: reading $C050-$C057 toggles switches", "[mmu][switches]") {
    auto mmu = createMMU();

    // Default: text=true
    CHECK(mmu->getSoftSwitches().text);

    // $C050 = TXTCLR = graphics mode
    mmu->read(0xC050);
    CHECK_FALSE(mmu->getSoftSwitches().text);

    // $C051 = TXTSET = text mode
    mmu->read(0xC051);
    CHECK(mmu->getSoftSwitches().text);

    // $C052 = MIXCLR
    mmu->read(0xC053);  // MIXSET on
    CHECK(mmu->getSoftSwitches().mixed);
    mmu->read(0xC052);  // MIXCLR off
    CHECK_FALSE(mmu->getSoftSwitches().mixed);

    // $C054/$C055 = PAGE1/PAGE2
    mmu->read(0xC055);
    CHECK(mmu->getSoftSwitches().page2);
    mmu->read(0xC054);
    CHECK_FALSE(mmu->getSoftSwitches().page2);

    // $C056/$C057 = LORES/HIRES
    mmu->read(0xC057);
    CHECK(mmu->getSoftSwitches().hires);
    mmu->read(0xC056);
    CHECK_FALSE(mmu->getSoftSwitches().hires);
}

// ============================================================================
// Keyboard callback
// ============================================================================

TEST_CASE("Keyboard callback: read $C000 returns callback value", "[mmu][keyboard]") {
    auto mmu = createMMU();

    mmu->setKeyboardCallback([]() -> uint8_t { return 0xC1; });  // 'A' with high bit

    uint8_t val = mmu->read(0xC000);
    CHECK(val == 0xC1);
}

// ============================================================================
// Speaker callback
// ============================================================================

TEST_CASE("Speaker callback: read $C030 triggers speaker callback", "[mmu][speaker]") {
    auto mmu = createMMU();

    bool called = false;
    mmu->setSpeakerCallback([&called]() { called = true; });

    mmu->read(0xC030);
    CHECK(called);
}

TEST_CASE("Speaker callback: write $C030 also triggers speaker callback", "[mmu][speaker]") {
    auto mmu = createMMU();

    bool called = false;
    mmu->setSpeakerCallback([&called]() { called = true; });

    mmu->write(0xC030, 0x00);
    CHECK(called);
}

// ============================================================================
// Paddle values
// ============================================================================

TEST_CASE("Paddle: setPaddleValue and getPaddleValue", "[mmu][paddle]") {
    auto mmu = createMMU();

    mmu->setPaddleValue(0, 200);
    CHECK(mmu->getPaddleValue(0) == 200);

    mmu->setPaddleValue(1, 50);
    CHECK(mmu->getPaddleValue(1) == 50);

    // Out of range paddle returns 128 (default)
    CHECK(mmu->getPaddleValue(5) == 128);
}

TEST_CASE("Paddle: read $C064 returns timer state based on cycles", "[mmu][paddle]") {
    auto mmu = createMMU();

    uint64_t currentCycle = 0;
    mmu->setCycleCallback([&currentCycle]() -> uint64_t { return currentCycle; });

    mmu->setPaddleValue(0, 100);

    // Trigger paddle timers
    mmu->read(0xC070);

    // Immediately after trigger: timer should be running (bit 7 = 1)
    uint8_t val = mmu->read(0xC064);
    CHECK((val & 0x80) == 0x80);

    // Advance cycles past timer duration (100 * 11 = 1100 cycles)
    currentCycle = 1200;
    val = mmu->read(0xC064);
    CHECK((val & 0x80) == 0x00);
}

// ============================================================================
// Memory tracking
// ============================================================================

TEST_CASE("Memory tracking: enableTracking records read and write counts", "[mmu][tracking]") {
    auto mmu = createMMU();

    mmu->enableTracking(true);
    mmu->clearTracking();

    // Perform some reads and writes
    mmu->write(0x0800, 0x42);
    mmu->read(0x0800);
    mmu->read(0x0800);

    const uint8_t* readCounts = mmu->getReadCounts();
    const uint8_t* writeCounts = mmu->getWriteCounts();

    CHECK(readCounts[0x0800] == 2);
    CHECK(writeCounts[0x0800] == 1);
}

TEST_CASE("Memory tracking: clearTracking resets all counts", "[mmu][tracking]") {
    auto mmu = createMMU();

    mmu->enableTracking(true);
    mmu->write(0x0800, 0x42);
    mmu->read(0x0800);

    mmu->clearTracking();

    CHECK(mmu->getReadCounts()[0x0800] == 0);
    CHECK(mmu->getWriteCounts()[0x0800] == 0);
}

TEST_CASE("Memory tracking: decayTracking reduces counts", "[mmu][tracking]") {
    auto mmu = createMMU();

    mmu->enableTracking(true);
    mmu->clearTracking();

    // Read 5 times
    for (int i = 0; i < 5; ++i) {
        mmu->read(0x0800);
    }
    REQUIRE(mmu->getReadCounts()[0x0800] == 5);

    mmu->decayTracking(2);
    CHECK(mmu->getReadCounts()[0x0800] == 3);

    // Decay past zero should clamp to 0
    mmu->decayTracking(10);
    CHECK(mmu->getReadCounts()[0x0800] == 0);
}

// ============================================================================
// peek() - non-side-effecting read
// ============================================================================

TEST_CASE("peek does not trigger side effects", "[mmu][peek]") {
    auto mmu = createMMU();

    // Write a known value to test page
    mmu->write(0x0400, 0x42);

    // peek should return the value
    CHECK(mmu->peek(0x0400) == 0x42);

    // peek of speaker address should NOT trigger speaker callback
    bool speakerCalled = false;
    mmu->setSpeakerCallback([&speakerCalled]() { speakerCalled = true; });
    mmu->peek(0xC030);
    CHECK_FALSE(speakerCalled);

    // peek of keyboard address should NOT trigger keyboard callback
    bool keyboardCalled = false;
    mmu->setKeyboardCallback([&keyboardCalled]() -> uint8_t {
        keyboardCalled = true;
        return 0xC1;
    });
    mmu->peek(0xC000);
    CHECK_FALSE(keyboardCalled);
}

TEST_CASE("peek does not update memory tracking", "[mmu][peek]") {
    auto mmu = createMMU();

    mmu->enableTracking(true);
    mmu->clearTracking();

    mmu->write(0x0800, 0x42);
    mmu->peek(0x0800);

    // peek should NOT increment read count
    CHECK(mmu->getReadCounts()[0x0800] == 0);
    // write should still have been tracked
    CHECK(mmu->getWriteCounts()[0x0800] == 1);
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("reset restores defaults", "[mmu][reset]") {
    auto mmu = createMMU();

    // Change many switches
    mmu->write(0xC005, 0);  // RAMWRT on
    mmu->write(0xC003, 0);  // RAMRD on
    mmu->write(0xC009, 0);  // ALTZP on
    mmu->write(0xC001, 0);  // 80STORE on
    mmu->read(0xC057);      // HIRES on
    mmu->read(0xC050);      // TEXT off

    // Write some data
    mmu->writeRAM(0x0400, 0x42, false);

    mmu->reset();

    const auto& sw = mmu->getSoftSwitches();
    CHECK_FALSE(sw.ramwrt);
    CHECK_FALSE(sw.ramrd);
    CHECK_FALSE(sw.altzp);
    CHECK_FALSE(sw.store80);
    CHECK_FALSE(sw.hires);
    CHECK(sw.text);  // text defaults to true

    // RAM should be cleared
    CHECK(mmu->read(0x0400) == 0x00);
}

TEST_CASE("warmReset resets switches but preserves RAM", "[mmu][reset]") {
    auto mmu = createMMU();

    // Write data and change switches
    mmu->write(0x0400, 0x42);
    mmu->write(0xC005, 0);  // RAMWRT on

    mmu->warmReset();

    const auto& sw = mmu->getSoftSwitches();
    CHECK_FALSE(sw.ramwrt);
    CHECK(sw.text);

    // RAM should be preserved
    CHECK(mmu->read(0x0400) == 0x42);
}

// ============================================================================
// Video scanner address / floating bus
// ============================================================================

namespace {

// The addresses the renderer draws from, so the scanner can be held to them
constexpr std::array<int, 24> TEXT_ROW_OFFSETS = {
    0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
    0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
    0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0};

uint16_t rendererTextAddress(int row, int col) {
    return static_cast<uint16_t>(0x0400 + TEXT_ROW_OFFSETS[row] + col);
}

uint16_t rendererHiResAddress(int scanline, int col) {
    return static_cast<uint16_t>(0x2000 + TEXT_ROW_OFFSETS[scanline / 8] +
                                 (scanline % 8) * 0x400 + col);
}

// Cycle at which the scanner fetches a given visible column of a given line.
// Our frame cycle counts from the start of horizontal blanking, so the visible
// picture starts 25 cycles in.
uint64_t visibleCycle(int scanline, int col) {
    return static_cast<uint64_t>(scanline) * CYCLES_PER_SCANLINE + 25 + col;
}

uint64_t hblankCycle(int scanline, int hPos) {
    return static_cast<uint64_t>(scanline) * CYCLES_PER_SCANLINE + hPos;
}

} // namespace

TEST_CASE("Scanner addresses over the visible picture match what the renderer draws",
          "[mmu][video][scanner]") {
    auto mmu = createMMU();
    uint64_t cycle = 0;
    mmu->setCycleCallback([&cycle]() { return cycle; });

    SECTION("Text") {
        mmu->read(0xC051); // TEXT on
        for (int row = 0; row < 24; row++) {
            for (int line = 0; line < 8; line++) {
                int scanline = row * 8 + line;
                for (int col = 0; col < 40; col++) {
                    uint16_t addr =
                        mmu->getVideoScannerAddress(visibleCycle(scanline, col));
                    REQUIRE(addr == rendererTextAddress(row, col));
                }
            }
        }
    }

    SECTION("HiRes") {
        mmu->read(0xC050); // GRAPHICS
        mmu->read(0xC057); // HIRES
        mmu->read(0xC052); // Full screen, no mixed text at the bottom
        for (int scanline = 0; scanline < 192; scanline++) {
            for (int col = 0; col < 40; col++) {
                uint16_t addr =
                    mmu->getVideoScannerAddress(visibleCycle(scanline, col));
                REQUIRE(addr == rendererHiResAddress(scanline, col));
            }
        }
    }
}

TEST_CASE("Scanner keeps fetching through horizontal blanking", "[mmu][video][scanner]") {
    auto mmu = createMMU();
    uint64_t cycle = 0;
    mmu->setCycleCallback([&cycle]() { return cycle; });
    mmu->read(0xC051); // TEXT

    // Horizontal blanking reads the 25 bytes past the end of the row, which in
    // text mode are the screen holes at $x78-$x7F and the unused bytes below
    // them — never the same address twice, and never a stuck value.
    std::set<uint16_t> seen;
    for (int hPos = 0; hPos < 25; hPos++) {
        uint16_t addr = mmu->getVideoScannerAddress(hblankCycle(0, hPos));
        CHECK(addr >= 0x0400);
        CHECK(addr < 0x0800);
        // Past the 40 visible columns of row 0, which end at $0427
        CHECK(addr >= 0x0428);
        seen.insert(addr);
    }
    // 24 addresses over 25 cycles: the horizontal preset repeats one counter
    // state, so the first two blanking cycles fetch the same address
    CHECK(seen.size() == 24);

    // The last blanking cycle is followed immediately by column 0 of the row
    CHECK(mmu->getVideoScannerAddress(hblankCycle(0, 24)) == 0x047F);
    CHECK(mmu->getVideoScannerAddress(visibleCycle(0, 0)) == 0x0400);
}

TEST_CASE("Floating bus returns real memory during horizontal blanking",
          "[mmu][video][floatingbus]") {
    auto mmu = createMMU();
    uint64_t cycle = 0;
    mmu->setCycleCallback([&cycle]() { return cycle; });
    mmu->read(0xC051); // TEXT

    // Put a known byte where the scanner will be during blanking
    uint16_t hblankAddr = mmu->getVideoScannerAddress(hblankCycle(10, 5));
    mmu->write(hblankAddr, 0xA5);

    cycle = hblankCycle(10, 5);
    CHECK(mmu->read(0xC020) == 0xA5); // Cassette toggle: floating bus read
    CHECK(mmu->read(0xC021) == 0xA5); // Undriven address
    CHECK(mmu->read(0xC030) == 0xA5); // Speaker

    // And still tracks the picture during the visible part of the line
    uint16_t visibleAddr = mmu->getVideoScannerAddress(visibleCycle(10, 7));
    mmu->write(visibleAddr, 0x5A);
    cycle = visibleCycle(10, 7);
    CHECK(mmu->read(0xC020) == 0x5A);
}

TEST_CASE("Floating bus follows the beam across a whole line", "[mmu][video][floatingbus]") {
    auto mmu = createMMU();
    uint64_t cycle = 0;
    mmu->setCycleCallback([&cycle]() { return cycle; });
    mmu->read(0xC051); // TEXT

    // Fill the row's whole 128-byte block with a position-dependent pattern
    // that is never zero, so a stuck blanking value would stand out
    for (uint16_t addr = 0x0400; addr < 0x0480; addr++) {
        mmu->write(addr, static_cast<uint8_t>((addr & 0x7F) + 1));
    }

    std::set<uint16_t> addresses;
    for (int hPos = 0; hPos < CYCLES_PER_SCANLINE; hPos++) {
        cycle = hblankCycle(0, hPos);
        CHECK(mmu->read(0xC020) != 0x00);
        addresses.insert(mmu->getVideoScannerAddress(cycle));
    }

    // 65 cycles cover 64 addresses: the horizontal preset repeats one counter
    // state, so the scanner fetches that address on two consecutive cycles
    // (UTAIIe 3-11) — the first two cycles of blanking here.
    CHECK(addresses.size() == 64);
    CHECK(mmu->getVideoScannerAddress(hblankCycle(0, 0)) ==
          mmu->getVideoScannerAddress(hblankCycle(0, 1)));
}

TEST_CASE("Scanner honours page and mixed-mode switches", "[mmu][video][scanner]") {
    auto mmu = createMMU();
    uint64_t cycle = 0;
    mmu->setCycleCallback([&cycle]() { return cycle; });

    SECTION("Text page 2") {
        mmu->read(0xC051); // TEXT
        mmu->read(0xC055); // PAGE2
        CHECK(mmu->getVideoScannerAddress(visibleCycle(0, 0)) == 0x0800);
    }

    SECTION("80STORE pins the address to page 1") {
        mmu->read(0xC051);
        mmu->write(0xC001, 0); // 80STORE on
        mmu->read(0xC055);     // PAGE2 now selects aux, not page 2
        CHECK(mmu->getVideoScannerAddress(visibleCycle(0, 0)) == 0x0400);
    }

    SECTION("HiRes page 2") {
        mmu->read(0xC050);
        mmu->read(0xC057);
        mmu->read(0xC052);
        mmu->read(0xC055); // PAGE2
        CHECK(mmu->getVideoScannerAddress(visibleCycle(0, 0)) == 0x4000);
    }

    SECTION("Mixed mode fetches the bottom four rows from text memory") {
        mmu->read(0xC050); // GRAPHICS
        mmu->read(0xC057); // HIRES
        mmu->read(0xC053); // MIXED

        // Rows 0-19 are hires
        CHECK(mmu->getVideoScannerAddress(visibleCycle(0, 0)) == 0x2000);
        // Rows 20-23 (scanlines 160+) are text
        uint16_t addr = mmu->getVideoScannerAddress(visibleCycle(160, 0));
        CHECK(addr == rendererTextAddress(20, 0));
    }
}
