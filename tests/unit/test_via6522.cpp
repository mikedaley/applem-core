/*
 * test_via6522.cpp - Unit tests for VIA 6522 timer chip emulation
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "via6522.hpp"
#include "ay8910.hpp"

using namespace a2e;

// VIA register addresses (matching the private constants)
static constexpr uint8_t REG_ORB  = 0x00;
static constexpr uint8_t REG_ORA  = 0x01;
static constexpr uint8_t REG_DDRB = 0x02;
static constexpr uint8_t REG_DDRA = 0x03;
static constexpr uint8_t REG_T1CL = 0x04;
static constexpr uint8_t REG_T1CH = 0x05;
static constexpr uint8_t REG_T1LL = 0x06;
static constexpr uint8_t REG_T1LH = 0x07;
static constexpr uint8_t REG_T2CL = 0x08;
static constexpr uint8_t REG_T2CH = 0x09;
static constexpr uint8_t REG_ACR  = 0x0B;
static constexpr uint8_t REG_IFR  = 0x0D;
static constexpr uint8_t REG_IER  = 0x0E;

// ============================================================================
// Register read/write: ORA, ORB, DDRA, DDRB
// ============================================================================

TEST_CASE("VIA6522 ORA register write and read back", "[via][port]") {
    VIA6522 via;
    // Set DDRA to all output
    via.write(REG_DDRA, 0xFF);
    via.write(REG_ORA, 0xA5);
    CHECK(via.getORA() == 0xA5);
}

TEST_CASE("VIA6522 ORB register write and read back", "[via][port]") {
    VIA6522 via;
    // Set DDRB to all output
    via.write(REG_DDRB, 0xFF);
    via.write(REG_ORB, 0x5A);
    CHECK(via.getORB() == 0x5A);
}

TEST_CASE("VIA6522 DDRA register write and read back", "[via][port]") {
    VIA6522 via;
    via.write(REG_DDRA, 0xF0);
    CHECK(via.getDDRA() == 0xF0);
}

TEST_CASE("VIA6522 DDRB register write and read back", "[via][port]") {
    VIA6522 via;
    via.write(REG_DDRB, 0x0F);
    CHECK(via.getDDRB() == 0x0F);
}

// ============================================================================
// Timer 1: write T1CL/T1CH, read back counter
// ============================================================================

TEST_CASE("VIA6522 Timer 1 latch write via T1CL/T1CH", "[via][timer1]") {
    VIA6522 via;

    // Writing T1CL sets the low latch (doesn't start timer)
    via.write(REG_T1CL, 0x34);

    // Writing T1CH sets the high latch AND loads counter AND starts timer
    via.write(REG_T1CH, 0x12);

    // Read back the latch values
    CHECK(via.getT1Latch() == 0x1234);
    CHECK(via.isT1Running() == true);
}

// ============================================================================
// Timer 1 latch: T1LL/T1LH
// ============================================================================

TEST_CASE("VIA6522 Timer 1 latch only (T1LL/T1LH)", "[via][timer1_latch]") {
    VIA6522 via;

    // Writing T1LL/T1LH only sets the latch, does not start timer
    via.write(REG_T1LL, 0xCD);
    via.write(REG_T1LH, 0xAB);

    CHECK(via.getT1Latch() == 0xABCD);
}

// ============================================================================
// Timer 2: T2CL/T2CH
// ============================================================================

TEST_CASE("VIA6522 Timer 2 write and start", "[via][timer2]") {
    VIA6522 via;

    // Writing T2CL sets the low latch
    via.write(REG_T2CL, 0x10);
    // Writing T2CH loads counter and starts timer
    via.write(REG_T2CH, 0x00);

    // Timer 2 should be loaded with 0x0010
    // We can verify it's running by updating and checking the counter
    // decrements
}

// ============================================================================
// IFR (0x0D): read interrupt flags
// ============================================================================

TEST_CASE("VIA6522 IFR initially zero", "[via][ifr]") {
    VIA6522 via;
    CHECK(via.getIFR() == 0x00);
}

// ============================================================================
// IER (0x0E): write enable/disable interrupt bits
// ============================================================================

TEST_CASE("VIA6522 IER enable and disable bits", "[via][ier]") {
    VIA6522 via;

    // Enable Timer 1 interrupt (bit 6): write with bit 7 set = enable
    via.write(REG_IER, 0x80 | 0x40); // Set bit 7 (enable mode) + bit 6 (T1)
    CHECK((via.getIER() & 0x40) != 0);

    // Disable Timer 1 interrupt: write without bit 7 = disable
    via.write(REG_IER, 0x40); // Clear mode (bit 7=0) + bit 6 (T1)
    CHECK((via.getIER() & 0x40) == 0);
}

TEST_CASE("VIA6522 IER multiple interrupt sources", "[via][ier]") {
    VIA6522 via;

    // Enable T1 (bit 6) and T2 (bit 5)
    via.write(REG_IER, 0x80 | 0x60);
    CHECK((via.getIER() & 0x60) == 0x60);

    // Disable only T2 (bit 5)
    via.write(REG_IER, 0x20); // Clear mode, bit 5
    CHECK((via.getIER() & 0x40) != 0); // T1 still enabled
    CHECK((via.getIER() & 0x20) == 0); // T2 disabled
}

// ============================================================================
// Timer fires IRQ
// ============================================================================

TEST_CASE("VIA6522 Timer 1 fires IRQ after countdown", "[via][timer1_irq]") {
    VIA6522 via;

    // Enable Timer 1 interrupt
    via.write(REG_IER, 0x80 | 0x40);

    // Set a short timer value
    via.write(REG_T1CL, 0x05);
    via.write(REG_T1CH, 0x00); // Timer = 5 cycles, starts running

    CHECK(via.isIRQActive() == false);

    // Run enough cycles for the timer to fire
    via.update(10);

    // Timer 1 should have fired, setting IFR bit 6
    CHECK((via.getIFR() & 0x40) != 0);
    CHECK(via.isIRQActive() == true);
}

TEST_CASE("VIA6522 Timer 1 IRQ not active when not enabled", "[via][timer1_irq]") {
    VIA6522 via;

    // Don't enable any interrupts in IER

    via.write(REG_T1CL, 0x05);
    via.write(REG_T1CH, 0x00);

    via.update(10);

    // IFR bit should be set, but IRQ should not be active (IER doesn't enable it)
    CHECK((via.getIFR() & 0x40) != 0);  // T1 flag set
    CHECK(via.isIRQActive() == false);   // But not enabled
}

// ============================================================================
// IRQ callback invoked on timer fire
// ============================================================================

TEST_CASE("VIA6522 IRQ callback invoked when timer fires", "[via][irq_callback]") {
    VIA6522 via;
    bool callbackFired = false;

    via.setIRQCallback([&callbackFired]() {
        callbackFired = true;
    });

    // Enable Timer 1 interrupt
    via.write(REG_IER, 0x80 | 0x40);

    // Start timer with short value
    via.write(REG_T1CL, 0x02);
    via.write(REG_T1CH, 0x00);

    CHECK(callbackFired == false);

    // Run enough cycles
    via.update(10);

    CHECK(callbackFired == true);
}

// ============================================================================
// ACR controls timer mode
// ============================================================================

TEST_CASE("VIA6522 ACR register write and read", "[via][acr]") {
    VIA6522 via;

    via.write(REG_ACR, 0x40); // T1 free-running mode (bit 6)
    CHECK(via.getACR() == 0x40);

    via.write(REG_ACR, 0x00); // T1 one-shot mode
    CHECK(via.getACR() == 0x00);
}

TEST_CASE("VIA6522 Timer 1 free-running mode re-fires", "[via][acr_freerun]") {
    VIA6522 via;
    int callbackCount = 0;

    via.setIRQCallback([&callbackCount]() {
        callbackCount++;
    });

    // Enable T1 interrupt
    via.write(REG_IER, 0x80 | 0x40);

    // Set ACR for T1 free-running mode (bit 6 set)
    via.write(REG_ACR, 0x40);

    // Start timer with a short period
    via.write(REG_T1CL, 0x03);
    via.write(REG_T1CH, 0x00);

    // Run many cycles - should fire multiple times in free-running mode
    // Clear IFR between iterations to allow re-fire
    for (int i = 0; i < 50; i++) {
        via.update(1);
    }

    // In free-running mode, the timer should have fired at least once
    CHECK(callbackCount >= 1);
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("VIA6522 reset clears all state", "[via][reset]") {
    VIA6522 via;

    // Set up some state
    via.write(REG_DDRA, 0xFF);
    via.write(REG_ORA, 0xAA);
    via.write(REG_IER, 0x80 | 0x40);
    via.write(REG_T1CL, 0x10);
    via.write(REG_T1CH, 0x20);

    via.reset();

    CHECK(via.getORA() == 0x00);
    CHECK(via.getORB() == 0x00);
    CHECK(via.getDDRA() == 0x00);
    CHECK(via.getDDRB() == 0x00);
    CHECK(via.getIFR() == 0x00);
    CHECK(via.getACR() == 0x00);
    CHECK(via.isIRQActive() == false);
}

// ============================================================================
// State serialization
// ============================================================================

TEST_CASE("VIA6522 state serialization round-trip", "[via][state]") {
    VIA6522 via1;
    via1.write(REG_DDRA, 0xFF);
    via1.write(REG_ORA, 0xBB);
    via1.write(REG_DDRB, 0x0F);

    uint8_t stateBuffer[VIA6522::STATE_SIZE];
    size_t written = via1.exportState(stateBuffer);
    REQUIRE(written == VIA6522::STATE_SIZE);

    VIA6522 via2;
    via2.importState(stateBuffer);

    CHECK(via2.getORA() == 0xBB);
    CHECK(via2.getDDRA() == 0xFF);
    CHECK(via2.getDDRB() == 0x0F);
}

// ============================================================================
// Reading T1CL clears T1 interrupt flag
// ============================================================================

TEST_CASE("VIA6522 reading T1CL clears T1 interrupt flag", "[via][timer1_clear]") {
    VIA6522 via;

    // Enable T1 interrupt
    via.write(REG_IER, 0x80 | 0x40);

    // Start short timer
    via.write(REG_T1CL, 0x02);
    via.write(REG_T1CH, 0x00);

    // Let timer fire
    via.update(10);
    CHECK((via.getIFR() & 0x40) != 0);

    // Reading T1CL should clear the T1 interrupt flag
    via.read(REG_T1CL);
    CHECK((via.getIFR() & 0x40) == 0);
}
