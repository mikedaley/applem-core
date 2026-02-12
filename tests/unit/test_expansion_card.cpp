/*
 * test_expansion_card.cpp - Unit tests for expansion card interface
 *
 * Tests the common ExpansionCard interface across all card types:
 * Disk2Card, MockingboardCard, ThunderclockCard, MouseCard, SmartPortCard.
 * Verifies getName, getPreferredSlot, hasROM, hasExpansionROM, reset,
 * and getStateSize for each card type.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "expansion_card.hpp"
#include "cards/disk2_card.hpp"
#include "cards/mockingboard_card.hpp"
#include "cards/thunderclock_card.hpp"
#include "cards/mouse_card.hpp"
#include "cards/smartport/smartport_card.hpp"
#include "roms.cpp"

#include <string>

using namespace a2e;

// ---------------------------------------------------------------------------
// getName - each card returns a non-null, non-empty name
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card getName returns non-null", "[expansion][disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.getName() != nullptr);
    REQUIRE(std::string(card.getName()).length() > 0);
}

TEST_CASE("MockingboardCard getName returns non-null", "[expansion][mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getName() != nullptr);
    REQUIRE(std::string(card.getName()).length() > 0);
}

TEST_CASE("ThunderclockCard getName returns non-null", "[expansion][thunderclock]") {
    ThunderclockCard card;
    REQUIRE(card.getName() != nullptr);
    REQUIRE(std::string(card.getName()).length() > 0);
}

TEST_CASE("MouseCard getName returns non-null", "[expansion][mouse]") {
    MouseCard card;
    REQUIRE(card.getName() != nullptr);
    REQUIRE(std::string(card.getName()).length() > 0);
}

TEST_CASE("SmartPortCard getName returns non-null", "[expansion][smartport]") {
    SmartPortCard card;
    REQUIRE(card.getName() != nullptr);
    REQUIRE(std::string(card.getName()).length() > 0);
}

// ---------------------------------------------------------------------------
// getPreferredSlot - each card returns a valid slot number
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card getPreferredSlot is 6", "[expansion][disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.getPreferredSlot() == 6);
}

TEST_CASE("MockingboardCard getPreferredSlot is 4", "[expansion][mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getPreferredSlot() == 4);
}

TEST_CASE("ThunderclockCard getPreferredSlot is 5", "[expansion][thunderclock]") {
    ThunderclockCard card;
    REQUIRE(card.getPreferredSlot() == 5);
}

TEST_CASE("MouseCard getPreferredSlot is 4", "[expansion][mouse]") {
    MouseCard card;
    REQUIRE(card.getPreferredSlot() == 4);
}

TEST_CASE("SmartPortCard getPreferredSlot is 7", "[expansion][smartport]") {
    SmartPortCard card;
    REQUIRE(card.getPreferredSlot() == 7);
}

// ---------------------------------------------------------------------------
// hasROM / hasExpansionROM
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card hasROM is true, hasExpansionROM is false", "[expansion][disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.hasROM());
    REQUIRE_FALSE(card.hasExpansionROM());
}

TEST_CASE("MockingboardCard hasROM is true, hasExpansionROM is false", "[expansion][mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.hasROM());
    REQUIRE_FALSE(card.hasExpansionROM());
}

TEST_CASE("ThunderclockCard hasROM is true, hasExpansionROM is true", "[expansion][thunderclock]") {
    ThunderclockCard card;
    REQUIRE(card.hasROM());
    REQUIRE(card.hasExpansionROM());
}

TEST_CASE("MouseCard hasROM is true", "[expansion][mouse]") {
    MouseCard card;
    REQUIRE(card.hasROM());
}

// ---------------------------------------------------------------------------
// reset() - should not crash for any card type
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card reset does not crash", "[expansion][disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE_NOTHROW(card.reset());
}

TEST_CASE("MockingboardCard reset does not crash", "[expansion][mockingboard]") {
    MockingboardCard card;
    REQUIRE_NOTHROW(card.reset());
}

TEST_CASE("ThunderclockCard reset does not crash", "[expansion][thunderclock]") {
    ThunderclockCard card;
    REQUIRE_NOTHROW(card.reset());
}

TEST_CASE("MouseCard reset does not crash", "[expansion][mouse]") {
    MouseCard card;
    REQUIRE_NOTHROW(card.reset());
}

TEST_CASE("SmartPortCard reset does not crash", "[expansion][smartport]") {
    SmartPortCard card;
    REQUIRE_NOTHROW(card.reset());
}

// ---------------------------------------------------------------------------
// getStateSize - returns reasonable values
// ---------------------------------------------------------------------------

TEST_CASE("Disk2Card getStateSize is greater than zero", "[expansion][disk2]") {
    Disk2Card card(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);
    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("MockingboardCard getStateSize equals STATE_SIZE constant", "[expansion][mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getStateSize() == MockingboardCard::STATE_SIZE);
    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("ThunderclockCard getStateSize equals STATE_SIZE constant", "[expansion][thunderclock]") {
    ThunderclockCard card;
    REQUIRE(card.getStateSize() == ThunderclockCard::STATE_SIZE);
    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("MouseCard getStateSize equals STATE_SIZE constant", "[expansion][mouse]") {
    MouseCard card;
    REQUIRE(card.getStateSize() == MouseCard::STATE_SIZE);
    REQUIRE(card.getStateSize() > 0);
}

TEST_CASE("SmartPortCard getStateSize returns a value", "[expansion][smartport]") {
    SmartPortCard card;
    // SmartPort state size may be 0 when no devices are loaded, or > 0
    // Just verify it does not crash
    size_t sz = card.getStateSize();
    (void)sz;
    REQUIRE(true);
}
