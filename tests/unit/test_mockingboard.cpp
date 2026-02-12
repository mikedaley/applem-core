/*
 * test_mockingboard.cpp - Unit tests for MockingboardCard
 *
 * Tests the Mockingboard sound card implementation including:
 * - Construction
 * - Card metadata (name, preferred slot)
 * - VIA register access via ROM space
 * - PSG register write sequence via VIA
 * - Timer updates
 * - Reset behavior
 * - Enable/disable state
 * - Audio sample generation
 * - Serialization round-trip
 * - IRQ generation from VIA timer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mockingboard_card.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// VIA register offsets (within the VIA's 16-register space)
static constexpr uint8_t VIA_ORB  = 0x00;
static constexpr uint8_t VIA_ORA  = 0x01;
static constexpr uint8_t VIA_DDRB = 0x02;
static constexpr uint8_t VIA_DDRA = 0x03;
static constexpr uint8_t VIA_T1CL = 0x04;
static constexpr uint8_t VIA_T1CH = 0x05;
static constexpr uint8_t VIA_T1LL = 0x06;
static constexpr uint8_t VIA_T1LH = 0x07;
static constexpr uint8_t VIA_ACR  = 0x0B;
static constexpr uint8_t VIA_IFR  = 0x0D;
static constexpr uint8_t VIA_IER  = 0x0E;

// VIA1 base offset in ROM space: bit 7 = 0, so offsets 0x00-0x0F
// VIA2 base offset in ROM space: bit 7 = 1, so offsets 0x80-0x8F

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard constructor creates a valid instance", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getName() != nullptr);
}

// ---------------------------------------------------------------------------
// Card metadata
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard getName returns Mockingboard", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(std::string(card.getName()) == "Mockingboard");
}

TEST_CASE("MockingboardCard getPreferredSlot returns 4", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getPreferredSlot() == 4);
}

TEST_CASE("MockingboardCard hasROM returns true", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.hasROM());
}

TEST_CASE("MockingboardCard hasExpansionROM returns false", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE_FALSE(card.hasExpansionROM());
}

// ---------------------------------------------------------------------------
// VIA register access via ROM space
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard write/read VIA1 DDRA register", "[mockingboard]") {
    MockingboardCard card;

    // Write 0xFF to VIA1 DDRA (offset 0x03 in VIA1 space = ROM offset 0x03)
    card.writeROM(VIA_DDRA, 0xFF);
    uint8_t val = card.readROM(VIA_DDRA);
    REQUIRE(val == 0xFF);
}

TEST_CASE("MockingboardCard write/read VIA1 DDRB register", "[mockingboard]") {
    MockingboardCard card;

    card.writeROM(VIA_DDRB, 0xFF);
    uint8_t val = card.readROM(VIA_DDRB);
    REQUIRE(val == 0xFF);
}

TEST_CASE("MockingboardCard write/read VIA2 DDRA register", "[mockingboard]") {
    MockingboardCard card;

    // VIA2 registers are at offset 0x80+
    card.writeROM(0x80 | VIA_DDRA, 0xFF);
    uint8_t val = card.readROM(0x80 | VIA_DDRA);
    REQUIRE(val == 0xFF);
}

TEST_CASE("MockingboardCard write/read VIA2 DDRB register", "[mockingboard]") {
    MockingboardCard card;

    card.writeROM(0x80 | VIA_DDRB, 0xFF);
    uint8_t val = card.readROM(0x80 | VIA_DDRB);
    REQUIRE(val == 0xFF);
}

TEST_CASE("MockingboardCard VIA1 and VIA2 are independent", "[mockingboard]") {
    MockingboardCard card;

    card.writeROM(VIA_DDRA, 0xAA);
    card.writeROM(0x80 | VIA_DDRA, 0x55);

    REQUIRE(card.readROM(VIA_DDRA) == 0xAA);
    REQUIRE(card.readROM(0x80 | VIA_DDRA) == 0x55);
}

// ---------------------------------------------------------------------------
// PSG register write sequence via VIA
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard PSG register write via VIA1 protocol", "[mockingboard]") {
    MockingboardCard card;

    // The PSG write protocol via VIA:
    // 1. Set DDRA to 0xFF (all outputs) and DDRB to 0xFF (all outputs)
    // 2. Write register address to ORA
    // 3. Set ORB to 0x07 (LATCH command: BC1=1, BDIR=1, /RESET=1)
    // 4. Set ORB to 0x04 (INACTIVE: BC1=0, BDIR=0, /RESET=1)
    // 5. Write register value to ORA
    // 6. Set ORB to 0x06 (WRITE command: BC1=0, BDIR=1, /RESET=1)
    // 7. Set ORB to 0x04 (INACTIVE)

    // Step 1: Set data directions
    card.writeROM(VIA_DDRA, 0xFF);
    card.writeROM(VIA_DDRB, 0xFF);

    // Step 2: Latch register address (register 7 = mixer)
    card.writeROM(VIA_ORA, 0x07);
    card.writeROM(VIA_ORB, 0x07); // LATCH
    card.writeROM(VIA_ORB, 0x04); // INACTIVE

    // Step 3: Write value to register
    card.writeROM(VIA_ORA, 0x38); // All channels: tone on, noise off
    card.writeROM(VIA_ORB, 0x06); // WRITE
    card.writeROM(VIA_ORB, 0x04); // INACTIVE

    // Verify via debug accessor
    const AY8910& psg1 = card.getPSG1();
    REQUIRE(psg1.getRegister(7) == 0x38);
}

// ---------------------------------------------------------------------------
// update() advances timers
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard update does not crash", "[mockingboard]") {
    MockingboardCard card;

    // Simply verify update() runs without crashing
    card.update(100);
    card.update(1000);
    card.update(10000);
    REQUIRE(true); // If we get here, no crash occurred
}

// ---------------------------------------------------------------------------
// reset() clears state
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard reset clears VIA state", "[mockingboard]") {
    MockingboardCard card;

    // Write something to VIA1
    card.writeROM(VIA_DDRA, 0xFF);
    card.writeROM(VIA_ORA, 0xAA);

    card.reset();

    // After reset, DDRA should be 0
    const VIA6522& via1 = card.getVIA1();
    REQUIRE(via1.getDDRA() == 0x00);
    REQUIRE(via1.getORA() == 0x00);
}

// ---------------------------------------------------------------------------
// Enable/disable
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard isEnabled is true by default", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.isEnabled());
}

TEST_CASE("MockingboardCard setEnabled toggles state", "[mockingboard]") {
    MockingboardCard card;

    card.setEnabled(false);
    REQUIRE_FALSE(card.isEnabled());

    card.setEnabled(true);
    REQUIRE(card.isEnabled());
}

// ---------------------------------------------------------------------------
// Audio generation
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard generateStereoSamples produces output", "[mockingboard]") {
    MockingboardCard card;

    // Generate a small buffer of stereo samples
    const int frameCount = 128;
    std::vector<float> buffer(frameCount * 2, -999.0f); // interleaved L/R

    card.generateStereoSamples(buffer.data(), frameCount, 48000);

    // After generation, buffer should have been written (values should not be -999)
    bool anyWritten = false;
    for (int i = 0; i < frameCount * 2; ++i) {
        if (buffer[i] != -999.0f) {
            anyWritten = true;
            break;
        }
    }
    REQUIRE(anyWritten);
}

TEST_CASE("MockingboardCard generateStereoSamples with timing does not crash", "[mockingboard]") {
    MockingboardCard card;

    const int frameCount = 128;
    std::vector<float> buffer(frameCount * 2, 0.0f);

    card.generateStereoSamples(buffer.data(), frameCount, 48000, 0, 2730);
    REQUIRE(true); // No crash
}

// ---------------------------------------------------------------------------
// I/O space (unused by Mockingboard)
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard readIO returns a value without crash", "[mockingboard]") {
    MockingboardCard card;
    // Mockingboard does not use I/O space, but calling it should not crash
    uint8_t val = card.readIO(0x00);
    (void)val;
    REQUIRE(true);
}

TEST_CASE("MockingboardCard writeIO does not crash", "[mockingboard]") {
    MockingboardCard card;
    card.writeIO(0x00, 0x55);
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// Serialization round-trip
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard getStateSize returns expected size", "[mockingboard]") {
    MockingboardCard card;
    REQUIRE(card.getStateSize() == MockingboardCard::STATE_SIZE);
}

TEST_CASE("MockingboardCard serialize/deserialize round-trip", "[mockingboard]") {
    MockingboardCard card1;

    // Set up some state: write to PSG via VIA1
    card1.writeROM(VIA_DDRA, 0xFF);
    card1.writeROM(VIA_DDRB, 0xFF);
    card1.writeROM(VIA_ORA, 0x07);  // register address = 7
    card1.writeROM(VIA_ORB, 0x07);  // LATCH
    card1.writeROM(VIA_ORB, 0x04);  // INACTIVE
    card1.writeROM(VIA_ORA, 0x38);  // value
    card1.writeROM(VIA_ORB, 0x06);  // WRITE
    card1.writeROM(VIA_ORB, 0x04);  // INACTIVE

    // Serialize
    std::vector<uint8_t> buffer(card1.getStateSize());
    size_t written = card1.serialize(buffer.data(), buffer.size());
    REQUIRE(written > 0);
    REQUIRE(written <= buffer.size());

    // Deserialize into a new card
    MockingboardCard card2;
    size_t consumed = card2.deserialize(buffer.data(), written);
    REQUIRE(consumed > 0);

    // Verify PSG state was preserved
    REQUIRE(card2.getPSG1().getRegister(7) == card1.getPSG1().getRegister(7));
}

// ---------------------------------------------------------------------------
// IRQ: set VIA timer, update until fires, check isIRQActive
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard IRQ fires from VIA1 Timer 1", "[mockingboard]") {
    MockingboardCard card;

    // Track IRQ firing via callback
    bool irqFired = false;
    card.setIRQCallback([&irqFired]() {
        irqFired = true;
    });

    // Enable Timer 1 interrupt in VIA1:
    // Write to IER: set bit 7 (enable) and bit 6 (T1)
    card.writeROM(VIA_IER, 0xC0);

    // Set ACR for one-shot mode (bit 6 = 0)
    card.writeROM(VIA_ACR, 0x00);

    // Load a short timer value: latch low then high starts the timer
    card.writeROM(VIA_T1CL, 0x05); // low byte = 5
    card.writeROM(VIA_T1CH, 0x00); // high byte = 0 (starts timer)

    // Update enough cycles for the timer to fire (timer counts down from 5)
    for (int i = 0; i < 20; ++i) {
        card.update(1);
    }

    // Either the IRQ callback was called or the IRQ flag is set
    bool irqActive = card.isIRQActive() || irqFired;
    REQUIRE(irqActive);
}

// ---------------------------------------------------------------------------
// Debug accessors
// ---------------------------------------------------------------------------

TEST_CASE("MockingboardCard debug accessors return VIA/PSG references", "[mockingboard]") {
    MockingboardCard card;

    const VIA6522& via1 = card.getVIA1();
    const VIA6522& via2 = card.getVIA2();
    const AY8910& psg1 = card.getPSG1();
    const AY8910& psg2 = card.getPSG2();

    // Just verify the accessors work and return references
    (void)via1.getDDRA();
    (void)via2.getDDRA();
    (void)psg1.getRegister(0);
    (void)psg2.getRegister(0);
    REQUIRE(true);
}
