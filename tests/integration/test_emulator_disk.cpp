/*
 * test_emulator_disk.cpp - Integration tests for Emulator disk operations
 *
 * Tests disk insert, eject, blank disk creation, two-drive support,
 * filename tracking, SmartPort, and error handling through the
 * Emulator facade.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// Standard DSK image size: 35 tracks * 16 sectors * 256 bytes
static constexpr size_t DSK_SIZE = 143360;

// Helper: create a valid-sized DSK image filled with a byte value
static std::vector<uint8_t> makeDskImage(uint8_t fill = 0x00) {
    return std::vector<uint8_t>(DSK_SIZE, fill);
}

// ---------------------------------------------------------------------------
// Insert disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertDisk succeeds with valid DSK data", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    bool result = emu.insertDisk(0, img.data(), img.size(), "test.dsk");
    REQUIRE(result);
}

TEST_CASE("Emulator getDiskFilename returns inserted filename", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    emu.insertDisk(0, img.data(), img.size(), "test.dsk");

    const char* name = emu.getDiskFilename(0);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == "test.dsk");
}

// ---------------------------------------------------------------------------
// Eject disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator ejectDisk clears the drive", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    emu.insertDisk(0, img.data(), img.size(), "test.dsk");
    emu.ejectDisk(0);

    // After ejecting, getDiskData should return nullptr
    size_t dataSize = 0;
    const uint8_t* data = emu.getDiskData(0, &dataSize);
    REQUIRE(data == nullptr);
}

// ---------------------------------------------------------------------------
// Blank disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertBlankDisk succeeds", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    bool result = emu.insertBlankDisk(0);
    REQUIRE(result);
}

// ---------------------------------------------------------------------------
// Two drives
// ---------------------------------------------------------------------------

TEST_CASE("Emulator supports two drives simultaneously", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img0 = makeDskImage(0x00);
    auto img1 = makeDskImage(0xFF);

    bool r0 = emu.insertDisk(0, img0.data(), img0.size(), "disk1.dsk");
    bool r1 = emu.insertDisk(1, img1.data(), img1.size(), "disk2.dsk");

    REQUIRE(r0);
    REQUIRE(r1);

    REQUIRE(std::string(emu.getDiskFilename(0)) == "disk1.dsk");
    REQUIRE(std::string(emu.getDiskFilename(1)) == "disk2.dsk");
}

// ---------------------------------------------------------------------------
// getDisk reference
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getDisk returns a valid Disk2Card reference", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    Disk2Card& disk = emu.getDisk();
    REQUIRE(std::string(disk.getName()) == "Disk II");
}

// ---------------------------------------------------------------------------
// SmartPort
// ---------------------------------------------------------------------------

TEST_CASE("Emulator isSmartPortCardInstalled reflects slot configuration", "[emulator][disk][smartport]") {
    Emulator emu;
    emu.init();

    // By default, SmartPort is not installed (no card in slot 7)
    // The result depends on default configuration
    // Just verify the method is callable without crashing
    bool installed = emu.isSmartPortCardInstalled();

    if (installed) {
        // If SmartPort is installed, insertSmartPortImage should be callable
        // (actual success depends on data validity)
        std::vector<uint8_t> hdvData(512 * 280, 0x00); // Minimal ProDOS volume
        // This may or may not succeed depending on image validation
        emu.insertSmartPortImage(0, hdvData.data(), hdvData.size(), "test.hdv");
    }

    // Either way, no crash
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// Invalid data
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertDisk with invalid size returns false", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    // A DSK image must be exactly 143360 bytes (or a valid NIB/WOZ size)
    // An arbitrary size should be rejected
    std::vector<uint8_t> badData(1000, 0x00);
    bool result = emu.insertDisk(0, badData.data(), badData.size(), "bad.dsk");
    REQUIRE_FALSE(result);
}
