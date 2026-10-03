/*
 * test_disk_inspection.cpp - The Disk Inspector's track analysis
 *
 * The analyser reads a track the way the drive's latch does and names what it
 * finds. These pin that it finds a standard sector and decodes it, that a
 * failed checksum is reported rather than hidden, that a field across the end
 * of the track still reads, that noise is told apart from well-formed data it
 * does not recognise, and that the buffers the window reads carry all of it
 * for every image format.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "disk_inspection.hpp"
#include "dsk_disk_image.hpp"
#include "gcr_encoding.hpp"
#include "woz_disk_image.hpp"

#include <cstring>
#include <vector>

using namespace a2e;
using namespace a2e::inspect;

namespace {

// A bit stream built nibble by nibble, sync bytes with their two extra zeros
struct TrackBuilder {
    std::vector<uint8_t> bits;
    uint32_t count = 0;

    void bit(int b) {
        if ((count & 7) == 0) bits.push_back(0);
        if (b) bits.back() |= static_cast<uint8_t>(0x80 >> (count & 7));
        count++;
    }
    void nibble(uint8_t v, int extraZeros = 0) {
        for (int i = 7; i >= 0; --i) bit((v >> i) & 1);
        for (int i = 0; i < extraZeros; ++i) bit(0);
    }
    void sync(int n) {
        for (int i = 0; i < n; ++i) nibble(0xFF, 2);
    }
    void sector(uint8_t volume, uint8_t track, uint8_t sector,
                const uint8_t *data, int corruptData = -1, bool badAddress = false) {
        for (uint8_t v : {0xD5, 0xAA, 0x96}) nibble(v);
        const uint8_t sum = static_cast<uint8_t>(volume ^ track ^ sector ^ (badAddress ? 0x55 : 0));
        for (uint8_t value : {volume, track, sector, sum}) {
            auto pair = GCR::encode4and4(value);
            nibble(pair.first);
            nibble(pair.second);
        }
        for (uint8_t v : {0xDE, 0xAA, 0xEB}) nibble(v);
        sync(6);
        for (uint8_t v : {0xD5, 0xAA, 0xAD}) nibble(v);
        auto encoded = GCR::encode6and2(data);
        REQUIRE(encoded.size() == 343);
        for (size_t i = 0; i < encoded.size(); ++i) {
            // A different valid disk byte in place of the one encoded
            uint8_t v = encoded[i];
            if (static_cast<int>(i) == corruptData) v = (v == 0x96) ? 0x97 : 0x96;
            nibble(v);
        }
        for (uint8_t v : {0xDE, 0xAA, 0xEB}) nibble(v);
    }
};

std::vector<uint8_t> pattern(uint8_t seed) {
    std::vector<uint8_t> data(256);
    for (int i = 0; i < 256; ++i) data[i] = static_cast<uint8_t>(seed * 7 + i);
    return data;
}

uint32_t u32at(const std::vector<uint8_t> &b, size_t o) {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (uint32_t(b[o + 3]) << 24);
}
uint16_t u16at(const std::vector<uint8_t> &b, size_t o) {
    return static_cast<uint16_t>(b[o] | (b[o + 1] << 8));
}

} // namespace

TEST_CASE("analyzeTrack finds and decodes a standard sector", "[inspect]") {
    auto data = pattern(3);
    TrackBuilder t;
    t.sync(40);
    t.sector(254, 17, 5, data.data());
    t.sync(40);

    auto a = analyzeTrack(t.bits.data(), t.count);
    REQUIRE(a.sectors.size() == 1);
    const Sector &s = a.sectors[0];
    REQUIRE(s.volume == 254);
    REQUIRE(s.track == 17);
    REQUIRE(s.sector == 5);
    REQUIRE(s.sectors_per_track == 16);
    REQUIRE(s.address_ok);
    REQUIRE(s.data == DataState::Good);
    REQUIRE(std::memcmp(s.bytes.data(), data.data(), 256) == 0);

    // Every nibble is accounted for, and each part is named
    int kinds[KIND_COUNT] = {};
    for (const Nibble &n : a.nibbles) kinds[n.kind & KIND_MASK]++;
    REQUIRE(kinds[SYNC] == 86);
    REQUIRE(kinds[ADDR_PROLOGUE] == 3);
    REQUIRE(kinds[ADDR] == 8);
    REQUIRE(kinds[ADDR_EPILOGUE] == 3);
    REQUIRE(kinds[DATA_PROLOGUE] == 3);
    REQUIRE(kinds[DATA] == 343);
    REQUIRE(kinds[DATA_EPILOGUE] == 3);
    REQUIRE(kinds[OTHER] == 0);
    REQUIRE(kinds[INVALID] == 0);

    // A sync byte takes ten cells and a data byte eight
    REQUIRE(a.nibbles.front().cells == 10);
}

TEST_CASE("analyzeTrack reports a data field that fails its checksum", "[inspect]") {
    auto data = pattern(9);
    TrackBuilder t;
    t.sync(40);
    t.sector(254, 3, 12, data.data(), 100);
    t.sync(40);

    auto a = analyzeTrack(t.bits.data(), t.count);
    REQUIRE(a.sectors.size() == 1);
    REQUIRE(a.sectors[0].address_ok);
    REQUIRE(a.sectors[0].data == DataState::Bad);
    bool badMarked = false;
    for (const Nibble &n : a.nibbles) {
        if ((n.kind & KIND_MASK) == DATA) {
            REQUIRE((n.kind & BAD) != 0);
            badMarked = true;
        }
    }
    REQUIRE(badMarked);
}

TEST_CASE("A track whose address fields never verify is in an unknown format", "[inspect]") {
    // A fast loader's or a copy protection's own format, using the standard
    // marks: not a track of failed checksums, and not sector 255 sixteen times.
    auto data = pattern(4);
    TrackBuilder t;
    t.sync(40);
    for (uint8_t s = 0; s < 4; s++) {
        t.sector(254, 9, s, data.data(), -1, true);
        t.sync(20);
    }
    auto a = analyzeTrack(t.bits.data(), t.count);
    REQUIRE(a.sectors.empty());
    int unknown = 0;
    for (const Nibble &n : a.nibbles) {
        REQUIRE((n.kind & BAD) == 0);
        REQUIRE(n.sector == NO_SECTOR);
        unknown += (n.kind & KIND_MASK) == OTHER;
    }
    REQUIRE(unknown >= 4 * (3 + 8 + 3 + 3 + 343 + 3));
}

TEST_CASE("A standard track with one damaged address field keeps it bad", "[inspect]") {
    // The others verify, so the format is known and the damage is real.
    auto data = pattern(5);
    TrackBuilder t;
    t.sync(40);
    for (uint8_t s = 0; s < 4; s++) {
        t.sector(254, 9, s, data.data(), -1, s == 2);
        t.sync(20);
    }
    auto a = analyzeTrack(t.bits.data(), t.count);
    REQUIRE(a.sectors.size() == 4);
    REQUIRE_FALSE(a.sectors[2].address_ok);
    bool badMarked = false;
    for (const Nibble &n : a.nibbles) badMarked |= (n.kind & KIND_MASK) == ADDR && (n.kind & BAD);
    REQUIRE(badMarked);
}

TEST_CASE("analyzeTrack reads a sector across the end of the track", "[inspect]") {
    auto data = pattern(1);
    TrackBuilder whole;
    whole.sync(30);
    whole.sector(254, 0, 0, data.data());
    whole.sync(30);

    // Rotate the track so the data field is cut by the index
    const uint32_t cut = 30 * 10 + 14 * 8 + 6 * 10 + 200 * 8;
    TrackBuilder rotated;
    for (uint32_t i = 0; i < whole.count; ++i) {
        uint32_t p = (i + cut) % whole.count;
        rotated.bit((whole.bits[p / 8] >> (7 - p % 8)) & 1);
    }

    auto a = analyzeTrack(rotated.bits.data(), rotated.count);
    REQUIRE(a.sectors.size() == 1);
    REQUIRE(a.sectors[0].data == DataState::Good);
    REQUIRE(std::memcmp(a.sectors[0].bytes.data(), data.data(), 256) == 0);
}

TEST_CASE("analyzeTrack tells unrecognised data from noise", "[inspect]") {
    TrackBuilder t;
    t.sync(20);
    // Well-formed 4-and-4 style bytes in no standard field
    for (int i = 0; i < 40; ++i) t.nibble(i & 1 ? 0xAB : 0xEE);
    // A byte with three zeros running, then a byte followed by a long silence
    t.nibble(0x88);
    t.nibble(0xFF, 6);
    t.sync(20);

    auto a = analyzeTrack(t.bits.data(), t.count);
    REQUIRE(a.sectors.empty());
    int other = 0, invalid = 0;
    for (const Nibble &n : a.nibbles) {
        if (n.kind == OTHER) other++;
        if (n.kind == INVALID) invalid++;
    }
    REQUIRE(other == 40);
    REQUIRE(invalid == 2);
}

TEST_CASE("The overview maps a sector image's whole disk", "[inspect]") {
    std::vector<uint8_t> image(143360);
    for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i * 13);
    DskDiskImage dsk;
    REQUIRE(dsk.load(image.data(), image.size(), "test.dsk"));

    auto o = buildOverview(dsk, 256);
    REQUIRE(std::memcmp(o.data(), "DINS", 4) == 0);
    REQUIRE(u16at(o, 4) == OVERVIEW_VERSION);
    REQUIRE(u16at(o, 6) == 160);
    REQUIRE(u16at(o, 8) == 256);
    const size_t record = u16at(o, 10);
    REQUIRE(record == 12 + 256 * 3);
    REQUIRE(o.size() == 16 + 160 * record);

    for (int qt = 0; qt < 160; ++qt) {
        const size_t r = 16 + qt * record;
        INFO("quarter track " << qt);
        if (qt < 140) {
            REQUIRE(o[r] == 1);           // present, not flux
            REQUIRE(o[r + 1] == qt / 4);  // track id
            REQUIRE(o[r + 2] == 16);      // sectors found
            REQUIRE(o[r + 3] == 16);      // all good
            REQUIRE(o[r + 5] == 0);
        } else {
            REQUIRE(o[r] == 0);
            REQUIRE(o[r + 1] == 0xFF);
        }
    }

    // The arcs of track 0 are mostly data, and name all sixteen sectors
    const size_t r0 = 16 + 12;
    int data = 0;
    bool seen[16] = {};
    for (int b = 0; b < 256; ++b) {
        if (o[r0 + b] == DATA) data++;
        uint8_t sector = o[r0 + 256 + b];
        if (sector < 16) seen[sector] = true;
    }
    REQUIRE(data > 128);
    for (bool s : seen) REQUIRE(s);
}

TEST_CASE("Track detail carries the decoded sectors", "[inspect]") {
    std::vector<uint8_t> image(143360);
    for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i / 256);
    DskDiskImage dsk;
    REQUIRE(dsk.load(image.data(), image.size(), "test.dsk"));

    auto d = buildTrackDetail(dsk, 4 * 17, 512);
    REQUIRE(std::memcmp(d.data(), "DTRK", 4) == 0);
    REQUIRE(u16at(d, 6) == 1);
    const uint32_t nibbles = u32at(d, 12);
    const uint16_t sectors = u16at(d, 16);
    REQUIRE(u16at(d, 18) == 512);
    REQUIRE(d[20] == 17);
    REQUIRE(sectors == 16);
    REQUIRE(d.size() == 24 + nibbles * 8 + sectors * 272 + 512);

    // Each sector holds its DOS logical sector's bytes, which this image
    // numbers track * 16 + sector
    for (int i = 0; i < sectors; ++i) {
        const size_t s = 24 + nibbles * 8 + i * 272;
        REQUIRE(d[s + 1] == 17);
        REQUIRE(d[s + 4] == 1);
        REQUIRE(d[s + 5] == static_cast<uint8_t>(DataState::Good));
        int logical = GCR::DOS_PHYSICAL_TO_LOGICAL[d[s + 2]];
        REQUIRE(d[s + 16] == static_cast<uint8_t>(17 * 16 + logical));
    }
}

TEST_CASE("A flux track's timing reaches the overview", "[inspect]") {
    // One WOZ 2.1 track: a quarter written fast, the rest at the nominal rate
    std::vector<uint8_t> flux;
    for (int i = 0; i < 12000; ++i) flux.push_back(i < 3000 ? 29 : 31);

    std::vector<uint8_t> file = {'W', 'O', 'Z', '2', 0xFF, 0x0A, 0x0D, 0x0A,
                                 0, 0, 0, 0};
    auto chunk = [&](const char *id, const std::vector<uint8_t> &data) {
        file.insert(file.end(), id, id + 4);
        uint32_t n = static_cast<uint32_t>(data.size());
        for (int i = 0; i < 4; ++i) file.push_back((n >> (8 * i)) & 0xFF);
        file.insert(file.end(), data.begin(), data.end());
    };
    std::vector<uint8_t> info(60, 0);
    info[0] = 3;
    info[1] = 1;
    chunk("INFO", info);
    chunk("TMAP", std::vector<uint8_t>(160, 0xFF));
    const uint16_t blocks = static_cast<uint16_t>((flux.size() + 511) / 512);
    std::vector<uint8_t> trks(1280, 0);
    trks[0] = 4;
    trks[2] = blocks & 0xFF;
    uint32_t bytes = static_cast<uint32_t>(flux.size());
    for (int i = 0; i < 4; ++i) trks[4 + i] = (bytes >> (8 * i)) & 0xFF;
    chunk("TRKS", trks);
    std::vector<uint8_t> fluxMap(160, 0xFF);
    fluxMap[0] = 0;
    chunk("FLUX", fluxMap);
    file.resize(4 * 512, 0);
    file.insert(file.end(), flux.begin(), flux.end());
    file.resize(file.size() + blocks * 512 - flux.size(), 0);

    WozDiskImage woz;
    REQUIRE(woz.load(file.data(), file.size(), "flux.woz"));
    auto o = buildOverview(woz, 64);
    const size_t record = u16at(o, 10);
    const size_t r = 16;
    REQUIRE(o[r] == 3); // present, flux
    const uint8_t *times = &o[r + 12 + 64 * 2];
    // Quarters of 125ns: 29 ticks a cell is 116, 31 is 124
    REQUIRE(times[2] == 116);
    REQUIRE(times[50] == 124);
    // Quarter track 1 has nothing recorded
    REQUIRE(o[16 + record] == 0);
}
