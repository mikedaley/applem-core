/*
 * test_dsk_disk_image.cpp - Unit tests for DskDiskImage
 *
 * Tests the DSK/DO/PO disk image format implementation including:
 * - Loading with .dsk and .po filenames
 * - Format detection from filename
 * - Track count and geometry
 * - Initial track position
 * - Head positioning via setPhase / getQuarterTrack
 * - Nibble reading after load
 * - Sector data access
 * - Modification tracking
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "dsk_disk_image.hpp"
#include "gcr_encoding.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace a2e;

// Helper: create a zero-filled DSK image (143360 bytes)
static std::vector<uint8_t> createBlankDSK() {
    return std::vector<uint8_t>(DskDiskImage::DISK_SIZE, 0x00);
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage load with valid .dsk data succeeds", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();

    bool ok = img.load(data.data(), data.size(), "test.dsk");
    REQUIRE(ok);
    REQUIRE(img.isLoaded());
}

TEST_CASE("DskDiskImage load with .dsk filename sets DSK format", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();

    img.load(data.data(), data.size(), "test.dsk");
    // .dsk files are typically treated as DOS-order (DSK or DO)
    DiskImage::Format fmt = img.getFormat();
    REQUIRE((fmt == DiskImage::Format::DSK || fmt == DiskImage::Format::DO));
}

TEST_CASE("DskDiskImage load with .po filename sets PO format", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();

    img.load(data.data(), data.size(), "test.po");
    REQUIRE(img.getFormat() == DiskImage::Format::PO);
}

TEST_CASE("DskDiskImage load with .do filename sets DO/DSK format", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();

    img.load(data.data(), data.size(), "test.do");
    DiskImage::Format fmt = img.getFormat();
    REQUIRE((fmt == DiskImage::Format::DSK || fmt == DiskImage::Format::DO));
}

TEST_CASE("DskDiskImage load with wrong size fails", "[dsk]") {
    DskDiskImage img;
    std::vector<uint8_t> tooSmall(1000, 0x00);

    bool ok = img.load(tooSmall.data(), tooSmall.size(), "bad.dsk");
    REQUIRE_FALSE(ok);
    REQUIRE_FALSE(img.isLoaded());
}

TEST_CASE("DskDiskImage load with nullptr fails gracefully", "[dsk]") {
    DskDiskImage img;
    bool ok = img.load(nullptr, 0, "empty.dsk");
    REQUIRE_FALSE(ok);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage getTrackCount returns 35", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE(img.getTrackCount() == 35);
}

TEST_CASE("DskDiskImage initial track is 0", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE(img.getTrack() == 0);
    REQUIRE(img.getQuarterTrack() == 0);
}

// ---------------------------------------------------------------------------
// Head positioning
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage setQuarterTrack changes position", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    img.setQuarterTrack(8); // track 2
    REQUIRE(img.getQuarterTrack() == 8);
    REQUIRE(img.getTrack() == 2);
}

TEST_CASE("DskDiskImage setQuarterTrack clamps to valid range", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    // Clamp high
    img.setQuarterTrack(1000);
    REQUIRE(img.getQuarterTrack() <= (34 * 4));

    // Clamp low
    img.setQuarterTrack(-10);
    REQUIRE(img.getQuarterTrack() >= 0);
}

TEST_CASE("DskDiskImage setPhase stepping moves head", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE(img.getQuarterTrack() == 0);

    // Simulate stepping from track 0 toward track 1:
    // Phase sequence: activate phase 1 while phase 0 was active, then deactivate phase 0
    img.setPhase(0, true);  // phase 0 on (already at track 0)
    img.setPhase(1, true);  // phase 1 on
    img.setPhase(0, false); // phase 0 off - head should step toward phase 1

    // After stepping, quarter track should have increased
    REQUIRE(img.getQuarterTrack() > 0);
}

// ---------------------------------------------------------------------------
// Nibble reading
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage readNibble returns bytes with bit 7 set after load", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    // Read several nibbles; valid GCR nibbles have bit 7 set
    int validCount = 0;
    for (int i = 0; i < 100; ++i) {
        uint8_t nibble = img.readNibble();
        if (nibble & 0x80) {
            ++validCount;
        }
    }
    // Most nibbles from a freshly nibblized track should be valid
    REQUIRE(validCount > 50);
}

// ---------------------------------------------------------------------------
// Sector data access
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage getSectorData returns correct size", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    size_t size = 0;
    const uint8_t* sectorData = img.getSectorData(&size);
    REQUIRE(sectorData != nullptr);
    REQUIRE(size == 143360);
}

TEST_CASE("DskDiskImage getSectorData matches loaded data", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    // Write a known pattern
    data[0] = 0xAB;
    data[1] = 0xCD;
    data[143359] = 0xEF;
    img.load(data.data(), data.size(), "test.dsk");

    size_t size = 0;
    const uint8_t* sectorData = img.getSectorData(&size);
    REQUIRE(sectorData != nullptr);
    REQUIRE(sectorData[0] == 0xAB);
    REQUIRE(sectorData[1] == 0xCD);
    REQUIRE(sectorData[143359] == 0xEF);
}

// ---------------------------------------------------------------------------
// Modification tracking
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage isModified is initially false", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE_FALSE(img.isModified());
}

// ---------------------------------------------------------------------------
// Write protection
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage isWriteProtected is initially false", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE_FALSE(img.isWriteProtected());
}

// ---------------------------------------------------------------------------
// Format name
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage getFormatName returns non-empty string", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    std::string name = img.getFormatName();
    REQUIRE_FALSE(name.empty());
}

// ---------------------------------------------------------------------------
// Volume number
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage default volume number is 254", "[dsk]") {
    DskDiskImage img;
    REQUIRE(img.getVolumeNumber() == 254);
}

TEST_CASE("DskDiskImage setVolumeNumber changes volume", "[dsk]") {
    DskDiskImage img;
    img.setVolumeNumber(100);
    REQUIRE(img.getVolumeNumber() == 100);
}

// ---------------------------------------------------------------------------
// Reset state
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage resetState resets head position", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    img.setQuarterTrack(20);
    REQUIRE(img.getQuarterTrack() == 20);

    img.resetState();
    REQUIRE(img.getQuarterTrack() == 0);
    REQUIRE(img.getTrack() == 0);
}

// ---------------------------------------------------------------------------
// hasData
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage hasData after load", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "test.dsk");

    REQUIRE(img.hasData());
}

// ---------------------------------------------------------------------------
// Filename tracking
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage stores filename on load", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "my_disk.dsk");

    REQUIRE(img.getFilename() == "my_disk.dsk");
}

// ---------------------------------------------------------------------------
// ProDOS order detection
// ---------------------------------------------------------------------------

TEST_CASE("DskDiskImage isProDOSOrder for .po file", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "prodos.po");

    REQUIRE(img.isProDOSOrder());
}

TEST_CASE("DskDiskImage isProDOSOrder false for .dsk file", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "dos33.dsk");

    REQUIRE_FALSE(img.isProDOSOrder());
}

// ---------------------------------------------------------------------------
// Bit-level write-back round-trip
//
// Regression guard for the .po write-back bug: in-emulator writes arrive at the
// disk through writeBit() (the LSS bit-level path). getSectorData() must recover
// them by re-syncing on the GCR prologues at the BIT level, exactly like a real
// Disk II. The previous fixed-frame reader (bitTrackToNibbleTrack) walked the
// stale is_sync grid and dropped any sector whose write landed off that grid.
// ---------------------------------------------------------------------------

// Build one complete sector as a GCR nibble stream (sync gap, address field,
// gap, data field). Mirrors the on-disk structure the controller produces.
static std::vector<uint8_t> buildSectorNibbles(uint8_t volume, uint8_t track,
                                               uint8_t sector,
                                               const uint8_t* data256) {
    std::vector<uint8_t> n;
    auto enc44 = [&](uint8_t v) {
        n.push_back((v >> 1) | 0xAA);
        n.push_back(v | 0xAA);
    };
    for (int i = 0; i < 12; ++i) n.push_back(0xFF); // sync gap
    n.push_back(0xD5); n.push_back(0xAA); n.push_back(0x96); // addr prologue
    enc44(volume); enc44(track); enc44(sector);
    enc44(static_cast<uint8_t>(volume ^ track ^ sector)); // checksum
    n.push_back(0xDE); n.push_back(0xAA); n.push_back(0xEB); // addr epilogue
    for (int i = 0; i < 6; ++i) n.push_back(0xFF);  // gap 2
    n.push_back(0xD5); n.push_back(0xAA); n.push_back(0xAD); // data prologue
    auto encoded = GCR::encode6and2(data256);       // 343 nibbles
    n.insert(n.end(), encoded.begin(), encoded.end());
    n.push_back(0xDE); n.push_back(0xAA); n.push_back(0xEB); // data epilogue
    return n;
}

TEST_CASE("DskDiskImage bit-level write round-trips through getSectorData", "[dsk]") {
    DskDiskImage img;
    auto data = createBlankDSK();
    img.load(data.data(), data.size(), "rttest.po");
    REQUIRE(img.getFormat() == DiskImage::Format::PO);

    const int track = 17;

    // Build a fresh 16-sector track image carrying NEW data. Physical sector p
    // is filled with the pattern (p ^ k) so each sector is uniquely identifiable
    // and every byte is integrity-checkable.
    std::vector<uint8_t> trackNibbles;
    for (int p = 0; p < 16; ++p) {
        uint8_t sectorData[256];
        for (int k = 0; k < 256; ++k)
            sectorData[k] = static_cast<uint8_t>(p ^ k);
        auto s = buildSectorNibbles(254, track, static_cast<uint8_t>(p), sectorData);
        trackNibbles.insert(trackNibbles.end(), s.begin(), s.end());
    }
    // Pack nibbles to a bit stream, 8 bits MSB-first (data-byte cadence).
    std::vector<uint8_t> streamBits;
    streamBits.reserve(trackNibbles.size() * 8);
    for (uint8_t nib : trackNibbles)
        for (int b = 7; b >= 0; --b)
            streamBits.push_back((nib >> b) & 1);

    // Position the head and measure the track's bit length by reading one full
    // revolution (the first readBit bitifies the track; the wrap is detected
    // when the nibble position drops back to 0). This leaves bit_position_ at 0.
    img.setQuarterTrack(track * 4);
    img.readBit();
    size_t bitCount = 1;
    size_t prev = img.getCurrentNibblePosition();
    for (;;) {
        img.readBit();
        ++bitCount;
        size_t cur = img.getCurrentNibblePosition();
        if (cur < prev) break; // wrapped past the index mark
        prev = cur;
    }
    REQUIRE(streamBits.size() < bitCount); // one full track copy must fit

    // Drive the new track in through the bit-level write path, overwriting the
    // whole revolution (cycling the stream; the trailing partial copy is just
    // skipped by the scanner).
    for (size_t i = 0; i < bitCount; ++i)
        img.writeBit(streamBits[i % streamBits.size()]);

    REQUIRE(img.isModified());

    // Read back: every physical sector must reappear, each in a distinct logical
    // slot, byte-for-byte intact.
    size_t size = 0;
    const uint8_t* sd = img.getSectorData(&size);
    REQUIRE(size == 143360);

    std::vector<int> recovered;
    for (int logical = 0; logical < 16; ++logical) {
        const uint8_t* blk = sd + (track * 16 + logical) * 256;
        uint8_t p = blk[0]; // data[k] = p ^ k, so data[0] == p
        REQUIRE(p < 16);
        bool intact = true;
        for (int k = 0; k < 256; ++k)
            if (blk[k] != static_cast<uint8_t>(p ^ k)) { intact = false; break; }
        REQUIRE(intact);
        recovered.push_back(p);
    }
    std::sort(recovered.begin(), recovered.end());
    for (int p = 0; p < 16; ++p)
        REQUIRE(recovered[p] == p); // all 16 sectors present, none dropped

    // A different, untouched track must remain blank (no cross-track damage).
    for (int k = 0; k < 256; ++k)
        REQUIRE(sd[(0 * 16 + 0) * 256 + k] == 0x00);
}
