/*
 * test_emulator_state.cpp - Integration tests for Emulator state serialization
 *
 * Tests exportState, importState, round-trip fidelity, CPU state preservation,
 * and error handling for invalid data.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// Export state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator exportState returns non-null data with size > 0", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);

    REQUIRE(data != nullptr);
    REQUIRE(size > 0);
}

TEST_CASE("Emulator exportState size is reasonable", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    emu.exportState(&size);

    // State includes 128KB RAM (main + aux), 16KB LC RAM, CPU state,
    // soft switches, disk state, etc.  Should be at least 128KB.
    REQUIRE(size >= 128 * 1024);
    // But not unreasonably large (under 2MB)
    REQUIRE(size < 2 * 1024 * 1024);
}

// ---------------------------------------------------------------------------
// Import state
// ---------------------------------------------------------------------------

TEST_CASE("Emulator importState accepts previously exported data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    REQUIRE(data != nullptr);

    // Copy the exported data since import may reset internal buffers
    std::vector<uint8_t> stateCopy(data, data + size);

    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);
}

// ---------------------------------------------------------------------------
// Round-trip: memory preserved
// ---------------------------------------------------------------------------

TEST_CASE("Emulator state round-trip preserves memory", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Write known values to several RAM locations
    emu.writeMemory(0x0300, 0xDE);
    emu.writeMemory(0x0301, 0xAD);
    emu.writeMemory(0x0302, 0xBE);
    emu.writeMemory(0x0303, 0xEF);

    // Export
    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    // Reset clears memory
    emu.reset();
    // Verify memory was cleared (reset re-initializes)
    // Note: after reset, ROM re-initializes; RAM at $0300 should be cleared
    REQUIRE(emu.readMemory(0x0300) != 0xDE);

    // Import previously saved state
    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);

    // Memory should be restored
    REQUIRE(emu.readMemory(0x0300) == 0xDE);
    REQUIRE(emu.readMemory(0x0301) == 0xAD);
    REQUIRE(emu.readMemory(0x0302) == 0xBE);
    REQUIRE(emu.readMemory(0x0303) == 0xEF);
}

// ---------------------------------------------------------------------------
// Round-trip: CPU state preserved
// ---------------------------------------------------------------------------

TEST_CASE("Emulator state round-trip preserves CPU registers", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Set specific CPU register values
    emu.setA(0x42);
    emu.setX(0x13);
    emu.setY(0x77);

    // Export
    size_t size = 0;
    const uint8_t* data = emu.exportState(&size);
    std::vector<uint8_t> stateCopy(data, data + size);

    // Reset changes registers
    emu.reset();
    REQUIRE(emu.getA() != 0x42);

    // Import
    bool result = emu.importState(stateCopy.data(), stateCopy.size());
    REQUIRE(result);

    REQUIRE(emu.getA() == 0x42);
    REQUIRE(emu.getX() == 0x13);
    REQUIRE(emu.getY() == 0x77);
}

// ---------------------------------------------------------------------------
// Invalid data
// ---------------------------------------------------------------------------

TEST_CASE("Emulator importState rejects garbage data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Create garbage data that does not have a valid magic header
    std::vector<uint8_t> garbage(1024, 0xFF);
    bool result = emu.importState(garbage.data(), garbage.size());
    REQUIRE_FALSE(result);
}

TEST_CASE("Emulator importState rejects too-small data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    // Data smaller than the minimum header (8 bytes for magic + version)
    std::vector<uint8_t> tooSmall(4, 0x00);
    bool result = emu.importState(tooSmall.data(), tooSmall.size());
    REQUIRE_FALSE(result);
}

TEST_CASE("Emulator importState rejects empty data", "[emulator][state]") {
    Emulator emu;
    emu.init();

    bool result = emu.importState(nullptr, 0);
    REQUIRE_FALSE(result);
}
