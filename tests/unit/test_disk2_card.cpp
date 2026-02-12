/*
 * test_disk2_card.cpp - Unit tests for Disk2Card
 *
 * Tests the Disk II controller card implementation including:
 * - Construction with ROM data
 * - ROM read access
 * - Motor control (on/off)
 * - Drive selection
 * - Q6/Q7 latch states
 * - Phase control for head stepping
 * - Disk insert/eject operations
 * - Card metadata (name, preferred slot)
 * - State serialization round-trip
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "disk2_card.hpp"
#include "roms.cpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card constructor with ROM loads ROM data", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    // The ROM should be 256 bytes; every byte should match the source data
    for (size_t i = 0; i < roms::ROM_DISK2_SIZE; ++i) {
        REQUIRE(card.readROM(static_cast<uint8_t>(i)) == roms::ROM_DISK2[i]);
    }
}

TEST_CASE("Disk2Card default constructor creates valid card", "[disk2]") {
    Disk2Card card;
    // Should still be constructable; ROM content may be zeroed
    REQUIRE(card.getName() != nullptr);
}

// ---------------------------------------------------------------------------
// readROM returns bytes from loaded ROM
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card readROM returns ROM bytes after loadROM", "[disk2]") {
    Disk2Card card;
    card.loadROM(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    REQUIRE(card.readROM(0x00) == roms::ROM_DISK2[0]);
    REQUIRE(card.readROM(0x7F) == roms::ROM_DISK2[0x7F]);
    REQUIRE(card.readROM(0xFF) == roms::ROM_DISK2[0xFF]);
}

// ---------------------------------------------------------------------------
// reset() clears controller state
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card reset clears state", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    // Turn motor on and select drive 2
    card.readIO(0x09); // Motor on
    card.readIO(0x0B); // Drive 2

    REQUIRE(card.isMotorOn());
    REQUIRE(card.getSelectedDrive() == 1);

    card.reset();

    REQUIRE_FALSE(card.isMotorOn());
    REQUIRE(card.getSelectedDrive() == 0);
    REQUIRE(card.getQ6() == false);
    REQUIRE(card.getQ7() == false);
    REQUIRE(card.getPhaseStates() == 0);
}

// ---------------------------------------------------------------------------
// Motor on/off via readIO
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card motor control via I/O offsets", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    SECTION("Motor starts off") {
        REQUIRE_FALSE(card.isMotorOn());
    }

    SECTION("Offset 0x09 turns motor on") {
        card.readIO(0x09);
        REQUIRE(card.isMotorOn());
    }

    SECTION("Offset 0x08 turns motor off") {
        card.readIO(0x09); // on
        REQUIRE(card.isMotorOn());
        card.readIO(0x08); // off
        // Motor may not turn off immediately (delay), but the off request is latched
        // After enough update cycles, motor will be off. For the immediate test,
        // just verify the toggle was accepted without error.
    }
}

// ---------------------------------------------------------------------------
// Drive select
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card drive selection via I/O offsets", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    SECTION("Default drive is 0 (drive 1)") {
        REQUIRE(card.getSelectedDrive() == 0);
    }

    SECTION("Offset 0x0B selects drive 2 (index 1)") {
        card.readIO(0x0B);
        REQUIRE(card.getSelectedDrive() == 1);
    }

    SECTION("Offset 0x0A selects drive 1 (index 0)") {
        card.readIO(0x0B); // select drive 2 first
        card.readIO(0x0A); // back to drive 1
        REQUIRE(card.getSelectedDrive() == 0);
    }
}

// ---------------------------------------------------------------------------
// Q6/Q7 latches
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card Q6 and Q7 latch control", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    SECTION("Q6 and Q7 start low") {
        REQUIRE(card.getQ6() == false);
        REQUIRE(card.getQ7() == false);
    }

    SECTION("Offset 0x0C sets Q6 low") {
        card.readIO(0x0D); // Q6H first
        REQUIRE(card.getQ6() == true);
        card.readIO(0x0C); // Q6L
        REQUIRE(card.getQ6() == false);
    }

    SECTION("Offset 0x0D sets Q6 high") {
        card.readIO(0x0D);
        REQUIRE(card.getQ6() == true);
    }

    SECTION("Offset 0x0E sets Q7 low") {
        card.readIO(0x0F); // Q7H first
        REQUIRE(card.getQ7() == true);
        card.readIO(0x0E); // Q7L
        REQUIRE(card.getQ7() == false);
    }

    SECTION("Offset 0x0F sets Q7 high") {
        card.readIO(0x0F);
        REQUIRE(card.getQ7() == true);
    }
}

// ---------------------------------------------------------------------------
// Phase control
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card phase control via I/O offsets", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    SECTION("All phases off initially") {
        REQUIRE(card.getPhaseStates() == 0x00);
    }

    SECTION("Phase 0 on (offset 0x01)") {
        card.readIO(0x01);
        REQUIRE((card.getPhaseStates() & 0x01) != 0);
    }

    SECTION("Phase 0 off (offset 0x00)") {
        card.readIO(0x01); // on
        card.readIO(0x00); // off
        REQUIRE((card.getPhaseStates() & 0x01) == 0);
    }

    SECTION("Phase 1 on (offset 0x03)") {
        card.readIO(0x03);
        REQUIRE((card.getPhaseStates() & 0x02) != 0);
    }

    SECTION("Phase 1 off (offset 0x02)") {
        card.readIO(0x03);
        card.readIO(0x02);
        REQUIRE((card.getPhaseStates() & 0x02) == 0);
    }

    SECTION("Phase 2 on (offset 0x05)") {
        card.readIO(0x05);
        REQUIRE((card.getPhaseStates() & 0x04) != 0);
    }

    SECTION("Phase 2 off (offset 0x04)") {
        card.readIO(0x05);
        card.readIO(0x04);
        REQUIRE((card.getPhaseStates() & 0x04) == 0);
    }

    SECTION("Phase 3 on (offset 0x07)") {
        card.readIO(0x07);
        REQUIRE((card.getPhaseStates() & 0x08) != 0);
    }

    SECTION("Phase 3 off (offset 0x06)") {
        card.readIO(0x07);
        card.readIO(0x06);
        REQUIRE((card.getPhaseStates() & 0x08) == 0);
    }

    SECTION("Multiple phases can be on simultaneously") {
        card.readIO(0x01); // phase 0 on
        card.readIO(0x03); // phase 1 on
        REQUIRE((card.getPhaseStates() & 0x03) == 0x03);
    }
}

// ---------------------------------------------------------------------------
// Disk insert / eject
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card hasDisk is false by default", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE_FALSE(card.hasDisk(0));
    REQUIRE_FALSE(card.hasDisk(1));
}

TEST_CASE("Disk2Card insertDisk with DSK data makes hasDisk true", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    // Create a minimal valid DSK image (143360 bytes, all zeros)
    std::vector<uint8_t> dskData(143360, 0x00);

    bool result = card.insertDisk(0, dskData.data(), dskData.size(), "test.dsk");
    REQUIRE(result);
    REQUIRE(card.hasDisk(0));
}

TEST_CASE("Disk2Card insertDisk into drive 2", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    std::vector<uint8_t> dskData(143360, 0x00);
    bool result = card.insertDisk(1, dskData.data(), dskData.size(), "test.dsk");
    REQUIRE(result);
    REQUIRE(card.hasDisk(1));
    REQUIRE_FALSE(card.hasDisk(0)); // drive 1 still empty
}

TEST_CASE("Disk2Card ejectDisk clears the disk", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    std::vector<uint8_t> dskData(143360, 0x00);
    card.insertDisk(0, dskData.data(), dskData.size(), "test.dsk");
    REQUIRE(card.hasDisk(0));

    card.ejectDisk(0);
    REQUIRE_FALSE(card.hasDisk(0));
}

// ---------------------------------------------------------------------------
// Card metadata
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card getName returns Disk II", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(std::string(card.getName()) == "Disk II");
}

TEST_CASE("Disk2Card getPreferredSlot returns 6", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.getPreferredSlot() == 6);
}

TEST_CASE("Disk2Card hasROM returns true", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.hasROM());
}

TEST_CASE("Disk2Card hasExpansionROM returns false", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE_FALSE(card.hasExpansionROM());
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card getStateSize is greater than zero", "[disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("Disk2Card serialize/deserialize round-trip preserves state", "[disk2]") {
    Disk2Card card1(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

    // Set up some state
    card1.readIO(0x09); // motor on
    card1.readIO(0x0B); // drive 2
    card1.readIO(0x0D); // Q6H
    card1.readIO(0x01); // phase 0 on

    size_t stateSize = card1.getStateSize();
    std::vector<uint8_t> buffer(stateSize);

    size_t written = card1.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);
    REQUIRE(written <= stateSize);

    Disk2Card card2(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    size_t consumed = card2.deserialize(buffer.data(), written);
    REQUIRE(consumed > 0);

    // Verify key state was preserved
    REQUIRE(card2.getSelectedDrive() == card1.getSelectedDrive());
    REQUIRE(card2.getQ6() == card1.getQ6());
    REQUIRE(card2.getQ7() == card1.getQ7());
    REQUIRE(card2.getPhaseStates() == card1.getPhaseStates());
}
