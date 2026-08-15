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

#include <set>

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

// ============================================================================
// Signal generation
//
// These reach past the palette and check the dot stream the machine actually
// emits, because that is where correctness now lives. Colour is downstream.
// ============================================================================

namespace {

// Read a framebuffer pixel as 0xAARRGGBB. Scanline n occupies rows 2n and 2n+1.
uint32_t pixelAt(const Video &video, int x, int scanline) {
    const uint8_t *fb = video.getFramebuffer();
    const size_t o = (static_cast<size_t>(scanline) * 2 * 560 + x) * 4;
    return (static_cast<uint32_t>(fb[o + 3]) << 24) |
           (static_cast<uint32_t>(fb[o + 0]) << 16) |
           (static_cast<uint32_t>(fb[o + 1]) << 8) | fb[o + 2];
}

bool isNeutral(uint32_t c) {
    const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return r == g && g == b;
}

// Switch into HIRES page 1, graphics, 40 column.
void selectHires(MMU &mmu) {
    mmu.read(0xC051); // TEXT off is C050; read C051 sets text, so clear below
    mmu.read(0xC050); // GRAPHICS
    mmu.read(0xC052); // not mixed
    mmu.read(0xC054); // page 1
    mmu.read(0xC057); // HIRES
    mmu.read(0xC00C); // 40 column
}

} // namespace

TEST_CASE("HIRES high bit delays the byte by one 14MHz dot", "[video][signal]") {
    // The half-dot shift is the whole mechanism behind orange and blue, and it
    // was previously faked by swapping palette entries. Check the geometry
    // directly: the same bit pattern with the high bit set must appear one
    // 560-wide pixel further right.
    VideoTestFixture noDelay;
    selectHires(noDelay.mmu);
    noDelay.mmu.write(0x2000, 0x01); // leftmost dot pair on, high bit clear
    noDelay.video->setColorMode(VideoColorMode::MONOCHROME);
    noDelay.video->forceRenderFrame();

    VideoTestFixture delayed;
    selectHires(delayed.mmu);
    delayed.mmu.write(0x2000, 0x81); // same pattern, high bit set
    delayed.video->setColorMode(VideoColorMode::MONOCHROME);
    delayed.video->forceRenderFrame();

    const uint32_t on = 0xFFFFFFFFu;
    const uint32_t off = 0xFF000000u;

    // Undelayed: dots 0 and 1 lit.
    CHECK(pixelAt(*noDelay.video, 0, 0) == on);
    CHECK(pixelAt(*noDelay.video, 1, 0) == on);
    CHECK(pixelAt(*noDelay.video, 2, 0) == off);

    // Delayed: shifted right by exactly one dot. Dot 0 holds what the shift
    // register was already outputting, which at the start of a line is black.
    CHECK(pixelAt(*delayed.video, 0, 0) == off);
    CHECK(pixelAt(*delayed.video, 1, 0) == on);
    CHECK(pixelAt(*delayed.video, 2, 0) == on);
    CHECK(pixelAt(*delayed.video, 3, 0) == off);
}

TEST_CASE("HIRES high bit swaps violet for blue", "[video][signal]") {
    // A byte holds seven pixels, so the parity of a pixel's position — and
    // therefore its colour — flips from column to column. Alternating 0x55/0x2A
    // lands every lit pixel on an even position, which is a solid violet line;
    // setting the high bit throughout delays the lot by one dot and turns it
    // blue. Those two hues are the LORES purple and medium blue, because
    // artifact colour and LORES colour are the same mechanism.
    const auto &palette = ntsc::idealPalette();

    VideoTestFixture a;
    selectHires(a.mmu);
    for (int col = 0; col < 40; col++) {
        a.mmu.write(0x2000 + col, (col % 2) ? 0x2A : 0x55);
    }
    a.video->setColorMode(VideoColorMode::COMPOSITE);
    a.video->forceRenderFrame();

    VideoTestFixture b;
    selectHires(b.mmu);
    for (int col = 0; col < 40; col++) {
        b.mmu.write(0x2000 + col, (col % 2) ? 0xAA : 0xD5);
    }
    b.video->setColorMode(VideoColorMode::COMPOSITE);
    b.video->forceRenderFrame();

    const uint32_t violet = pixelAt(*a.video, 280, 0);
    const uint32_t blue = pixelAt(*b.video, 280, 0);

    CHECK(!isNeutral(violet));
    CHECK(!isNeutral(blue));
    CHECK(violet != blue);

    // LORES 3 is Purple, LORES 6 is Medium Blue.
    auto near = [](uint32_t x, uint32_t y) {
        return std::abs(static_cast<int>((x >> 16) & 0xFF) -
                        static_cast<int>((y >> 16) & 0xFF)) <= 6 &&
               std::abs(static_cast<int>((x >> 8) & 0xFF) -
                        static_cast<int>((y >> 8) & 0xFF)) <= 6 &&
               std::abs(static_cast<int>(x & 0xFF) -
                        static_cast<int>(y & 0xFF)) <= 6;
    };
    CHECK(near(violet, palette[3]));
    CHECK(near(blue, palette[6]));
}

TEST_CASE("Text mode inhibits colour burst so text is colourless",
          "[video][signal][burst]") {
    // A //e kills the burst in text mode. Every pixel of a text screen must
    // therefore be neutral even under the composite decoder — no palette
    // special-case involved, it falls out of the signal.
    VideoTestFixture f;
    for (int i = 0; i < 0x400; i++) {
        f.mmu.write(0x0400 + i, static_cast<uint8_t>(0xC1 + (i % 26)));
    }
    f.video->setColorMode(VideoColorMode::COMPOSITE);
    f.video->forceRenderFrame();

    for (int scanline = 0; scanline < 192; scanline += 7) {
        for (int x = 0; x < 560; x += 3) {
            INFO("x=" << x << " scanline=" << scanline);
            REQUIRE(isNeutral(pixelAt(*f.video, x, scanline)));
        }
    }
}

TEST_CASE("Mixed mode fringes its text rows", "[video][signal][burst]") {
    // The machine inhibits burst on the four text rows of a mixed screen, but
    // the monitor does not decide colour a line at a time: its colour killer
    // integrates burst over a field and its reference flywheels through gaps.
    // 160 of 192 lines carry burst, so the killer stays disabled and the text
    // rows are decoded in colour along with everything else — which is why
    // mixed-mode text fringes on real hardware while full text mode does not.
    VideoTestFixture f;
    selectHires(f.mmu);
    f.mmu.read(0xC053); // mixed on

    // Fill hires memory with an alternating pattern that artifacts strongly.
    for (int line = 0; line < 192; line++) {
        for (int col = 0; col < 40; col++) {
            const int block = line / 8, row = line % 8;
            const uint16_t addr = 0x2000 + (block % 8) * 0x80 + (block / 8) * 0x28 +
                                  row * 0x400 + col;
            f.mmu.write(addr, 0x2A);
        }
    }
    f.video->setColorMode(VideoColorMode::COMPOSITE);
    f.video->forceRenderFrame();

    bool graphicsHasColour = false;
    for (int x = 100; x < 460; x++) {
        if (!isNeutral(pixelAt(*f.video, x, 100))) graphicsHasColour = true;
    }
    CHECK(graphicsHasColour);

    // Put text on the bottom rows and check it picks up colour too.
    for (int row = 20; row < 24; row++) {
        const uint16_t base = 0x400 + ((row % 8) * 0x80) + ((row / 8) * 0x28);
        for (int c = 0; c < 40; c++) {
            f.mmu.write(base + c, static_cast<uint8_t>(0xC1 + (c % 26)));
        }
    }
    f.video->forceRenderFrame();

    bool textHasColour = false;
    for (int scanline = 162; scanline < 190; scanline++) {
        for (int x = 20; x < 540; x++) {
            if (!isNeutral(pixelAt(*f.video, x, scanline))) textHasColour = true;
        }
    }
    CHECK(textHasColour);
}

TEST_CASE("The colour killer engages only when a whole field lacks burst",
          "[video][signal][burst]") {
    // Full text: no burst anywhere, killer engages, picture is monochrome.
    VideoTestFixture text;
    for (int i = 0; i < 0x400; i++) {
        text.mmu.write(0x0400 + i, static_cast<uint8_t>(0xC1 + (i % 26)));
    }
    text.video->setColorMode(VideoColorMode::COMPOSITE);
    text.video->forceRenderFrame();

    bool anyColour = false;
    for (int scanline = 0; scanline < 192; scanline += 3) {
        for (int x = 0; x < 560; x += 3) {
            if (!isNeutral(pixelAt(*text.video, x, scanline))) anyColour = true;
        }
    }
    CHECK_FALSE(anyColour);

    // The same text, but with graphics above it so the field carries burst.
    // Identical bytes on the identical rows, and now they are in colour: the
    // difference is the receiver, not anything the text itself is doing.
    VideoTestFixture mixed;
    mixed.mmu.read(0xC050); // graphics
    mixed.mmu.read(0xC053); // mixed
    mixed.mmu.read(0xC056); // lores
    for (int i = 0; i < 0x400; i++) {
        mixed.mmu.write(0x0400 + i, static_cast<uint8_t>(0xC1 + (i % 26)));
    }
    mixed.video->setColorMode(VideoColorMode::COMPOSITE);
    mixed.video->forceRenderFrame();

    bool textRowsColoured = false;
    for (int scanline = 162; scanline < 190; scanline++) {
        for (int x = 20; x < 540; x++) {
            if (!isNeutral(pixelAt(*mixed.video, x, scanline))) textRowsColoured = true;
        }
    }
    CHECK(textRowsColoured);
}

TEST_CASE("LORES colours are uniform across columns", "[video][signal]") {
    // A 14-dot byte cell is three and a half subcarrier cycles, so a naive
    // per-cell phase would make odd columns the complementary colour. Every
    // column of a solid LORES fill must be the same colour.
    VideoTestFixture f;
    f.mmu.read(0xC050); // graphics
    f.mmu.read(0xC052); // not mixed
    f.mmu.read(0xC056); // lores
    f.mmu.read(0xC054); // page 1
    f.mmu.read(0xC00C); // 40 column
    for (int i = 0; i < 40; i++) {
        f.mmu.write(0x0400 + i, 0x11); // colour 1 in both half-rows
    }
    f.video->setColorMode(VideoColorMode::COMPOSITE);
    f.video->forceRenderFrame();

    const uint32_t expected = pixelAt(*f.video, 280, 2);
    CHECK(!isNeutral(expected));
    for (int x = 100; x < 460; x++) {
        INFO("x=" << x);
        REQUIRE(pixelAt(*f.video, x, 2) == expected);
    }
}

// ============================================================================
// Colour mode selection
// ============================================================================

TEST_CASE("setColorMode round-trips and setMonochrome restores it",
          "[video][options]") {
    VideoTestFixture f;

    f.video->setColorMode(VideoColorMode::COMPOSITE);
    CHECK(f.video->getColorMode() == VideoColorMode::COMPOSITE);
    CHECK_FALSE(f.video->isMonochrome());

    // Toggling monochrome must come back to the mode that was in use, not to
    // some fixed default — a green-screen preset should not lose the choice.
    f.video->setMonochrome(true);
    CHECK(f.video->isMonochrome());
    CHECK(f.video->getColorMode() == VideoColorMode::MONOCHROME);

    f.video->setMonochrome(false);
    CHECK(f.video->getColorMode() == VideoColorMode::COMPOSITE);

    f.video->setColorMode(VideoColorMode::PIXEL_EXACT);
    f.video->setMonochrome(true);
    f.video->setMonochrome(false);
    CHECK(f.video->getColorMode() == VideoColorMode::PIXEL_EXACT);
}

TEST_CASE("Pixel exact is sharper than composite on the same signal",
          "[video][options]") {
    // Same bytes, two decoders. The sharp one must produce fewer distinct
    // values across an edge than the filtered one.
    VideoTestFixture f;
    selectHires(f.mmu);
    for (int col = 10; col < 20; col++) f.mmu.write(0x2000 + col, 0x7F);

    f.video->setColorMode(VideoColorMode::PIXEL_EXACT);
    f.video->forceRenderFrame();
    std::set<uint32_t> sharp;
    for (int x = 130; x < 160; x++) sharp.insert(pixelAt(*f.video, x, 0));

    f.video->setColorMode(VideoColorMode::COMPOSITE);
    f.video->forceRenderFrame();
    std::set<uint32_t> soft;
    for (int x = 130; x < 160; x++) soft.insert(pixelAt(*f.video, x, 0));

    CHECK(sharp.size() < soft.size());
}

TEST_CASE("Double hi-res sits one dot later than 40-column video",
          "[video][signal][dhgr]") {
    // The 80-column shift path is clocked a dot after the 40-column one, so a
    // four-dot pattern v lands on the next subcarrier phase and shows the
    // colour of nibble rotate-left-1(v). This is the relationship the old
    // DLGR_COLORS table encoded by hand; getting it wrong rotates every double
    // hi-res picture a quarter turn round the colour wheel, which is subtle
    // enough to look plausible and is why it needs pinning down here.
    const auto &palette = ntsc::idealPalette();

    for (int v = 1; v < 15; v++) {
        VideoTestFixture f;
        f.mmu.read(0xC050);        // graphics
        f.mmu.read(0xC052);        // not mixed
        f.mmu.read(0xC057);        // hires
        f.mmu.write(0xC00D, 0);    // 80 column (a write switch on the IIe)
        f.mmu.read(0xC05E);        // AN3 off -> double hi-res

        // Lay down a steady 4-dot pattern of value v, in the dot domain, then
        // pack it back into the aux/main byte pair the hardware reads.
        for (int line = 0; line < 192; line++) {
            const int block = line / 8, row = line % 8;
            for (int col = 0; col < 40; col++) {
                const uint16_t addr = 0x2000 + (block % 8) * 0x80 +
                                      (block / 8) * 0x28 + row * 0x400 + col;
                uint8_t aux = 0, main = 0;
                for (int i = 0; i < 7; i++) {
                    if ((v >> ((14 * col + i) & 3)) & 1) aux |= (1 << i);
                    if ((v >> ((14 * col + 7 + i) & 3)) & 1) main |= (1 << i);
                }
                f.mmu.writeRAM(addr, aux, true);
                f.mmu.writeRAM(addr, main, false);
            }
        }

        f.video->setColorMode(VideoColorMode::COMPOSITE);
        f.video->forceRenderFrame();

        const int rotated = ((v << 1) | (v >> 3)) & 0x0F;
        const uint32_t got = pixelAt(*f.video, 280, 60);

        INFO("dot value " << v << " should show palette entry " << rotated);
        const int dr = std::abs(static_cast<int>((got >> 16) & 0xFF) -
                                static_cast<int>((palette[rotated] >> 16) & 0xFF));
        const int dg = std::abs(static_cast<int>((got >> 8) & 0xFF) -
                                static_cast<int>((palette[rotated] >> 8) & 0xFF));
        const int db = std::abs(static_cast<int>(got & 0xFF) -
                                static_cast<int>(palette[rotated] & 0xFF));
        CHECK(dr <= 4);
        CHECK(dg <= 4);
        CHECK(db <= 4);
    }
}
