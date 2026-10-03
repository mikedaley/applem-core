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
 * - Audio sample generation and the PSGs' clock
 * - Serialization round-trip
 * - IRQ generation from VIA timer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "mockingboard_card.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace a2e;

namespace {
// Phase lock and mono are on by default; a test about the chips themselves
// turns them off for its own length and leaves them as it found them.
struct PhaseLock {
    bool was = MockingboardCard::phaseLock();
    explicit PhaseLock(bool on) { MockingboardCard::setPhaseLock(on); }
    ~PhaseLock() { MockingboardCard::setPhaseLock(was); }
};
struct Mono {
    bool was = MockingboardCard::mono();
    explicit Mono(bool on) { MockingboardCard::setMono(on); }
    ~Mono() { MockingboardCard::setMono(was); }
};
} // namespace

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

TEST_CASE("MockingboardCard sample production tracks emulation speed",
          "[mockingboard][speed]") {
    // Incremental generation emits one frame per CYCLES_PER_SAMPLE of emulated
    // time. Accelerated, the machine covers that time faster, so unless the
    // interval is stretched the card produces `multiplier` frames for every
    // one the mixer takes — an unbounded backlog, heard as music at normal
    // pitch drifting ever further behind the machine.
    constexpr double CYCLES_PER_SAMPLE = 1023000.0 / 48000.0;
    const int frames = 800; // one 60Hz buffer at 48kHz

    auto queuedAfterOneBuffer = [&](int multiplier) {
        MockingboardCard card;
        card.setEnabled(true);
        card.setSpeedMultiplier(multiplier);
        // Cycles the emulator runs to fill one buffer at this speed.
        card.update(static_cast<int>(frames * CYCLES_PER_SAMPLE * multiplier));
        return static_cast<int>(card.getQueuedSampleFrames());
    };

    for (int multiplier : {1, 2, 4, 8}) {
        INFO("multiplier " << multiplier);
        CHECK(queuedAfterOneBuffer(multiplier) == Approx(frames).margin(2));
    }
}

TEST_CASE("MockingboardCard muting a PSG 1 channel leaves PSG 2 playing",
          "[mockingboard][mute]") {
    // The mutes are the debugger's, per chip: two chips playing the same
    // notes are still two chips, and muting one must not silence the other.
    Mono stereo(false);
    MockingboardCard card;
    card.setEnabled(true);
    auto writePSG = [&](uint8_t via, uint8_t reg, uint8_t value) {
        card.writeROM(via | VIA_ORA, reg);
        card.writeROM(via | VIA_ORB, 0x07); // LATCH
        card.writeROM(via | VIA_ORB, 0x04);
        card.writeROM(via | VIA_ORA, value);
        card.writeROM(via | VIA_ORB, 0x06); // WRITE
        card.writeROM(via | VIA_ORB, 0x04);
    };
    for (uint8_t via : {uint8_t(0x00), uint8_t(0x80)}) {
        card.writeROM(via | VIA_DDRA, 0xFF);
        card.writeROM(via | VIA_DDRB, 0x07);
        writePSG(via, 0, 244);  // channel A, about 262Hz
        writePSG(via, 1, 0);
        writePSG(via, 7, 0x3E); // tone A only
        writePSG(via, 8, 15);
    }
    REQUIRE(card.getPSG1().getRegister(0) == card.getPSG2().getRegister(0));

    card.getPSG1().setChannelMute(0, true);
    const int frames = 4800;
    card.update(static_cast<int>(frames * 1023000.0 / 48000.0));
    std::vector<float> out(frames * 2);
    card.consumeStereoSamples(out.data(), frames);

    // Past the DC filter's settling, the left is silent and the right plays.
    float left = 0.0f, right = 0.0f;
    for (int i = frames / 2; i < frames; i++) {
        left = std::max(left, std::abs(out[i * 2]));
        right = std::max(right, std::abs(out[i * 2 + 1]));
    }
    CHECK(left < 0.01f);
    CHECK(right > 0.1f);
}

TEST_CASE("MockingboardCard clocks its PSGs from the machine", "[mockingboard][pal]") {
    // The PSGs run off the slot's phi0, so a PAL machine's 1.0179MHz plays
    // every note about 9 cents lower than an NTSC machine's 1.023MHz.
    MockingboardCard card;
    CHECK(card.getPSG1().getClock() == Approx(1023000.0));
    card.setMachine(APPLE_IIE_PAL_PROFILE);
    CHECK(card.getPSG1().getClock() == Approx(PAL_CPU_CLOCK_HZ));
    CHECK(card.getPSG2().getClock() == Approx(PAL_CPU_CLOCK_HZ));
    card.setMachine(APPLE_IIE_PROFILE);
    CHECK(card.getPSG1().getClock() == Approx(1023000.0));
}

TEST_CASE("MockingboardCard plays each PSG on its own side", "[mockingboard]") {
    // Two chips given the same registers are two oscillators: with the phase
    // lock off, the card does not substitute one chip's output for the other's.
    PhaseLock off(false);
    Mono stereo(false);
    MockingboardCard card;
    auto writePSG = [&](uint8_t via, uint8_t reg, uint8_t value) {
        card.writeROM(via | VIA_ORA, reg);
        card.writeROM(via | VIA_ORB, 0x07);
        card.writeROM(via | VIA_ORB, 0x04);
        card.writeROM(via | VIA_ORA, value);
        card.writeROM(via | VIA_ORB, 0x06);
        card.writeROM(via | VIA_ORB, 0x04);
    };
    for (uint8_t via : {uint8_t(0x00), uint8_t(0x80)}) {
        card.writeROM(via | VIA_DDRA, 0xFF);
        card.writeROM(via | VIA_DDRB, 0x07);
        card.writeROM(via | VIA_ORB, 0x04);
    }
    // PSG 1 starts its tone; PSG 2 is given the same notes half a period later.
    writePSG(0x00, 0, 100);
    writePSG(0x00, 7, 0x3E);
    writePSG(0x00, 8, 15);
    card.update(800);  // 100 ticks of the chip's clock/8 is 800 cycles
    writePSG(0x80, 0, 100);
    writePSG(0x80, 7, 0x3E);
    writePSG(0x80, 8, 15);

    const int frames = 4800;
    card.update(static_cast<int>(frames * 1023000.0 / 48000.0));
    std::vector<float> out(frames * 2);
    card.consumeStereoSamples(out.data(), frames);

    // Out of step by half a period, the two sides differ.
    float diff = 0.0f;
    for (int i = frames / 2; i < frames; i++) {
        diff = std::max(diff, std::abs(out[i * 2] - out[i * 2 + 1]));
    }
    CHECK(diff > 0.1f);
}

namespace {

// A card on a clock the test owns, with both chips set up to play tone A at
// full volume and period 0 (toggling every tick, far above hearing).
struct ClockedCard {
    MockingboardCard card;
    uint64_t now = 1000;

    ClockedCard() {
        card.setCycleCallback([this]() { return now; });
        for (uint8_t via : {uint8_t(0x00), uint8_t(0x80)}) {
            card.writeROM(via | VIA_DDRA, 0xFF);
            card.writeROM(via | VIA_DDRB, 0x07);
            card.writeROM(via | VIA_ORB, 0x04);
            writePSG(via, 7, 0x3E);
            writePSG(via, 8, 15);
        }
    }
    void writePSG(uint8_t via, uint8_t reg, uint8_t value) {
        card.writeROM(via | VIA_ORA, reg);
        card.writeROM(via | VIA_ORB, 0x07);
        card.writeROM(via | VIA_ORB, 0x04);
        card.writeROM(via | VIA_ORA, value);
        card.writeROM(via | VIA_ORB, 0x06);
        card.writeROM(via | VIA_ORB, 0x04);
    }
    // Run the machine on, a few cycles at a time as instructions would.
    void run(int cycles) {
        while (cycles > 0) {
            const int step = std::min(cycles, 4);
            now += static_cast<uint64_t>(step);
            card.update(step);
            cycles -= step;
        }
    }
    // How alike the two sides are once the DC filter has settled: 1 in step,
    // -1 inverted.
    double correlation() {
        // A second: the filter's time constant is about 0.2s, and the last
        // quarter is measured.
        const int frames = 48000;
        run(static_cast<int>(frames * 1023000.0 / 48000.0));
        std::vector<float> out(frames * 2);
        card.consumeStereoSamples(out.data(), frames);
        double lr = 0, ll = 0, rr = 0;
        for (int i = frames * 3 / 4; i < frames; i++) {
            lr += out[i * 2] * out[i * 2 + 1];
            ll += out[i * 2] * out[i * 2];
            rr += out[i * 2 + 1] * out[i * 2 + 1];
        }
        return lr / std::sqrt(ll * rr);
    }
};

} // namespace

TEST_CASE("MockingboardCard applies a register write at the cycle it was made",
          "[mockingboard][timing]") {
    // A song that mirrors its notes to both chips writes the second a few
    // cycles after the first. With the period at 0 each chip toggles every
    // tick (8 cycles), so a chip given its new period one tick later has made
    // one toggle more and plays inverted; two ticks later, in step again.
    // Which happens must depend on the cycles between the writes and nothing
    // else: the chips used to catch up only at each 48kHz sample, so the
    // answer changed with where a sample boundary fell between the two.
    PhaseLock off(false);
    Mono stereo(false);
    for (int gap : {8, 16}) {
        for (int offset = 0; offset < 22; offset += 3) {
            INFO("gap " << gap << " cycles, written " << offset << " cycles in");
            ClockedCard c;
            c.run(offset);
            c.writePSG(0x00, 0, 200);
            c.run(gap);
            c.writePSG(0x80, 0, 200);
            if (gap == 8) CHECK(c.correlation() < -0.9);
            else CHECK(c.correlation() > 0.9);
        }
    }
}

TEST_CASE("MockingboardCard locks matched chips in phase by default", "[mockingboard]") {
    // The listener's preference, on unless turned off: two chips holding the
    // same registers play the left chip on both sides, so an inverted pair
    // cannot cancel.
    REQUIRE(MockingboardCard::phaseLock());
    Mono stereo(false);
    ClockedCard c;
    c.writePSG(0x00, 0, 200);
    c.run(8);
    c.writePSG(0x80, 0, 200);
    CHECK(c.correlation() == Approx(1.0));
    PhaseLock off(false);
    CHECK(c.correlation() < -0.9);
}

TEST_CASE("MockingboardCard plays both chips on both sides by default", "[mockingboard]") {
    // Mono, on unless turned off: the two chips mixed, half each, and the
    // mix on both sides. A tone on the left chip alone is heard on the right.
    REQUIRE(MockingboardCard::mono());
    ClockedCard c;
    c.writePSG(0x80, 8, 0);                // the right chip silent
    c.writePSG(0x00, 0, 200);
    CHECK(c.correlation() == Approx(1.0));
    // Off, the right chip is silent on its own side, once the DC filter has
    // let go of the mix it was playing.
    Mono off(false);
    const int frames = 48000;
    std::vector<float> out(frames * 2);
    c.run(static_cast<int>(frames * 1023000.0 / 48000.0));
    c.card.consumeStereoSamples(out.data(), frames);
    float left = 0.0f, right = 0.0f;
    for (int i = frames * 3 / 4; i < frames; i++) {
        left = std::max(left, std::abs(out[i * 2]));
        right = std::max(right, std::abs(out[i * 2 + 1]));
    }
    CHECK(left > 0.1f);
    CHECK(right < 0.01f);
}
