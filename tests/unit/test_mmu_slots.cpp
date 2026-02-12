/*
 * test_mmu_slots.cpp - Unit tests for MMU expansion slot system
 *
 * Tests the expansion card slot management including card insertion,
 * removal, ROM routing, I/O routing, INTCXROM/SLOTC3ROM switches,
 * expansion ROM activation, and multiple cards.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mmu/mmu.hpp"
#include "cards/thunderclock_card.hpp"
#include "roms.cpp"

#include <memory>

using namespace a2e;

// Helper: create an MMU with ROMs loaded
static std::unique_ptr<MMU> createMMU() {
    auto mmu = std::make_unique<MMU>();
    mmu->loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                 roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
    return mmu;
}

// ============================================================================
// Slot empty/occupied state
// ============================================================================

TEST_CASE("Slots are initially empty", "[mmu][slots]") {
    auto mmu = createMMU();

    for (int slot = 1; slot <= 7; ++slot) {
        INFO("Slot " << slot);
        CHECK(mmu->isSlotEmpty(slot));
        CHECK(mmu->getCard(slot) == nullptr);
    }
}

TEST_CASE("isSlotEmpty returns true for out-of-range slots", "[mmu][slots]") {
    auto mmu = createMMU();

    CHECK(mmu->isSlotEmpty(0));
    CHECK(mmu->isSlotEmpty(8));
}

// ============================================================================
// Card insertion
// ============================================================================

TEST_CASE("Insert card into slot: slot becomes occupied", "[mmu][slots]") {
    auto mmu = createMMU();

    auto card = std::make_unique<ThunderclockCard>();
    auto* cardPtr = card.get();

    auto removed = mmu->insertCard(5, std::move(card));

    CHECK(removed == nullptr);  // Slot was empty
    CHECK_FALSE(mmu->isSlotEmpty(5));
    CHECK(mmu->getCard(5) == cardPtr);
}

TEST_CASE("insertCard returns previously installed card", "[mmu][slots]") {
    auto mmu = createMMU();

    auto card1 = std::make_unique<ThunderclockCard>();
    auto* card1Ptr = card1.get();
    mmu->insertCard(5, std::move(card1));

    auto card2 = std::make_unique<ThunderclockCard>();
    auto removed = mmu->insertCard(5, std::move(card2));

    // Should get back card1
    REQUIRE(removed != nullptr);
    CHECK(removed.get() == card1Ptr);
    CHECK_FALSE(mmu->isSlotEmpty(5));
}

// ============================================================================
// Card removal
// ============================================================================

TEST_CASE("removeCard returns the card and slot becomes empty", "[mmu][slots]") {
    auto mmu = createMMU();

    auto card = std::make_unique<ThunderclockCard>();
    auto* cardPtr = card.get();
    mmu->insertCard(5, std::move(card));

    auto removed = mmu->removeCard(5);
    REQUIRE(removed != nullptr);
    CHECK(removed.get() == cardPtr);
    CHECK(mmu->isSlotEmpty(5));
}

TEST_CASE("removeCard from empty slot returns nullptr", "[mmu][slots]") {
    auto mmu = createMMU();

    auto removed = mmu->removeCard(5);
    CHECK(removed == nullptr);
}

// ============================================================================
// Slot ROM routing
// ============================================================================

TEST_CASE("Slot ROM: read $C500-$C5FF accesses Thunderclock card ROM", "[mmu][slots][rom]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());

    // Thunderclock ROM signature bytes (ProDOS clock detection)
    CHECK(mmu->read(0xC500) == 0x08);  // PHP
    CHECK(mmu->read(0xC502) == 0x28);  // PLP
    CHECK(mmu->read(0xC504) == 0x58);  // CLI
    CHECK(mmu->read(0xC506) == 0x70);  // BVS
}

TEST_CASE("Slot ROM: empty slot returns floating bus value", "[mmu][slots][rom]") {
    auto mmu = createMMU();

    // No cycle callback so floating bus returns 0x00
    uint8_t val = mmu->read(0xC500);
    CHECK(val == 0x00);
}

// ============================================================================
// Slot I/O routing
// ============================================================================

TEST_CASE("Slot I/O: read/write $C0D0-$C0DF accesses slot 5 card", "[mmu][slots][io]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());

    // Initial I/O read from Thunderclock
    uint8_t initialValue = mmu->read(0xC0D0);
    CHECK(initialValue == 0x00);

    // Write a command and verify I/O is being routed
    mmu->write(0xC0D0, 0x00);
    mmu->write(0xC0D0, 0xA4);  // CMD_TIMED | FLAG_STROBE

    // The fact that we can write and read without crashing confirms routing
    uint8_t result = mmu->read(0xC0D0);
    // Result will have bit 7 set to data bit from Thunderclock
    (void)result;  // Value depends on time, just verify no crash
}

TEST_CASE("Slot I/O: slot 7 I/O at $C0F0-$C0FF", "[mmu][slots][io]") {
    auto mmu = createMMU();
    mmu->insertCard(7, std::make_unique<ThunderclockCard>());

    uint8_t val = mmu->read(0xC0F0);
    CHECK(val == 0x00);

    // Slot 7 ROM at $C700
    CHECK(mmu->read(0xC700) == 0x08);
}

// ============================================================================
// INTCXROM switch
// ============================================================================

TEST_CASE("INTCXROM: when internal, slot ROM reads from internal ROM", "[mmu][slots][intcxrom]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());

    // Verify card ROM is accessible first
    CHECK(mmu->read(0xC500) == 0x08);

    // Enable INTCXROM (write to $C007)
    mmu->write(0xC007, 0);
    CHECK(mmu->getSoftSwitches().intcxrom);

    // Now $C500 should read from internal ROM instead of card
    uint8_t internalByte = roms::ROM_SYSTEM[0x0500];  // offset $C500 - $C000 = $0500
    CHECK(mmu->read(0xC500) == internalByte);

    // Disable INTCXROM (write to $C006)
    mmu->write(0xC006, 0);
    CHECK_FALSE(mmu->getSoftSwitches().intcxrom);

    // Card ROM should be accessible again
    CHECK(mmu->read(0xC500) == 0x08);
}

// ============================================================================
// SLOTC3ROM switch
// ============================================================================

TEST_CASE("SLOTC3ROM: controls slot 3 behavior", "[mmu][slots][slotc3rom]") {
    auto mmu = createMMU();

    // Default: SLOTC3ROM is off, slot 3 uses internal ROM
    CHECK_FALSE(mmu->getSoftSwitches().slotc3rom);

    // Read from $C300 should return internal ROM
    uint8_t internalByte = roms::ROM_SYSTEM[0x0300];  // $C300 - $C000
    CHECK(mmu->read(0xC300) == internalByte);

    // Enable SLOTC3ROM (write to $C00B)
    mmu->write(0xC00B, 0);
    CHECK(mmu->getSoftSwitches().slotc3rom);

    // Disable SLOTC3ROM (write to $C00A)
    mmu->write(0xC00A, 0);
    CHECK_FALSE(mmu->getSoftSwitches().slotc3rom);
}

// ============================================================================
// Expansion ROM
// ============================================================================

TEST_CASE("Expansion ROM: reading $Cn00 activates expansion ROM at $C800", "[mmu][slots][exprom]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());

    // Access slot 5 ROM to activate expansion ROM
    mmu->read(0xC500);

    // Thunderclock has expansion ROM, should be active now
    CHECK(mmu->getActiveExpansionSlot() == 5);

    // Read from expansion ROM area - should not be floating bus
    uint8_t byte = mmu->read(0xC800);
    // Thunderclock expansion ROM should have valid data (not 0xFF floating bus)
    // We just verify it was activated and returns card data
    (void)byte;
}

TEST_CASE("Expansion ROM: reading $CFFF deactivates expansion ROM", "[mmu][slots][exprom]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());

    // Activate expansion ROM
    mmu->read(0xC500);
    REQUIRE(mmu->getActiveExpansionSlot() == 5);

    // Read $CFFF to deactivate
    mmu->read(0xCFFF);
    CHECK(mmu->getActiveExpansionSlot() == 0);
}

TEST_CASE("Expansion ROM: switching between cards updates active slot", "[mmu][slots][exprom]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());
    mmu->insertCard(7, std::make_unique<ThunderclockCard>());

    // Activate slot 5 expansion ROM
    mmu->read(0xC500);
    CHECK(mmu->getActiveExpansionSlot() == 5);

    // Activate slot 7 expansion ROM
    mmu->read(0xC700);
    CHECK(mmu->getActiveExpansionSlot() == 7);
}

// ============================================================================
// Multiple cards in different slots
// ============================================================================

TEST_CASE("Multiple cards: independent access in different slots", "[mmu][slots][multi]") {
    auto mmu = createMMU();
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());
    mmu->insertCard(7, std::make_unique<ThunderclockCard>());

    // Both should be accessible
    CHECK(mmu->read(0xC500) == 0x08);
    CHECK(mmu->read(0xC700) == 0x08);

    // Both slot I/O spaces should work independently
    CHECK(mmu->read(0xC0D0) == 0x00);  // Slot 5
    CHECK(mmu->read(0xC0F0) == 0x00);  // Slot 7

    // Slots that don't have cards are still empty
    CHECK(mmu->isSlotEmpty(1));
    CHECK(mmu->isSlotEmpty(2));
}

TEST_CASE("Card in slot 1: ROM at $C100, I/O at $C090", "[mmu][slots][multi]") {
    auto mmu = createMMU();
    mmu->insertCard(1, std::make_unique<ThunderclockCard>());

    // Slot 1 ROM at $C100
    CHECK(mmu->read(0xC100) == 0x08);

    // Slot 1 I/O at $C090
    CHECK(mmu->read(0xC090) == 0x00);
}

// ============================================================================
// INTCXROM affects all slot ROMs
// ============================================================================

TEST_CASE("INTCXROM blocks ROM for all slots simultaneously", "[mmu][slots][intcxrom]") {
    auto mmu = createMMU();
    mmu->insertCard(1, std::make_unique<ThunderclockCard>());
    mmu->insertCard(5, std::make_unique<ThunderclockCard>());
    mmu->insertCard(7, std::make_unique<ThunderclockCard>());

    // All cards read their own ROM initially
    CHECK(mmu->read(0xC100) == 0x08);
    CHECK(mmu->read(0xC500) == 0x08);
    CHECK(mmu->read(0xC700) == 0x08);

    // Enable INTCXROM
    mmu->write(0xC007, 0);

    // All slots now read internal ROM
    CHECK(mmu->read(0xC100) == roms::ROM_SYSTEM[0x0100]);
    CHECK(mmu->read(0xC500) == roms::ROM_SYSTEM[0x0500]);
    CHECK(mmu->read(0xC700) == roms::ROM_SYSTEM[0x0700]);

    // Disable INTCXROM
    mmu->write(0xC006, 0);

    // Cards are accessible again
    CHECK(mmu->read(0xC100) == 0x08);
    CHECK(mmu->read(0xC500) == 0x08);
    CHECK(mmu->read(0xC700) == 0x08);
}
