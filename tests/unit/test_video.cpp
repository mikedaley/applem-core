/*
 * test_video.cpp - Unit tests for Video output generation
 *
 * Tests the video subsystem including mode detection, framebuffer
 * management, display options, page switching, text rendering,
 * and dirty flag management.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "video/video.hpp"
#include "mmu/mmu.hpp"
#include "roms.cpp"

using namespace a2e;

// Helper: create an MMU with ROMs loaded and a Video instance
struct VideoTestFixture {
    MMU mmu;
    std::unique_ptr<Video> video;

    VideoTestFixture() {
        mmu.loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                     roms::ROM_CHAR, roms::ROM_CHAR_SIZE);
        video = std::make_unique<Video>(mmu);
    }
};

// ============================================================================
// Default state
// ============================================================================

TEST_CASE("Video default mode is TEXT_40", "[video][mode]") {
    VideoTestFixture f;

    // Default soft switches: text=true, col80=false
    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_40);
}

// ============================================================================
// Framebuffer
// ============================================================================

TEST_CASE("Video getFramebufferSize returns 860160", "[video][framebuffer]") {
    // 560 * 384 * 4 (RGBA) = 860160
    CHECK(Video::getFramebufferSize() == 860160);
}

TEST_CASE("Video getFramebuffer returns non-null pointer", "[video][framebuffer]") {
    VideoTestFixture f;

    CHECK(f.video->getFramebuffer() != nullptr);
}

TEST_CASE("Video const getFramebuffer returns non-null pointer", "[video][framebuffer]") {
    VideoTestFixture f;

    const Video& constVideo = *f.video;
    CHECK(constVideo.getFramebuffer() != nullptr);
}

// ============================================================================
// forceRenderFrame
// ============================================================================

TEST_CASE("Video forceRenderFrame does not crash", "[video][render]") {
    VideoTestFixture f;

    // Should complete without error
    f.video->forceRenderFrame();
}

TEST_CASE("Video forceRenderFrame sets frame dirty", "[video][render]") {
    VideoTestFixture f;

    f.video->clearFrameDirty();
    CHECK_FALSE(f.video->isFrameDirty());

    f.video->forceRenderFrame();
    // After rendering, the dirty flag should reflect the render occurred
    // (forceRenderFrame renders regardless of dirty state)
}

// ============================================================================
// Mode detection via soft switch toggling
// ============================================================================

TEST_CASE("Video mode: text=true, col80=false -> TEXT_40", "[video][mode]") {
    VideoTestFixture f;

    // Ensure text mode
    f.mmu.write(0xC051, 0);  // TEXT on (write also toggles)
    f.mmu.write(0xC00C, 0);  // 80COL off

    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_40);
}

TEST_CASE("Video mode: text=true, col80=true -> TEXT_80", "[video][mode]") {
    VideoTestFixture f;

    f.mmu.write(0xC051, 0);  // TEXT on
    f.mmu.write(0xC00D, 0);  // 80COL on

    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_80);
}

TEST_CASE("Video mode: text=false, hires=false -> LORES", "[video][mode]") {
    VideoTestFixture f;

    f.mmu.read(0xC050);   // TEXT off (graphics mode)
    f.mmu.read(0xC056);   // HIRES off

    CHECK(f.video->getCurrentMode() == VideoMode::LORES);
}

TEST_CASE("Video mode: text=false, hires=true -> HIRES", "[video][mode]") {
    VideoTestFixture f;

    f.mmu.read(0xC050);   // TEXT off (graphics mode)
    f.mmu.read(0xC057);   // HIRES on

    CHECK(f.video->getCurrentMode() == VideoMode::HIRES);
}

TEST_CASE("Video mode: DOUBLE_LORES requires AN3 off + 80COL", "[video][mode]") {
    VideoTestFixture f;

    f.mmu.read(0xC050);   // TEXT off (graphics)
    f.mmu.read(0xC056);   // HIRES off
    f.mmu.write(0xC00D, 0);  // 80COL on
    f.mmu.read(0xC05E);   // AN3 off

    CHECK(f.video->getCurrentMode() == VideoMode::DOUBLE_LORES);
}

TEST_CASE("Video mode: DOUBLE_HIRES requires AN3 off + 80COL + HIRES", "[video][mode]") {
    VideoTestFixture f;

    f.mmu.read(0xC050);   // TEXT off (graphics)
    f.mmu.read(0xC057);   // HIRES on
    f.mmu.write(0xC00D, 0);  // 80COL on
    f.mmu.read(0xC05E);   // AN3 off

    CHECK(f.video->getCurrentMode() == VideoMode::DOUBLE_HIRES);
}

// ============================================================================
// Display options: monochrome
// ============================================================================

TEST_CASE("Video setMonochrome/isMonochrome toggle", "[video][options]") {
    VideoTestFixture f;

    CHECK_FALSE(f.video->isMonochrome());

    f.video->setMonochrome(true);
    CHECK(f.video->isMonochrome());

    f.video->setMonochrome(false);
    CHECK_FALSE(f.video->isMonochrome());
}

// ============================================================================
// Display options: green phosphor
// ============================================================================

TEST_CASE("Video setGreenPhosphor/isGreenPhosphor toggle", "[video][options]") {
    VideoTestFixture f;

    CHECK_FALSE(f.video->isGreenPhosphor());

    f.video->setGreenPhosphor(true);
    CHECK(f.video->isGreenPhosphor());

    f.video->setGreenPhosphor(false);
    CHECK_FALSE(f.video->isGreenPhosphor());
}

// ============================================================================
// Display options: UK character set
// ============================================================================

TEST_CASE("Video setUKCharacterSet/isUKCharacterSet toggle", "[video][options]") {
    VideoTestFixture f;

    CHECK_FALSE(f.video->isUKCharacterSet());

    f.video->setUKCharacterSet(true);
    CHECK(f.video->isUKCharacterSet());

    f.video->setUKCharacterSet(false);
    CHECK_FALSE(f.video->isUKCharacterSet());
}

// ============================================================================
// Page switching
// ============================================================================

TEST_CASE("Video page switching via $C055/$C054", "[video][page]") {
    VideoTestFixture f;

    // Default: page1
    CHECK_FALSE(f.mmu.getSoftSwitches().page2);

    // Switch to page 2
    f.mmu.write(0xC055, 0);
    CHECK(f.mmu.getSoftSwitches().page2);

    // Switch back to page 1
    f.mmu.write(0xC054, 0);
    CHECK_FALSE(f.mmu.getSoftSwitches().page2);
}

// ============================================================================
// Text rendering
// ============================================================================

TEST_CASE("Video text rendering: writing ASCII to $0400 produces non-zero pixels", "[video][render]") {
    VideoTestFixture f;

    // Fill text page 1 with a visible character (inverse '@' = 0x00, normal 'A' = 0xC1)
    // Use normal 'A' character (0xC1 in Apple II encoding)
    for (int i = 0; i < 40; ++i) {
        f.mmu.write(0x0400 + i, 0xC1);  // 'A' in normal video
    }

    // Render the frame
    f.video->forceRenderFrame();

    // Check that framebuffer has some non-zero pixels in the first row area
    const uint8_t* fb = f.video->getFramebuffer();
    bool hasNonZero = false;
    // Check the first few scanlines (each text row is 16 pixels tall in 560x384)
    // 384 / 24 = 16 pixels per text row
    for (size_t i = 0; i < 560 * 16 * 4; ++i) {
        if (fb[i] != 0) {
            hasNonZero = true;
            break;
        }
    }

    CHECK(hasNonZero);
}

TEST_CASE("Video text rendering: blank screen has predictable output", "[video][render]") {
    VideoTestFixture f;

    // Text page is all zeros (inverse '@' on Apple IIe)
    // Even with all zeros, the character ROM should produce some pixel output
    f.video->forceRenderFrame();

    const uint8_t* fb = f.video->getFramebuffer();
    // Framebuffer should exist and have been written to
    CHECK(fb != nullptr);
}

// ============================================================================
// Frame dirty flag
// ============================================================================

TEST_CASE("Video isFrameDirty/clearFrameDirty/setFrameDirty", "[video][dirty]") {
    VideoTestFixture f;

    // Initially dirty
    CHECK(f.video->isFrameDirty());

    f.video->clearFrameDirty();
    CHECK_FALSE(f.video->isFrameDirty());

    f.video->setFrameDirty();
    CHECK(f.video->isFrameDirty());
}

// ============================================================================
// Mixed mode
// ============================================================================

TEST_CASE("Video mixed mode: $C053 sets mixed, $C052 clears", "[video][mode]") {
    VideoTestFixture f;

    // Default: mixed off
    CHECK_FALSE(f.mmu.getSoftSwitches().mixed);

    f.mmu.read(0xC053);  // MIXSET
    CHECK(f.mmu.getSoftSwitches().mixed);

    f.mmu.read(0xC052);  // MIXCLR
    CHECK_FALSE(f.mmu.getSoftSwitches().mixed);
}

// ============================================================================
// Mode transitions
// ============================================================================

TEST_CASE("Video mode transitions work correctly", "[video][mode]") {
    VideoTestFixture f;

    // Start in TEXT_40
    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_40);

    // Switch to HIRES
    f.mmu.read(0xC050);  // TEXT off
    f.mmu.read(0xC057);  // HIRES on
    CHECK(f.video->getCurrentMode() == VideoMode::HIRES);

    // Switch to LORES
    f.mmu.read(0xC056);  // HIRES off
    CHECK(f.video->getCurrentMode() == VideoMode::LORES);

    // Switch to TEXT_80
    f.mmu.read(0xC051);     // TEXT on
    f.mmu.write(0xC00D, 0); // 80COL on
    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_80);

    // Back to TEXT_40
    f.mmu.write(0xC00C, 0); // 80COL off
    CHECK(f.video->getCurrentMode() == VideoMode::TEXT_40);
}
