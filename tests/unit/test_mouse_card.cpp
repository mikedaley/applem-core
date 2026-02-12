/*
 * test_mouse_card.cpp - Unit tests for MouseCard
 *
 * Tests the Apple Mouse Interface card implementation including:
 * - Construction
 * - Card metadata (name, preferred slot)
 * - ROM presence
 * - ROM data reading
 * - Mouse delta input and position tracking
 * - Button state tracking
 * - Position clamping
 * - PIA register access
 * - Reset behavior
 * - Serialization round-trip
 * - Slot number configuration
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mouse_card.hpp"
#include "roms.cpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard constructor creates valid instance", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getName() != nullptr);
}

// ---------------------------------------------------------------------------
// Card metadata
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard getName returns Mouse", "[mouse]") {
    MouseCard card;
    REQUIRE(std::string(card.getName()) == "Mouse");
}

TEST_CASE("MouseCard getPreferredSlot returns 4", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getPreferredSlot() == 4);
}

// ---------------------------------------------------------------------------
// ROM presence
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard hasROM returns true", "[mouse]") {
    MouseCard card;
    REQUIRE(card.hasROM());
}

TEST_CASE("MouseCard hasExpansionROM returns false", "[mouse]") {
    MouseCard card;
    REQUIRE_FALSE(card.hasExpansionROM());
}

// ---------------------------------------------------------------------------
// ROM data reading
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard readROM returns ROM data from embedded mouse ROM", "[mouse]") {
    MouseCard card;

    // The mouse ROM should not be all zeros
    bool allZero = true;
    for (int i = 0; i < 256; ++i) {
        if (card.readROM(static_cast<uint8_t>(i)) != 0x00) {
            allZero = false;
            break;
        }
    }
    REQUIRE_FALSE(allZero);
}

TEST_CASE("MouseCard readROM returns ROM data not all 0xFF", "[mouse]") {
    MouseCard card;

    bool allFF = true;
    for (int i = 0; i < 256; ++i) {
        if (card.readROM(static_cast<uint8_t>(i)) != 0xFF) {
            allFF = false;
            break;
        }
    }
    REQUIRE_FALSE(allFF);
}

// ---------------------------------------------------------------------------
// Mouse delta input: addDelta
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard initial position is 0,0", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getMouseX() == 0);
    REQUIRE(card.getMouseY() == 0);
}

TEST_CASE("MouseCard addDelta changes position", "[mouse]") {
    MouseCard card;

    card.addDelta(10, 20);
    REQUIRE(card.getMouseX() == 10);
    REQUIRE(card.getMouseY() == 20);
}

TEST_CASE("MouseCard addDelta sets moved flag", "[mouse]") {
    MouseCard card;

    REQUIRE_FALSE(card.getMoved());
    card.addDelta(5, 5);
    REQUIRE(card.getMoved());
}

TEST_CASE("MouseCard addDelta accumulates", "[mouse]") {
    MouseCard card;

    card.addDelta(10, 10);
    card.addDelta(5, -3);
    REQUIRE(card.getMouseX() == 15);
    REQUIRE(card.getMouseY() == 7);
}

TEST_CASE("MouseCard addDelta with zero does not change position", "[mouse]") {
    MouseCard card;

    card.addDelta(0, 0);
    REQUIRE(card.getMouseX() == 0);
    REQUIRE(card.getMouseY() == 0);
}

// ---------------------------------------------------------------------------
// Button state
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard button starts unpressed", "[mouse]") {
    MouseCard card;
    REQUIRE_FALSE(card.getMouseButton());
}

TEST_CASE("MouseCard setMouseButton true sets button pressed", "[mouse]") {
    MouseCard card;

    card.setMouseButton(true);
    REQUIRE(card.getMouseButton());
}

TEST_CASE("MouseCard setMouseButton sets buttonChanged flag", "[mouse]") {
    MouseCard card;

    REQUIRE_FALSE(card.getButtonChanged());
    card.setMouseButton(true);
    REQUIRE(card.getButtonChanged());
}

TEST_CASE("MouseCard setMouseButton false after true clears button", "[mouse]") {
    MouseCard card;

    card.setMouseButton(true);
    REQUIRE(card.getMouseButton());

    card.setMouseButton(false);
    REQUIRE_FALSE(card.getMouseButton());
}

// ---------------------------------------------------------------------------
// Position clamping
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard default clamp range is 0-1023", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getClampMinX() == 0);
    REQUIRE(card.getClampMaxX() == 1023);
    REQUIRE(card.getClampMinY() == 0);
    REQUIRE(card.getClampMaxY() == 1023);
}

TEST_CASE("MouseCard position clamps to max bounds", "[mouse]") {
    MouseCard card;

    // Move far beyond the default max clamp of 1023
    card.addDelta(2000, 2000);

    REQUIRE(card.getMouseX() <= card.getClampMaxX());
    REQUIRE(card.getMouseY() <= card.getClampMaxY());
}

TEST_CASE("MouseCard position clamps to min bounds", "[mouse]") {
    MouseCard card;

    // Move far negative beyond the default min clamp of 0
    card.addDelta(-1000, -1000);

    REQUIRE(card.getMouseX() >= card.getClampMinX());
    REQUIRE(card.getMouseY() >= card.getClampMinY());
}

// ---------------------------------------------------------------------------
// PIA register access
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard readIO for PIA registers does not crash", "[mouse]") {
    MouseCard card;

    // PIA has 4 registers at offsets 0-3
    for (uint8_t offset = 0; offset < 4; ++offset) {
        uint8_t val = card.readIO(offset);
        (void)val;
    }
    REQUIRE(true);
}

TEST_CASE("MouseCard writeIO for PIA registers does not crash", "[mouse]") {
    MouseCard card;

    for (uint8_t offset = 0; offset < 4; ++offset) {
        card.writeIO(offset, 0x00);
        card.writeIO(offset, 0xFF);
    }
    REQUIRE(true);
}

TEST_CASE("MouseCard peekIO does not crash", "[mouse]") {
    MouseCard card;

    for (uint8_t offset = 0; offset < 4; ++offset) {
        uint8_t val = card.peekIO(offset);
        (void)val;
    }
    REQUIRE(true);
}

TEST_CASE("MouseCard PIA debug accessors return initial values", "[mouse]") {
    MouseCard card;

    // After construction, PIA registers should be 0
    REQUIRE(card.getDDRA() == 0);
    REQUIRE(card.getDDRB() == 0);
    REQUIRE(card.getORA() == 0);
    REQUIRE(card.getORB() == 0);
    REQUIRE(card.getCRA() == 0);
    REQUIRE(card.getCRB() == 0);
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard reset clears state", "[mouse]") {
    MouseCard card;

    // Set up some state
    card.addDelta(100, 200);
    card.setMouseButton(true);

    card.reset();

    // After reset, position and button should be cleared
    REQUIRE(card.getMouseX() == 0);
    REQUIRE(card.getMouseY() == 0);
    REQUIRE_FALSE(card.getMouseButton());
    REQUIRE_FALSE(card.getMoved());
    REQUIRE_FALSE(card.getButtonChanged());
}

TEST_CASE("MouseCard reset clears PIA registers", "[mouse]") {
    MouseCard card;

    // Write to PIA registers to set state
    card.writeIO(0, 0xFF);
    card.writeIO(1, 0xFF);

    card.reset();

    REQUIRE(card.getDDRA() == 0);
    REQUIRE(card.getDDRB() == 0);
    REQUIRE(card.getORA() == 0);
    REQUIRE(card.getORB() == 0);
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard getStateSize is correct", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getStateSize() == MouseCard::STATE_SIZE);
}

TEST_CASE("MouseCard serialize/deserialize round-trip", "[mouse]") {
    MouseCard card1;

    // Set up some state
    card1.addDelta(50, 75);
    card1.setMouseButton(true);

    int16_t xBefore = card1.getMouseX();
    int16_t yBefore = card1.getMouseY();
    bool btnBefore  = card1.getMouseButton();

    // Serialize
    std::vector<uint8_t> buffer(card1.getStateSize());
    size_t written = card1.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);
    REQUIRE(written <= buffer.size());

    // Deserialize
    MouseCard card2;
    size_t consumed = card2.deserialize(buffer.data(), written);
    REQUIRE(consumed > 0);

    // Verify state preserved
    REQUIRE(card2.getMouseX() == xBefore);
    REQUIRE(card2.getMouseY() == yBefore);
    REQUIRE(card2.getMouseButton() == btnBefore);
}

// ---------------------------------------------------------------------------
// Slot number
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard default slot is 4", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getSlotNumber() == 4);
}

TEST_CASE("MouseCard setSlotNumber changes slot", "[mouse]") {
    MouseCard card;

    card.setSlotNumber(2);
    REQUIRE(card.getSlotNumber() == 2);

    card.setSlotNumber(7);
    REQUIRE(card.getSlotNumber() == 7);
}

// ---------------------------------------------------------------------------
// IRQ state
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard isIRQActive is false initially", "[mouse]") {
    MouseCard card;
    REQUIRE_FALSE(card.isIRQActive());
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard update does not crash", "[mouse]") {
    MouseCard card;
    card.update(100);
    card.update(1000);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// Mode
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard initial mode is 0", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getMode() == 0);
}

// ---------------------------------------------------------------------------
// Debug accessors
// ---------------------------------------------------------------------------

TEST_CASE("MouseCard getLastCommand returns 0 initially", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getLastCommand() == 0);
}

TEST_CASE("MouseCard getResponseState returns 0 initially", "[mouse]") {
    MouseCard card;
    REQUIRE(card.getResponseState() == 0);
}
