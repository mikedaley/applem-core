/*
 * test_ay8910.cpp - Unit tests for AY-3-8910 sound chip emulation
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ay8910.hpp"

#include <algorithm>
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
    // A chip out of reset has every register at zero, the mixer included
    for (int r = 0; r < 16; r++) {
        CHECK(psg.getRegister(r) == 0);
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

    // The RESET pin clears every register, the mixer included: every tone
    // and noise path is enabled, and the zeroed amplitudes keep it silent.
    for (int r = 0; r < 16; r++) {
        INFO("Register " << r);
        CHECK(psg.getRegister(r) == 0);
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

// ============================================================================
// Timing and output, measured against the datasheet
// ============================================================================

namespace {

constexpr double SAMPLES_PER_CLOCK = 48000.0 / 1023000.0;

// Rising edges of a channel's square wave in one second of output.
int toneFrequency(AY8910& psg) {
    int edges = 0;
    float prev = psg.generateSingleSample();
    for (int i = 0; i < 48000; i++) {
        float s = psg.generateSingleSample();
        if (s > 0.1667f && prev <= 0.1667f) edges++;
        prev = s;
    }
    return edges;
}

// Peak-to-peak output over a window, after the filter has settled.
float peakToPeak(AY8910& psg, int samples) {
    for (int i = 0; i < 256; i++) psg.generateSingleSample();
    float lo = 1.0f, hi = -1.0f;
    for (int i = 0; i < samples; i++) {
        float s = psg.generateSingleSample();
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    return hi - lo;
}

} // namespace

TEST_CASE("A tone's frequency is the clock over 16 times its period", "[ay8910][timing]") {
    // Datasheet: fT = fCLOCK / (16 * TP). TP = 100 at 1.023MHz is 639.4Hz.
    AY8910 psg;
    writeReg(psg, 0, 100);
    writeReg(psg, 7, 0x3E);
    writeReg(psg, 8, 15);
    CHECK(toneFrequency(psg) == Approx(1023000.0 / 1600.0).margin(1));
}

TEST_CASE("The chip runs at the clock it is given", "[ay8910][timing]") {
    // A Mockingboard's PSGs run off phi0, which on a PAL machine is
    // 14.25045MHz / 14: the same period plays lower.
    constexpr double PAL_CLOCK = 14250450.0 / 14.0;
    AY8910 psg;
    psg.setClock(PAL_CLOCK);
    writeReg(psg, 0, 100);
    writeReg(psg, 7, 0x3E);
    writeReg(psg, 8, 15);
    CHECK(toneFrequency(psg) == Approx(PAL_CLOCK / 1600.0).margin(1));
}

TEST_CASE("An envelope ramp lasts 256 clocks times its period", "[ay8910][envelope][timing]") {
    // Datasheet: fE = fCLOCK / (256 * EP), one ramp of the AY-3-8910's 16
    // steps. The YM2149 takes 32 steps over the same ramp, so both chips
    // must repeat at this rate.
    AY8910 psg;
    writeReg(psg, 7, 0x3F);  // tone and noise off: the output is the envelope
    writeReg(psg, 8, 0x10);  // channel A follows the envelope
    writeReg(psg, 11, 100);
    writeReg(psg, 12, 0);
    writeReg(psg, 13, 0x08); // repeating decay

    // Measure between two returns to the top of the ramp. The filter spreads
    // each jump over a sample or two, so one jump is counted once.
    float prev = psg.generateSingleSample();
    int starts = 0, first = 0, ramp = 0, last = -1000;
    for (int i = 1; i < 20000 && starts < 3; i++) {
        float s = psg.generateSingleSample();
        if (s > prev + 0.1f && i - last > 100) {
            if (starts == 1) first = i;
            if (starts == 2) ramp = i - first;
            starts++;
            last = i;
        }
        prev = s;
    }
    REQUIRE(starts == 3);
    CHECK(ramp == Approx(256.0 * 100 * SAMPLES_PER_CLOCK).margin(1));
}

TEST_CASE("A steady level passes the output filter unchanged", "[ay8910][output]") {
    // Sample playback writes the volume register with tone and noise off; the
    // resampler must not change the level it was given.
    AY8910 psg;
    writeReg(psg, 7, 0x3F);
    writeReg(psg, 8, 15);
    for (int i = 0; i < 256; i++) psg.generateSingleSample();
    CHECK(psg.generateSingleSample() == Approx(1.0f / 3.0f).margin(1e-4));
}

TEST_CASE("Tones above the host's Nyquist limit do not fold back", "[ay8910][output]") {
    // Period 1 is 63.9kHz and period 2 is 32kHz: inaudible on the real card.
    // A plain average of the ticks in each 48kHz sample folded them back as
    // audible tones (32kHz came out at 16kHz).
    for (int period : {1, 2}) {
        INFO("period " << period);
        AY8910 psg;
        writeReg(psg, 0, static_cast<uint8_t>(period));
        writeReg(psg, 7, 0x3E);
        writeReg(psg, 8, 15);
        CHECK(peakToPeak(psg, 4800) < 0.002f);
    }

    // An audible tone still comes through at full swing.
    AY8910 psg;
    writeReg(psg, 0, 100);
    writeReg(psg, 7, 0x3E);
    writeReg(psg, 8, 15);
    CHECK(peakToPeak(psg, 4800) > 0.3f);
}

TEST_CASE("The I/O ports read their pins when set to input", "[ay8910][reg]") {
    // A Mockingboard leaves IOA and IOB unconnected, so as inputs they float
    // high; as outputs a read returns what was written.
    AY8910 psg;
    writeReg(psg, 14, 0x12);
    writeReg(psg, 15, 0x34);
    CHECK(readReg(psg, 14) == 0xFF);
    CHECK(readReg(psg, 15) == 0xFF);

    writeReg(psg, 7, 0xC0);  // both ports output
    CHECK(readReg(psg, 14) == 0x12);
    CHECK(readReg(psg, 15) == 0x34);
}

// ============================================================================
// Noise, against the datasheet and the die
// ============================================================================

TEST_CASE("The noise register shifts every 16 NP clocks", "[ay8910][noise][timing]") {
    // Datasheet: the noise clock is the input clock over 16, then over NP.
    // NP = 31 shifts every 496 clocks, which at 48kHz is every 23.27
    // samples; from a reset the register holds 1, so the output starts high
    // and the first shift takes it low.
    AY8910 psg;
    writeReg(psg, 6, 31);
    writeReg(psg, 7, 0x37);  // noise on A only, tones off
    writeReg(psg, 8, 15);
    const int count = 48000;
    std::vector<float> out(count);
    psg.generateChannelSamples(out.data(), count, 48000, 0);

    // Every change of level is on a shift; the shifts are 496 clocks apart.
    constexpr double SAMPLES_PER_SHIFT = 496.0 * 48000.0 / 1023000.0;
    std::vector<int> changes;
    for (int i = 1; i < count; i++) if ((out[i] > 0.1f) != (out[i - 1] > 0.1f)) changes.push_back(i);
    REQUIRE(changes.size() > 1000);
    for (size_t i = 1; i < changes.size(); i++) {
        const double gap = changes[i] - changes[i - 1];
        const double shifts = gap / SAMPLES_PER_SHIFT;
        INFO("gap " << gap << " samples at change " << i);
        CHECK(std::fabs(shifts - std::round(shifts)) < 0.1);
    }

    // Reading the level once per shift gives the register's own sequence.
    uint32_t rng = 1;
    int wrong = 0;
    for (int shift = 0; shift < 2000; shift++) {
        const int at = static_cast<int>((shift + 0.5) * SAMPLES_PER_SHIFT);
        if ((out[at] > 0.1f) != ((rng & 1) != 0)) wrong++;
        const uint32_t feedback = (rng & 1) ^ ((rng >> 3) & 1);
        rng = (rng >> 1) | (feedback << 16);
    }
    CHECK(wrong == 0);
}

TEST_CASE("Noise period 0 is period 1", "[ay8910][noise]") {
    AY8910 zero, one;
    for (AY8910* psg : {&zero, &one}) {
        writeReg(*psg, 7, 0x37);
        writeReg(*psg, 8, 15);
    }
    writeReg(zero, 6, 0);
    writeReg(one, 6, 1);
    const int count = 4096;
    std::vector<float> a(count), b(count);
    zero.generateChannelSamples(a.data(), count, 48000, 0);
    one.generateChannelSamples(b.data(), count, 48000, 0);
    CHECK(a == b);
}

TEST_CASE("The noise register is maximal length", "[ay8910][noise]") {
    // 17 bits, the input bit 0 XOR bit 3: every state but zero before it repeats.
    uint32_t rng = 1;
    long states = 0;
    do {
        const uint32_t feedback = (rng & 1) ^ ((rng >> 3) & 1);
        rng = (rng >> 1) | (feedback << 16);
        states++;
    } while (rng != 1);
    CHECK(states == (1 << 17) - 1);
}

// ============================================================================
// The YM2149
// ============================================================================

namespace {

// The model is shared by every chip; each test puts the AY-3-8910 back.
struct ModelScope {
    explicit ModelScope(AY8910::Model model) { AY8910::setModel(model); }
    ~ModelScope() { AY8910::setModel(AY8910::Model::AY38910); }
};

// The steady output for one channel at a 5-bit step, as the mixer gives it.
float ymStep(int n) { return n == 0 ? 0.0f : static_cast<float>(std::pow(2.0, (n - 31) / 4.0)) / 3.0f; }

// What channels B and C add at fixed level 0. On a YM2149 that is step 1,
// about 45dB down: quiet, not silent.
float ymQuiet() { return 2.0f * ymStep(1); }

// The level in the middle of each of the first `steps` envelope steps of a
// single decay (shape 0) of period `ep`.
std::vector<float> envelopeSteps(int ep, int steps) {
    AY8910 psg;
    writeReg(psg, 7, 0x3F);
    writeReg(psg, 8, 0x10);
    writeReg(psg, 11, static_cast<uint8_t>(ep & 0xFF));
    writeReg(psg, 12, static_cast<uint8_t>(ep >> 8));
    writeReg(psg, 13, 0x00);
    std::vector<float> levels;
    psg.advance(ep / 2.0);
    for (int k = 0; k < steps; k++) {
        levels.push_back(psg.sampleNow());
        psg.advance(ep);
    }
    return levels;
}

} // namespace

TEST_CASE("A YM2149's envelope steps 32 times in a ramp", "[ay8910][ym2149][envelope]") {
    // Datasheet: "The envelope generator counts the envelope clock fEA 32
    // times for each envelope pattern cycle", a cycle of 256 EP clocks.
    ModelScope ym(AY8910::Model::YM2149);
    const std::vector<float> levels = envelopeSteps(1000, 32);
    for (int k = 0; k < 32; k++) {
        INFO("step " << k);
        CHECK(levels[k] == Approx(ymStep(31 - k) + ymQuiet()).margin(1e-4));
    }
}

TEST_CASE("An AY-3-8910's envelope holds each of its 16 levels for two steps", "[ay8910][envelope]") {
    // The same ramp on an AY-3-8910: the 32 steps' top four bits.
    const std::vector<float> levels = envelopeSteps(1000, 32);
    for (int k = 0; k < 32; k += 2) {
        INFO("step " << k);
        CHECK(levels[k] == Approx(levels[k + 1]).margin(1e-6));
        if (k > 0) CHECK(levels[k] < levels[k - 1]);
    }
    CHECK(levels[0] == Approx(1.0f / 3.0f).margin(1e-4));
}

TEST_CASE("A YM2149's fixed levels sit on every other step of its curve", "[ay8910][ym2149][volume]") {
    // Datasheet Fig. 1: fixed level L is labelled at step 2L + 1, 15 at the
    // top, each 3dB below the one above.
    ModelScope ym(AY8910::Model::YM2149);
    for (int level = 0; level < 16; level++) {
        INFO("level " << level);
        AY8910 psg;
        writeReg(psg, 7, 0x3F);
        writeReg(psg, 8, static_cast<uint8_t>(level));
        psg.advance(100);
        CHECK(psg.sampleNow() == Approx(ymStep(level * 2 + 1) + ymQuiet()).margin(1e-5));
    }
}

TEST_CASE("Choosing the AY-3-8910 again restores its levels", "[ay8910][ym2149][volume]") {
    { ModelScope ym(AY8910::Model::YM2149); }
    CHECK(AY8910::model() == AY8910::Model::AY38910);
    AY8910 psg;
    writeReg(psg, 7, 0x3F);
    writeReg(psg, 8, 14);
    psg.advance(100);
    CHECK(psg.sampleNow() == Approx(0.8482f / 3.0f).margin(1e-5));
}

TEST_CASE("A state saved with a 4-bit envelope loads at the same level", "[ay8910][state]") {
    AY8910 psg;
    writeReg(psg, 7, 0x3F);
    writeReg(psg, 8, 0x10);
    writeReg(psg, 11, 0x60);
    writeReg(psg, 12, 0xEA);  // EP 60000: no step while the test runs
    writeReg(psg, 13, 0x00);
    std::vector<uint8_t> state(AY8910::STATE_SIZE);
    psg.exportState(state.data());

    // The older form: level 7 of 16, falling, no envelope-form byte.
    state[31] = 7;
    state[44] = 0;
    state[45] = 0;
    AY8910 loaded;
    loaded.importState(state.data());
    loaded.advance(100);

    AY8910 fixed;
    writeReg(fixed, 7, 0x3F);
    writeReg(fixed, 8, 7);
    fixed.advance(100);
    CHECK(loaded.sampleNow() == Approx(fixed.sampleNow()).margin(1e-6));
}
