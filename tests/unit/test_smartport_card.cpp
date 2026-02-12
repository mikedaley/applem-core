/*
 * test_smartport_card.cpp - Unit tests for SmartPortCard
 *
 * Tests the SmartPort hard drive controller card including:
 * - Construction
 * - Card metadata (name, preferred slot)
 * - No device initially: hasROM false, no images inserted
 * - Image insert/eject operations
 * - Filename tracking
 * - Slot number configuration
 * - Activity tracking
 * - hasROM becomes true when a device is present
 * - Serialization round-trip
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "smartport_card.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// Helper: create minimal block data (one block = 512 bytes minimum)
static std::vector<uint8_t> createMinimalImage(size_t blocks = 280) {
    return std::vector<uint8_t>(blocks * 512, 0x00);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard constructor creates valid instance", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getName() != nullptr);
}

// ---------------------------------------------------------------------------
// Card metadata
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getName returns SmartPort", "[smartport]") {
    SmartPortCard card;
    REQUIRE(std::string(card.getName()) == "SmartPort");
}

TEST_CASE("SmartPortCard getPreferredSlot returns 7", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getPreferredSlot() == 7);
}

// ---------------------------------------------------------------------------
// No device initially
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard hasROM false when no device present", "[smartport]") {
    SmartPortCard card;
    // hasROM() depends on hasAnyDevice() which should be false with no images
    REQUIRE_FALSE(card.hasROM());
}

TEST_CASE("SmartPortCard no images inserted initially", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.isImageInserted(0));
    REQUIRE_FALSE(card.isImageInserted(1));
}

// ---------------------------------------------------------------------------
// Image insert
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard insertImage makes isImageInserted true", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    bool result = card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(result);
    REQUIRE(card.isImageInserted(0));
}

TEST_CASE("SmartPortCard insertImage into device 1", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    bool result = card.insertImage(1, data.data(), data.size(), "disk2.hdv");
    REQUIRE(result);
    REQUIRE(card.isImageInserted(1));
    REQUIRE_FALSE(card.isImageInserted(0)); // device 0 still empty
}

TEST_CASE("SmartPortCard hasROM becomes true after insertImage", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.hasROM());

    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "prodos.hdv");

    REQUIRE(card.hasROM());
}

// ---------------------------------------------------------------------------
// Image filename
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getImageFilename returns inserted filename", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "myvolume.po");
    REQUIRE(card.getImageFilename(0) == "myvolume.po");
}

TEST_CASE("SmartPortCard getImageFilename returns empty when no image", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getImageFilename(0).empty());
    REQUIRE(card.getImageFilename(1).empty());
}

// ---------------------------------------------------------------------------
// Image eject
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard ejectImage makes isImageInserted false", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(card.isImageInserted(0));

    card.ejectImage(0);
    REQUIRE_FALSE(card.isImageInserted(0));
}

TEST_CASE("SmartPortCard ejectImage clears filename", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    card.ejectImage(0);
    REQUIRE(card.getImageFilename(0).empty());
}

TEST_CASE("SmartPortCard hasROM becomes false after ejecting all devices", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();

    card.insertImage(0, data.data(), data.size(), "test.hdv");
    REQUIRE(card.hasROM());

    card.ejectImage(0);
    REQUIRE_FALSE(card.hasROM());
}

// ---------------------------------------------------------------------------
// Slot number
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard default slot number is 7", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.getSlotNumber() == 7);
}

TEST_CASE("SmartPortCard setSlotNumber changes slot", "[smartport]") {
    SmartPortCard card;

    card.setSlotNumber(5);
    REQUIRE(card.getSlotNumber() == 5);

    card.setSlotNumber(2);
    REQUIRE(card.getSlotNumber() == 2);
}

// ---------------------------------------------------------------------------
// Activity tracking
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard hasActivity is false initially", "[smartport]") {
    SmartPortCard card;
    REQUIRE_FALSE(card.hasActivity());
}

TEST_CASE("SmartPortCard clearActivity does not crash when no activity", "[smartport]") {
    SmartPortCard card;
    card.clearActivity();
    REQUIRE_FALSE(card.hasActivity());
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard reset does not crash", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    card.reset();
    // Image should still be inserted after reset (reset clears state, not images)
    REQUIRE(card.isImageInserted(0));
}

// ---------------------------------------------------------------------------
// I/O space
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard readIO returns a value without crash", "[smartport]") {
    SmartPortCard card;
    uint8_t val = card.readIO(0x00);
    (void)val;
    REQUIRE(true);
}

TEST_CASE("SmartPortCard writeIO does not crash", "[smartport]") {
    SmartPortCard card;
    card.writeIO(0x00, 0x55);
    REQUIRE(true);
}

TEST_CASE("SmartPortCard peekIO returns 0xFF", "[smartport]") {
    SmartPortCard card;
    REQUIRE(card.peekIO(0x00) == 0xFF);
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard getStateSize is greater than zero when image loaded", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("SmartPortCard serialize/deserialize round-trip", "[smartport]") {
    SmartPortCard card1;
    auto data = createMinimalImage();
    card1.insertImage(0, data.data(), data.size(), "saved.hdv");
    card1.setSlotNumber(5);

    // Serialize
    size_t stateSize = card1.getStateSize();
    std::vector<uint8_t> buffer(stateSize);
    size_t written = card1.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);
    REQUIRE(written <= stateSize);

    // Deserialize
    SmartPortCard card2;
    size_t consumed = card2.deserialize(buffer.data(), written);
    REQUIRE(consumed > 0);

    // Verify image and filename were preserved
    REQUIRE(card2.isImageInserted(0));
    REQUIRE(card2.getImageFilename(0) == "saved.hdv");
}

// ---------------------------------------------------------------------------
// Multiple devices
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard supports two devices simultaneously", "[smartport]") {
    SmartPortCard card;
    auto data1 = createMinimalImage(280);
    auto data2 = createMinimalImage(560);

    card.insertImage(0, data1.data(), data1.size(), "disk1.hdv");
    card.insertImage(1, data2.data(), data2.size(), "disk2.hdv");

    REQUIRE(card.isImageInserted(0));
    REQUIRE(card.isImageInserted(1));
    REQUIRE(card.getImageFilename(0) == "disk1.hdv");
    REQUIRE(card.getImageFilename(1) == "disk2.hdv");
}

TEST_CASE("SmartPortCard eject one device keeps the other", "[smartport]") {
    SmartPortCard card;
    auto data1 = createMinimalImage();
    auto data2 = createMinimalImage();

    card.insertImage(0, data1.data(), data1.size(), "disk1.hdv");
    card.insertImage(1, data2.data(), data2.size(), "disk2.hdv");

    card.ejectImage(0);

    REQUIRE_FALSE(card.isImageInserted(0));
    REQUIRE(card.isImageInserted(1));
    // hasROM should still be true because device 1 is present
    REQUIRE(card.hasROM());
}

// ---------------------------------------------------------------------------
// MAX_DEVICES constant
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard MAX_DEVICES is 2", "[smartport]") {
    REQUIRE(SmartPortCard::MAX_DEVICES == 2);
}

// ---------------------------------------------------------------------------
// ROM access with device loaded
// ---------------------------------------------------------------------------

TEST_CASE("SmartPortCard readROM returns valid data when device loaded", "[smartport]") {
    SmartPortCard card;
    auto data = createMinimalImage();
    card.insertImage(0, data.data(), data.size(), "test.hdv");

    // The self-built ROM should not be all zeros
    bool allZero = true;
    for (int i = 0; i < 256; ++i) {
        if (card.readROM(static_cast<uint8_t>(i)) != 0x00) {
            allZero = false;
            break;
        }
    }
    REQUIRE_FALSE(allZero);
}
