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

TEST_CASE("WozDiskImage createBlank is not modified until written", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    // Creating a disk is not a change to it. A blank disk nobody has written
    // to holds nothing worth saving, and marking it modified here made every
    // eject offer to save an empty image.
    REQUIRE_FALSE(img.isModified());

    img.writeBit(1);
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

// Helpers that drive the stepper the way real firmware does, one half-track
// per iteration. quarter-track position advances 2 per half-track step.
namespace {
// Overlapping style: energise the next magnet before de-energising the current.
void seekOverlap(WozDiskImage &img, int halfTracks, int dir) {
    int half = 0; // caller guarantees the head is at track 0 (magnet 0 aligned)
    for (int i = 0; i < halfTracks; ++i) {
        int oldPhase = ((half % 4) + 4) % 4;
        half += dir;
        int newPhase = ((half % 4) + 4) % 4;
        img.setPhase(newPhase, true);
        img.setPhase(oldPhase, false);
    }
}
// Non-overlapping style: pulse each magnet fully on then off, no overlap. Some
// copy-protected loaders (e.g. subLOGIC Flight Simulator II) seek like this.
void seekPulse(WozDiskImage &img, int halfTracks, int dir) {
    int half = 0; // caller guarantees the head is at track 0 (magnet 0 aligned)
    for (int i = 0; i < halfTracks; ++i) {
        half += dir;
        int phase = ((half % 4) + 4) % 4;
        img.setPhase(phase, true);
        img.setPhase(phase, false);
    }
}
} // namespace

TEST_CASE("WozDiskImage overlapping seek reaches exact track", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    img.resetState();
    seekOverlap(img, 20, +1); // 10 whole tracks inward
    REQUIRE(img.getTrack() == 10);
    REQUIRE(img.getQuarterTrack() == 40);
}

// Regression: non-overlapping (pulse) stepping must move the head. The old
// stepper only stepped when a phase was turned OFF while an adjacent phase was
// still ON, so pulse seeks moved the head zero tracks and disks that stepped
// this way (Flight Simulator II) hung mid-load.
TEST_CASE("WozDiskImage non-overlapping pulse seek moves head", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    img.resetState();
    seekPulse(img, 20, +1); // 10 whole tracks inward
    REQUIRE(img.getTrack() == 10);
    REQUIRE(img.getQuarterTrack() == 40);
}

// Recalibration (banging the head against track 0) must not desync the stepper:
// a subsequent inward seek must still land on the exact track.
TEST_CASE("WozDiskImage recalibration then seek lands on exact track", "[woz]") {
    WozDiskImage img;
    img.createBlank();

    img.resetState();
    seekPulse(img, 80, -1); // bang outward against track 0
    REQUIRE(img.getQuarterTrack() == 0);

    seekOverlap(img, 24, +1); // now seek 12 tracks inward
    REQUIRE(img.getTrack() == 12);
    REQUIRE(img.getQuarterTrack() == 48);
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

// ---------------------------------------------------------------------------
// WOZ 2.1 flux tracks
// ---------------------------------------------------------------------------

namespace {

void putChunk(std::vector<uint8_t> &file, const char *id,
              const std::vector<uint8_t> &data) {
    file.insert(file.end(), id, id + 4);
    uint32_t size = static_cast<uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) file.push_back((size >> (8 * i)) & 0xFF);
    file.insert(file.end(), data.begin(), data.end());
}

// A WOZ 2.1 image with one track: a bit track in TMAP entry 0 on quarter
// tracks 0-1, and a flux track in entry 1 that the FLUX chunk puts on quarter
// track 0 over it. The bit track is all zeros so reading it by mistake shows.
std::vector<uint8_t> buildFluxWoz(const std::vector<uint8_t> &flux) {
    std::vector<uint8_t> file = {'W', 'O', 'Z', '2', 0xFF, 0x0A, 0x0D, 0x0A,
                                 0, 0, 0, 0};

    std::vector<uint8_t> info(60, 0);
    info[0] = 3;  // INFO version 3
    info[1] = 1;  // 5.25"
    info[37] = 1; // one side
    info[39] = 32;
    putChunk(file, "INFO", info);

    std::vector<uint8_t> tmap(160, 0xFF);
    tmap[0] = 0;
    tmap[1] = 0;
    putChunk(file, "TMAP", tmap);

    // The chunks take up the first four blocks; track data follows them
    const uint16_t firstBlock = 4;
    const size_t bitBlocks = 1;
    const size_t fluxBlocks = (flux.size() + 511) / 512;
    std::vector<uint8_t> trks(160 * 8, 0);
    auto entry = [&](int i, uint16_t start, uint16_t count, uint32_t n) {
        trks[i * 8 + 0] = start & 0xFF;
        trks[i * 8 + 1] = start >> 8;
        trks[i * 8 + 2] = count & 0xFF;
        trks[i * 8 + 3] = count >> 8;
        for (int b = 0; b < 4; ++b) trks[i * 8 + 4 + b] = (n >> (8 * b)) & 0xFF;
    };
    entry(0, firstBlock, bitBlocks, 4096);
    entry(1, firstBlock + bitBlocks, static_cast<uint16_t>(fluxBlocks),
          static_cast<uint32_t>(flux.size()));
    putChunk(file, "TRKS", trks);

    std::vector<uint8_t> fluxMap(160, 0xFF);
    fluxMap[0] = 1;
    putChunk(file, "FLUX", fluxMap);

    REQUIRE(file.size() <= firstBlock * 512u);
    file.resize((firstBlock + bitBlocks) * 512, 0); // the zero bit track
    std::vector<uint8_t> data = flux;
    data.resize(fluxBlocks * 512, 0);
    file.insert(file.end(), data.begin(), data.end());
    return file;
}

// Flux bytes for a list of intervals in 125ns ticks, 255 carrying over
std::vector<uint8_t> fluxBytes(const std::vector<int> &intervals) {
    std::vector<uint8_t> flux;
    for (int ticks : intervals) {
        while (ticks >= 255) {
            flux.push_back(255);
            ticks -= 255;
        }
        flux.push_back(static_cast<uint8_t>(ticks));
    }
    return flux;
}

// The sequencer tick each transition should arrive on: a tick is 7 cycles of
// the 14.31818MHz clock, which is 176/45 of the flux data's 125ns. The track
// is one revolution, so the last transition is also tick 0 of the next.
std::vector<uint32_t> expectedTicks(const std::vector<int> &intervals) {
    std::vector<uint32_t> ticks = {0};
    uint64_t now = 0;
    for (size_t i = 0; i + 1 < intervals.size(); ++i) {
        now += intervals[i];
        ticks.push_back(static_cast<uint32_t>(now * 45 / 176));
    }
    return ticks;
}

uint32_t trackTicks(const std::vector<int> &intervals) {
    uint64_t now = 0;
    for (int interval : intervals) now += interval;
    return static_cast<uint32_t>(now * 45 / 176);
}

std::vector<uint32_t> pulseTicks(WozDiskImage &img, uint32_t count) {
    std::vector<uint32_t> ticks;
    for (uint32_t t = 0; t < count; ++t) {
        if (img.readTick()) ticks.push_back(t);
    }
    return ticks;
}

// Fifteen cells written fast (3.7us) then the same fifteen written slow
// (4.1us), as parts of the Sirius loader's tracks are
const std::vector<int> TWO_SPEEDS = {
    30, 59, 30, 30, 59, 30, 30, 30, 59, 30, 30, 30,
    33, 65, 33, 33, 65, 33, 33, 33, 65, 33, 33, 33};

} // namespace

TEST_CASE("WozDiskImage plays a flux track back at its recorded timing", "[woz][flux]") {
    auto file = buildFluxWoz(fluxBytes(TWO_SPEEDS));
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));
    REQUIRE(img.getFormatName() == "WOZ 2.1 (flux)");

    img.setQuarterTrack(0);
    REQUIRE(img.hasData());
    REQUIRE(img.isTickTimed());

    // Every transition arrives on the tick its time falls in, so a cell in
    // the fast half is under eight ticks and one in the slow half is over,
    // which is the difference a timing check measures
    auto expected = expectedTicks(TWO_SPEEDS);
    auto ticks = pulseTicks(img, trackTicks(TWO_SPEEDS));
    REQUIRE(ticks == expected);

    // ...and the next revolution starts where this one did
    REQUIRE(img.readTick() == 1);

    uint32_t fast = expected[12] - expected[0];
    uint32_t slow = trackTicks(TWO_SPEEDS) - expected[12];
    REQUIRE(fast < 15 * 8 - 4);
    REQUIRE(slow > 15 * 8 + 4);
}

TEST_CASE("WozDiskImage takes a FLUX quarter track over TMAP's", "[woz][flux]") {
    auto file = buildFluxWoz(fluxBytes(TWO_SPEEDS));
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));

    // Quarter track 0 is in both maps and plays the flux
    img.setQuarterTrack(0);
    REQUIRE(img.isTickTimed());

    // Quarter track 1 is only in TMAP, so it reads the (zero) bit track
    img.setQuarterTrack(1);
    REQUIRE_FALSE(img.isTickTimed());
    for (int i = 0; i < 64; ++i) REQUIRE(img.readBit() == 0);
}

TEST_CASE("WozDiskImage carries a flux count across 255 bytes", "[woz][flux]") {
    // A gap of twenty cells is 626 ticks: 255, 255, 116
    std::vector<int> intervals = {31, 626, 31};
    auto flux = fluxBytes(intervals);
    REQUIRE(flux == std::vector<uint8_t>{31, 255, 255, 116, 31});

    auto file = buildFluxWoz(flux);
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));
    REQUIRE(pulseTicks(img, trackTicks(intervals)) == expectedTicks(intervals));
}

TEST_CASE("WozDiskImage saves a flux track as flux", "[woz][flux]") {
    auto file = buildFluxWoz(fluxBytes(TWO_SPEEDS));
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));

    size_t size = 0;
    const uint8_t *saved = img.exportData(&size);
    REQUIRE(saved != nullptr);
    std::vector<uint8_t> copy(saved, saved + size);

    // INFO names the block the FLUX chunk starts on, and TMAP gives the
    // quarter track up to it
    const size_t info = 12 + 8;
    REQUIRE(copy[info] == 3);
    uint16_t fluxBlock = copy[info + 46] | (copy[info + 47] << 8);
    REQUIRE(fluxBlock > 0);
    REQUIRE(std::memcmp(&copy[fluxBlock * 512], "FLUX", 4) == 0);
    REQUIRE(copy[fluxBlock * 512 + 8] == 1);
    REQUIRE(copy[info + 60 + 8] == 0xFF);

    WozDiskImage reloaded;
    REQUIRE(reloaded.load(copy.data(), copy.size(), "saved.woz"));
    REQUIRE(reloaded.getFormatName() == "WOZ 2.1 (flux)");
    REQUIRE(pulseTicks(reloaded, trackTicks(TWO_SPEEDS)) ==
            expectedTicks(TWO_SPEEDS));

    // The bit track that shared the quarter track is still there
    reloaded.setQuarterTrack(1);
    REQUIRE(reloaded.hasData());
    REQUIRE_FALSE(reloaded.isTickTimed());
}

TEST_CASE("WozDiskImage turns a flux track into bits when written", "[woz][flux]") {
    auto file = buildFluxWoz(fluxBytes(TWO_SPEEDS));
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));

    // The first cell holds a 1 already, so this changes the kind, not the data
    img.writeBit(1);
    REQUIRE(img.isModified());
    REQUIRE_FALSE(img.isTickTimed());

    size_t size = 0;
    const uint8_t *saved = img.exportData(&size);
    std::vector<uint8_t> copy(saved, saved + size);
    REQUIRE((copy[12 + 8 + 46] | (copy[12 + 8 + 47] << 8)) == 0);

    WozDiskImage reloaded;
    REQUIRE(reloaded.load(copy.data(), copy.size(), "saved.woz"));
    REQUIRE(reloaded.getFormatName() == "WOZ 2.0");
    std::vector<uint8_t> bits;
    for (int i = 0; i < 30; ++i) bits.push_back(reloaded.readBit());
    // Both halves are 1 01 1 1 01 1 1 1 01 1 1 1: the same cells at two speeds
    std::vector<uint8_t> half = {1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1};
    std::vector<uint8_t> expected = half;
    expected.insert(expected.end(), half.begin(), half.end());
    REQUIRE(bits == expected);
}

TEST_CASE("WozDiskImage keeps the disk's angle between flux and bit tracks", "[woz][flux]") {
    auto file = buildFluxWoz(fluxBytes(TWO_SPEEDS));
    WozDiskImage img;
    REQUIRE(img.load(file.data(), file.size(), "flux.woz"));

    img.setQuarterTrack(0);
    pulseTicks(img, 8 * 20);             // twenty cells of flux
    REQUIRE(img.getCurrentNibblePosition() == 2);
    img.setQuarterTrack(1);              // onto the bit track
    for (int i = 0; i < 4; ++i) img.readBit();
    REQUIRE(img.getCurrentNibblePosition() == 3);
}
