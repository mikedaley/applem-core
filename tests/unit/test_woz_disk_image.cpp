/*
 * test_woz_disk_image.cpp - Unit tests for WozDiskImage
 *
 * Tests the WOZ 1.0/2.0 bit-accurate disk image format including:
 * - Loading with invalid data
 * - Creating blank WOZ2 images
 * - Bit read/write operations
 * - Track count and head positioning
 * - Format and metadata queries
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "woz_disk_image.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// Loading with invalid data
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage load with empty data returns false", "[woz]") {
    WozDiskImage img;
    bool ok = img.load(nullptr, 0, "empty.woz");
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(img.isLoaded());
}

TEST_CASE("WozDiskImage load with too-small data returns false", "[woz]") {
    WozDiskImage img;
    std::vector<uint8_t> tiny(10, 0x00);
    bool ok = img.load(tiny.data(), tiny.size(), "tiny.woz");
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(img.isLoaded());
}

TEST_CASE("WozDiskImage load with random garbage returns false", "[woz]") {
    WozDiskImage img;
    std::vector<uint8_t> garbage(8192, 0xAA);
    bool ok = img.load(garbage.data(), garbage.size(), "garbage.woz");
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(img.isLoaded());
}

TEST_CASE("WozDiskImage load with wrong signature returns false", "[woz]") {
    WozDiskImage img;
    // Build a buffer that looks like a file header but has wrong signature
    std::vector<uint8_t> badSig(256, 0x00);
    badSig[0] = 'W'; badSig[1] = 'O'; badSig[2] = 'Z'; badSig[3] = '9';
    badSig[4] = 0xFF;
    badSig[5] = 0x0A; badSig[6] = 0x0D; badSig[7] = 0x0A;

    bool ok = img.load(badSig.data(), badSig.size(), "badsig.woz");
    REQUIRE_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Creating blank images
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage createBlank creates a valid image", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.isLoaded());
    REQUIRE(img.getFormat() == DiskImage::Format::WOZ2);
}

TEST_CASE("WozDiskImage createBlank has 35 tracks", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.getTrackCount() == 35);
}

TEST_CASE("WozDiskImage createBlank starts at track 0", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.getTrack() == 0);
    REQUIRE(img.getQuarterTrack() == 0);
}

TEST_CASE("WozDiskImage createBlank reports 5.25 inch disk type", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.getDiskType() == 1); // 1 = 5.25"
}

TEST_CASE("WozDiskImage createBlank has valid disk type string", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    std::string typeStr = img.getDiskTypeString();
    REQUIRE_FALSE(typeStr.empty());
}

TEST_CASE("WozDiskImage createBlank has default bit timing", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // Default optimal bit timing is 32 (= 4 microseconds per bit)
    REQUIRE(img.getOptimalBitTiming() == 32);
}

TEST_CASE("WozDiskImage createBlank is not write-protected", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE_FALSE(img.isWriteProtected());
}

TEST_CASE("WozDiskImage createBlank is marked as modified", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // createBlank marks the image as modified so it will be saved
    REQUIRE(img.isModified());
}

TEST_CASE("WozDiskImage createBlank hasData returns true", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.hasData());
}

// ---------------------------------------------------------------------------
// Bit read/write operations
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage readBit on blank image returns 0 or 1", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // Blank disk is filled with sync bytes, so bits should be valid
    uint8_t bit = img.readBit();
    REQUIRE((bit == 0 || bit == 1));
}

TEST_CASE("WozDiskImage readBit advances position", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    size_t pos1 = img.getCurrentNibblePosition();
    // Read enough bits to shift nibble position
    for (int i = 0; i < 8; ++i) {
        img.readBit();
    }
    size_t pos2 = img.getCurrentNibblePosition();
    REQUIRE(pos2 > pos1);
}

TEST_CASE("WozDiskImage writeBit modifies the image", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // Write some bits
    img.writeBit(1);
    img.writeBit(0);
    img.writeBit(1);
    img.writeBit(1);

    REQUIRE(img.isModified());
}

TEST_CASE("WozDiskImage readNibble on blank image returns sync-like nibble", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // Blank disk is filled with sync bytes (0xFF typically)
    uint8_t nibble = img.readNibble();
    // Valid nibbles have bit 7 set
    REQUIRE((nibble & 0x80) != 0);
}

// ---------------------------------------------------------------------------
// Head positioning
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage setQuarterTrack changes position", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    img.setQuarterTrack(12);
    REQUIRE(img.getQuarterTrack() == 12);
    REQUIRE(img.getTrack() == 3);
}

TEST_CASE("WozDiskImage setPhase stepping moves head", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    REQUIRE(img.getQuarterTrack() == 0);

    // Step outward: phase 0 on, phase 1 on, phase 0 off
    img.setPhase(0, true);
    img.setPhase(1, true);
    img.setPhase(0, false);

    REQUIRE(img.getQuarterTrack() > 0);
}

// ---------------------------------------------------------------------------
// Reset state
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage resetState resets head position", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    img.setQuarterTrack(20);
    REQUIRE(img.getQuarterTrack() == 20);

    img.resetState();
    REQUIRE(img.getQuarterTrack() == 0);
}

// ---------------------------------------------------------------------------
// Format name
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage getFormatName returns non-empty string", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    std::string name = img.getFormatName();
    REQUIRE_FALSE(name.empty());
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST_CASE("WozDiskImage is move-constructible", "[woz]") {
    WozDiskImage img1;
    img1.createBlank();
    REQUIRE(img1.isLoaded());

    WozDiskImage img2(std::move(img1));
    REQUIRE(img2.isLoaded());
    REQUIRE(img2.getTrackCount() == 35);
}

TEST_CASE("WozDiskImage is move-assignable", "[woz]") {
    WozDiskImage img1;
    img1.createBlank();

    WozDiskImage img2;
    img2 = std::move(img1);
    REQUIRE(img2.isLoaded());
}
