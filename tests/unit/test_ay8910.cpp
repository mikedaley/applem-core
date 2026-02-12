/*
 * test_ay8910.cpp - Unit tests for AY-3-8910 sound chip emulation
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ay8910.hpp"

#include <vector>
#include <cmath>

using namespace a2e;

// Helper: write a value to a register
static void writeReg(AY8910& psg, uint8_t reg, uint8_t value) {
    psg.setRegisterAddress(reg);
    psg.writeRegister(value);
}

// Helper: read a register value
static uint8_t readReg(AY8910& psg, uint8_t reg) {
    psg.setRegisterAddress(reg);
    return psg.readRegister();
}

// ============================================================================
// Constructor
// ============================================================================

TEST_CASE("AY8910 constructor creates valid instance", "[ay8910][ctor]") {
    AY8910 psg;
    // Registers should be initialized to 0, except register 7 (mixer)
    // which defaults to 0x3F (all tone and noise channels disabled for silence)
    for (int r = 0; r < 16; r++) {
        if (r == 7) {
            CHECK(psg.getRegister(r) == 0x3F);
        } else {
            CHECK(psg.getRegister(r) == 0);
        }
    }
}

// ============================================================================
// Register access cycle
// ============================================================================

TEST_CASE("setRegisterAddress/writeRegister/readRegister cycle", "[ay8910][reg]") {
    AY8910 psg;

    // Write to register 0 (Tone A fine)
    psg.setRegisterAddress(0);
    psg.writeRegister(0x5A);

    // Read back
    psg.setRegisterAddress(0);
    uint8_t val = psg.readRegister();
    CHECK(val == 0x5A);
}

// ============================================================================
// Tone period registers (0-5)
// ============================================================================

TEST_CASE("Tone period registers write and read back", "[ay8910][tone]") {
    AY8910 psg;

    SECTION("Channel A tone period") {
        writeReg(psg, 0, 0xAB); // Fine
        writeReg(psg, 1, 0x0C); // Coarse (4-bit)
        CHECK(readReg(psg, 0) == 0xAB);
        CHECK(readReg(psg, 1) == 0x0C); // Only low 4 bits
    }

    SECTION("Channel B tone period") {
        writeReg(psg, 2, 0x34);
        writeReg(psg, 3, 0x05);
        CHECK(readReg(psg, 2) == 0x34);
        CHECK(readReg(psg, 3) == 0x05);
    }

    SECTION("Channel C tone period") {
        writeReg(psg, 4, 0xFF);
        writeReg(psg, 5, 0x0F);
        CHECK(readReg(psg, 4) == 0xFF);
        CHECK(readReg(psg, 5) == 0x0F);
    }
}

// ============================================================================
// Noise register (6) - 5 bit
// ============================================================================

TEST_CASE("Noise period register is 5-bit", "[ay8910][noise]") {
    AY8910 psg;
    writeReg(psg, 6, 0xFF);
    // Only lower 5 bits should be stored
    CHECK(readReg(psg, 6) == 0x1F);
}

TEST_CASE("Noise register write and read back", "[ay8910][noise]") {
    AY8910 psg;
    writeReg(psg, 6, 0x0A);
    CHECK(readReg(psg, 6) == 0x0A);
}

// ============================================================================
// Mixer register (7)
// ============================================================================

TEST_CASE("Mixer register controls tone/noise per channel", "[ay8910][mixer]") {
    AY8910 psg;
    // Bit layout: B7=IOB B6=IOA B5=NoiseC B4=NoiseB B3=NoiseA B2=ToneC B1=ToneB B0=ToneA
    // 1 = disabled, 0 = enabled
    writeReg(psg, 7, 0x38); // Disable all noise, enable all tone
    CHECK(readReg(psg, 7) == 0x38);

    writeReg(psg, 7, 0x3F); // Disable everything
    CHECK(readReg(psg, 7) == 0x3F);

    writeReg(psg, 7, 0x00); // Enable everything
    CHECK(readReg(psg, 7) == 0x00);
}

// ============================================================================
// Volume registers (8-10)
// ============================================================================

TEST_CASE("Volume registers write and read back", "[ay8910][volume]") {
    AY8910 psg;

    SECTION("Channel A volume") {
        writeReg(psg, 8, 0x0F); // Max volume, no envelope
        CHECK(readReg(psg, 8) == 0x0F);
    }

    SECTION("Channel B volume with envelope mode") {
        writeReg(psg, 9, 0x10); // Bit 4 = use envelope
        CHECK(readReg(psg, 9) == 0x10);
    }

    SECTION("Channel C volume") {
        writeReg(psg, 10, 0x0A);
        CHECK(readReg(psg, 10) == 0x0A);
    }
}

// ============================================================================
// Envelope registers (11-13)
// ============================================================================

TEST_CASE("Envelope period registers", "[ay8910][envelope]") {
    AY8910 psg;

    writeReg(psg, 11, 0xCD); // Envelope fine
    writeReg(psg, 12, 0xAB); // Envelope coarse
    CHECK(readReg(psg, 11) == 0xCD);
    CHECK(readReg(psg, 12) == 0xAB);
}

TEST_CASE("Envelope shape register", "[ay8910][envelope]") {
    AY8910 psg;

    // 4-bit shape control
    writeReg(psg, 13, 0x0E);
    CHECK(readReg(psg, 13) == 0x0E);
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("reset clears all registers", "[ay8910][reset]") {
    AY8910 psg;

    // Set various registers
    writeReg(psg, 0, 0xFF);
    writeReg(psg, 7, 0x00);
    writeReg(psg, 8, 0x0F);
    writeReg(psg, 13, 0x0E);

    psg.reset();

    for (int r = 0; r < 16; r++) {
        INFO("Register " << r);
        if (r == 7) {
            // Mixer register resets to 0x3F (all channels disabled for silence)
            CHECK(psg.getRegister(r) == 0x3F);
        } else {
            CHECK(psg.getRegister(r) == 0);
        }
    }
}

// ============================================================================
// Channel muting
// ============================================================================

TEST_CASE("Channel muting defaults to unmuted", "[ay8910][mute]") {
    AY8910 psg;
    CHECK(psg.isChannelMuted(0) == false);
    CHECK(psg.isChannelMuted(1) == false);
    CHECK(psg.isChannelMuted(2) == false);
}

TEST_CASE("setChannelMute/isChannelMuted round-trips", "[ay8910][mute]") {
    AY8910 psg;

    psg.setChannelMute(0, true);
    CHECK(psg.isChannelMuted(0) == true);
    CHECK(psg.isChannelMuted(1) == false);
    CHECK(psg.isChannelMuted(2) == false);

    psg.setChannelMute(1, true);
    CHECK(psg.isChannelMuted(1) == true);

    psg.setChannelMute(0, false);
    CHECK(psg.isChannelMuted(0) == false);
}

// ============================================================================
// generateSamples
// ============================================================================

TEST_CASE("generateSamples produces output", "[ay8910][generate]") {
    AY8910 psg;

    // Enable tone on channel A, set frequency and volume
    writeReg(psg, 0, 0x10); // Tone A fine = low period for audible freq
    writeReg(psg, 1, 0x00); // Tone A coarse
    writeReg(psg, 7, 0x3E); // Enable tone A only (disable noise all, tone B+C)
    writeReg(psg, 8, 0x0F); // Channel A volume max

    const int count = 256;
    std::vector<float> buffer(count, 0.0f);
    psg.generateSamples(buffer.data(), count, 48000);

    // With a short tone period and max volume, there should be non-zero output
    float maxAbs = 0.0f;
    for (int i = 0; i < count; i++) {
        float absVal = std::fabs(buffer[i]);
        if (absVal > maxAbs) maxAbs = absVal;
    }
    CHECK(maxAbs > 0.0f);
}

TEST_CASE("generateSamples all channels muted produces silence", "[ay8910][generate]") {
    AY8910 psg;

    // Set up tone but mute all channels
    writeReg(psg, 0, 0x10);
    writeReg(psg, 7, 0x3E);
    writeReg(psg, 8, 0x0F);

    psg.setChannelMute(0, true);
    psg.setChannelMute(1, true);
    psg.setChannelMute(2, true);

    const int count = 128;
    std::vector<float> buffer(count, 1.0f);
    psg.generateSamples(buffer.data(), count, 48000);

    for (int i = 0; i < count; i++) {
        CHECK(buffer[i] == Approx(0.0f));
    }
}

// ============================================================================
// Debug counters
// ============================================================================

TEST_CASE("Write count tracks register writes", "[ay8910][debug]") {
    AY8910 psg;
    CHECK(psg.getWriteCount() == 0);

    writeReg(psg, 0, 0x42);
    CHECK(psg.getWriteCount() == 1);

    writeReg(psg, 1, 0x03);
    CHECK(psg.getWriteCount() == 2);
}

TEST_CASE("Last write tracking", "[ay8910][debug]") {
    AY8910 psg;

    writeReg(psg, 5, 0xAB);
    CHECK(psg.getLastWriteReg() == 5);
    CHECK(psg.getLastWriteVal() == 0xAB);
}

// ============================================================================
// State serialization size
// ============================================================================

TEST_CASE("State serialization exports and imports", "[ay8910][state]") {
    AY8910 psg1;
    writeReg(psg1, 0, 0x55);
    writeReg(psg1, 7, 0x38);
    writeReg(psg1, 8, 0x0F);

    uint8_t stateBuffer[AY8910::STATE_SIZE];
    size_t written = psg1.exportState(stateBuffer);
    REQUIRE(written == AY8910::STATE_SIZE);

    AY8910 psg2;
    psg2.importState(stateBuffer);

    CHECK(psg2.getRegister(0) == 0x55);
    CHECK(psg2.getRegister(7) == 0x38);
    CHECK(psg2.getRegister(8) == 0x0F);
}
