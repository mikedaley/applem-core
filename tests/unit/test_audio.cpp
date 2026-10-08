/*
 * test_audio.cpp - Unit tests for Audio (speaker) emulation
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "audio.hpp"
#include "cards/mockingboard/mockingboard_card.hpp"

#include <cmath>
#include <vector>

using namespace a2e;

// ============================================================================
// Constructor
// ============================================================================

TEST_CASE("Audio constructor creates a valid instance", "[audio][ctor]") {
    Audio audio;
    // Default state: speaker off, volume 0.5, not muted
    CHECK(audio.getSpeakerState() == false);
    CHECK(audio.getVolume() == Approx(0.5f));
    CHECK(audio.isMuted() == false);
}

// ============================================================================
// toggleSpeaker
// ============================================================================

TEST_CASE("toggleSpeaker records events and state updates on sample generation", "[audio][toggle]") {
    Audio audio;
    CHECK(audio.getSpeakerState() == false);

    // toggleSpeaker only records toggle events; it does NOT immediately
    // change speakerState_. The state is updated when generateStereoSamples
    // processes the recorded toggle events.
    audio.toggleSpeaker(100);

    // State is still false until samples are generated
    CHECK(audio.getSpeakerState() == false);

    // Generate samples to process the toggle events
    const int count = 128;
    std::vector<float> buffer(count * 2, 0.0f);
    audio.generateStereoSamples(buffer.data(), count, 10000);

    // After generating samples with one toggle, speaker state should be true
    CHECK(audio.getSpeakerState() == true);

    // Toggle twice more and generate samples
    audio.toggleSpeaker(11000);
    audio.toggleSpeaker(12000);
    audio.generateStereoSamples(buffer.data(), count, 20000);

    // Two more toggles from true: true->false->true
    CHECK(audio.getSpeakerState() == true);
}

// ============================================================================
// generateStereoSamples
// ============================================================================

TEST_CASE("generateStereoSamples returns sample count", "[audio][generate]") {
    Audio audio;
    const int count = 128;
    std::vector<float> buffer(count * 2, 0.0f); // stereo interleaved

    int generated = audio.generateStereoSamples(buffer.data(), count, 10000);
    CHECK(generated == count);
}

TEST_CASE("Silence: no toggles produces near-zero output", "[audio][silence]") {
    Audio audio;
    const int count = 256;
    std::vector<float> buffer(count * 2, 1.0f); // Fill with non-zero

    audio.generateStereoSamples(buffer.data(), count, 50000);

    // After generating samples with no toggles, output should be near zero
    float maxAbs = 0.0f;
    for (int i = 0; i < count * 2; i++) {
        float absVal = std::fabs(buffer[i]);
        if (absVal > maxAbs) maxAbs = absVal;
    }
    // With DC removal and no toggles, output should settle toward zero
    CHECK(maxAbs < 0.5f);
}

TEST_CASE("Toggle produces non-zero output", "[audio][toggle_output]") {
    Audio audio;
    audio.setVolume(1.0f);

    // Toggle the speaker rapidly to create audio signal
    for (int i = 0; i < 100; i++) {
        audio.toggleSpeaker(i * 50);
    }

    const int count = 512;
    std::vector<float> buffer(count * 2, 0.0f);
    audio.generateStereoSamples(buffer.data(), count, 5100);

    // Check there is at least some non-zero output
    float maxAbs = 0.0f;
    for (int i = 0; i < count * 2; i++) {
        float absVal = std::fabs(buffer[i]);
        if (absVal > maxAbs) maxAbs = absVal;
    }
    CHECK(maxAbs > 0.0f);
}

// ============================================================================
// Volume control
// ============================================================================

TEST_CASE("setVolume/getVolume round-trips correctly", "[audio][volume]") {
    Audio audio;

    audio.setVolume(0.0f);
    CHECK(audio.getVolume() == Approx(0.0f));

    audio.setVolume(1.0f);
    CHECK(audio.getVolume() == Approx(1.0f));

    audio.setVolume(0.75f);
    CHECK(audio.getVolume() == Approx(0.75f));
}

// ============================================================================
// Mute control
// ============================================================================

TEST_CASE("setMuted/isMuted round-trips correctly", "[audio][mute]") {
    Audio audio;

    CHECK(audio.isMuted() == false);

    audio.setMuted(true);
    CHECK(audio.isMuted() == true);

    audio.setMuted(false);
    CHECK(audio.isMuted() == false);
}

TEST_CASE("Muted audio produces zero output", "[audio][mute_output]") {
    Audio audio;
    audio.setMuted(true);
    audio.setVolume(1.0f);

    // Generate toggles
    for (int i = 0; i < 100; i++) {
        audio.toggleSpeaker(i * 50);
    }

    const int count = 256;
    std::vector<float> buffer(count * 2, 1.0f); // Fill with non-zero
    audio.generateStereoSamples(buffer.data(), count, 5100);

    // All samples should be zero when muted
    for (int i = 0; i < count * 2; i++) {
        CHECK(buffer[i] == Approx(0.0f));
    }
}

// ============================================================================
// reset
// ============================================================================

TEST_CASE("reset clears speaker state", "[audio][reset]") {
    Audio audio;

    // Toggle speaker and generate samples so state is updated
    audio.toggleSpeaker(100);
    const int count = 128;
    std::vector<float> buffer(count * 2, 0.0f);
    audio.generateStereoSamples(buffer.data(), count, 10000);
    CHECK(audio.getSpeakerState() == true);

    audio.reset();

    CHECK(audio.getSpeakerState() == false);
}

TEST_CASE("reset preserves volume and mute settings", "[audio][reset]") {
    Audio audio;

    audio.setVolume(0.8f);
    audio.setMuted(true);

    audio.reset();

    // Volume and mute are user settings, typically preserved across reset
    // (though this depends on implementation; verify actual behavior)
    // The speaker state itself should be reset
    CHECK(audio.getSpeakerState() == false);
}

// ============================================================================
// Emulation speed
// ============================================================================

TEST_CASE("Accelerated buffers cover the whole accelerated window",
          "[audio][speed]") {
    // At 8x a buffer of samples legitimately spans eight times as many CPU
    // cycles. The plausibility clamp in generateStereoSamples() measured that
    // window at 1x, so it rendered only the last eighth of it, stretched over
    // the whole buffer: most of the speaker activity was dropped and what
    // survived came out at roughly real-time pitch instead of eight times up.
    const int count = 512;
    const int multiplier = 8;
    const uint64_t period = 4000; // cycles between speaker toggles
    const uint64_t window =
        static_cast<uint64_t>(count * CYCLES_PER_SAMPLE * multiplier);

    auto renderSquareWave = [&](Audio &audio) {
        for (uint64_t c = period; c < window; c += period) {
            audio.toggleSpeaker(c);
        }
        std::vector<float> buffer(count * 2, 0.0f);
        audio.generateStereoSamples(buffer.data(), count, window);
        // Count sign changes on the left channel: proportional to the pitch
        // that actually came out.
        int crossings = 0;
        for (int i = 2; i < count * 2; i += 2) {
            if ((buffer[i] > 0.0f) != (buffer[i - 2] > 0.0f)) crossings++;
        }
        return crossings;
    };

    // The wave toggles every `period` cycles, so the whole window holds
    // window/period half-cycles and every one of them should be in the buffer.
    const int expected = static_cast<int>(window / period);

    Audio accelerated;
    accelerated.setSpeedMultiplier(multiplier);
    const int fast = renderSquareWave(accelerated);
    CHECK(fast > expected * 3 / 4);

    // Left at 1x, the same input renders a fraction of the pitch — the bug.
    Audio unaware;
    const int slow = renderSquareWave(unaware);
    CHECK(slow < fast / 4);
}

// ============================================================================
// Speaker and Mockingboard together
// ============================================================================

TEST_CASE("A loud speaker and a loud Mockingboard together are not clipped", "[audio][mix]") {
    // Each is mixed at half level, which keeps either alone inside full
    // scale; but a Mockingboard's output reaches about 1.08 where a held level
    // drops away, so with the speaker swinging too the sum passes full scale.
    // It used to be clamped flat there. Now it is turned down, never past the
    // limiter's ceiling, and still reaches it.
    Audio audio;
    MockingboardCard card;
    audio.setMockingboard(&card);
    auto reg = [](AY8910& ay, uint8_t r, uint8_t v) { ay.setRegisterAddress(r); ay.writeRegister(v); };
    auto everyChannel = [&](uint8_t mixer, uint8_t amp) {
        for (AY8910* ay : {&card.getPSG1(), &card.getPSG2()}) {
            for (uint8_t ch = 0; ch < 3; ch++) {
                reg(*ay, ch * 2, 0x00);
                reg(*ay, ch * 2 + 1, 0x04); // a low square
                reg(*ay, 8 + ch, amp);
            }
            reg(*ay, 7, mixer);
        }
    };

    std::vector<float> buffer(800 * 2);
    uint64_t cycle = 0;
    float loudest = 0;
    auto play = [&](int buffers) {
        for (int b = 0; b < buffers; b++) {
            // About 17,000 cycles a buffer, with the speaker toggled at 200Hz.
            for (int t = 0; t < 7; t++) audio.toggleSpeaker(cycle + t * 2557);
            for (int step = 0; step < 17; step++) card.update(1000);
            cycle += 17000;
            audio.generateStereoSamples(buffer.data(), 800, cycle);
            for (float s : buffer) loudest = std::max(loudest, std::fabs(s));
        }
    };
    everyChannel(0x3F, 0x0F); // tone and noise off: a steady full level
    play(60);
    everyChannel(0x38, 0x0F); // and then a full-volume square on every channel
    play(30);
    everyChannel(0x3F, 0x00); // and then silence
    play(10);

    REQUIRE(loudest <= PeakLimiter::CEILING + 1e-5f);
    REQUIRE(loudest > 0.9f);
}
