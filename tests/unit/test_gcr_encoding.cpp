/*
 * test_gcr_encoding.cpp - Unit tests for GCR encoding routines
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "gcr_encoding.hpp"

#include <set>
#include <cstring>

using namespace a2e;

// ============================================================================
// encode4and4 tests
// ============================================================================

TEST_CASE("encode4and4 splits byte into odd and even bit pairs", "[gcr][4and4]") {
    SECTION("zero encodes to (0xAA, 0xAA)") {
        auto [odd, even] = GCR::encode4and4(0x00);
        CHECK(odd == 0xAA);
        CHECK(even == 0xAA);
    }

    SECTION("0xFF encodes correctly") {
        auto [odd, even] = GCR::encode4and4(0xFF);
        CHECK(odd == 0xFF);
        CHECK(even == 0xFF);
    }

    SECTION("both bytes always have high bit set") {
        for (int v = 0; v < 256; v++) {
            auto [odd, even] = GCR::encode4and4(static_cast<uint8_t>(v));
            REQUIRE((odd & 0x80) != 0);
            REQUIRE((even & 0x80) != 0);
        }
    }

    SECTION("decoding recovers original value") {
        // 4-and-4 decoding: AND the two bytes and strip extra bits
        for (int v = 0; v < 256; v++) {
            auto [odd, even] = GCR::encode4and4(static_cast<uint8_t>(v));
            // Odd byte has bits 7,5,3,1 set; even byte has bits 6,4,2,0.
            // To decode: shift odd left 1 then AND, combined with even.
            uint8_t decoded = ((odd << 1) | 0x01) & even;
            CHECK(decoded == static_cast<uint8_t>(v));
        }
    }

    SECTION("specific known value 0xFE") {
        auto [odd, even] = GCR::encode4and4(0xFE);
        // 0xFE = 1111 1110
        // odd bits (7,5,3,1) = 1,1,1,1 -> positions 6,4,2,0 = 0x55 | 0xAA = 0xFF
        // even bits (6,4,2,0) = 1,1,1,0 -> positions 6,4,2,0 = 0x54 | 0xAA = 0xFE
        CHECK(odd == 0xFF);
        CHECK(even == 0xFE);
    }
}

// ============================================================================
// encode6and2 tests
// ============================================================================

TEST_CASE("encode6and2 produces 343 nibbles from 256-byte sector", "[gcr][6and2]") {
    uint8_t sector[256];
    memset(sector, 0, sizeof(sector));

    auto nibbles = GCR::encode6and2(sector);
    REQUIRE(nibbles.size() == 343);
}

TEST_CASE("encode6and2 all nibbles have bit 7 set", "[gcr][6and2]") {
    // Fill sector with sequential pattern
    uint8_t sector[256];
    for (int i = 0; i < 256; i++) {
        sector[i] = static_cast<uint8_t>(i);
    }

    auto nibbles = GCR::encode6and2(sector);
    REQUIRE(nibbles.size() == 343);

    for (size_t i = 0; i < nibbles.size(); i++) {
        INFO("Nibble at position " << i << " is 0x" << std::hex << (int)nibbles[i]);
        REQUIRE((nibbles[i] & 0x80) != 0);
    }
}

TEST_CASE("encode6and2 produces valid encoded nibbles from lookup table", "[gcr][6and2]") {
    // All output nibbles should be values found in the ENCODE_6_AND_2 table
    uint8_t sector[256];
    for (int i = 0; i < 256; i++) {
        sector[i] = static_cast<uint8_t>(i);
    }

    auto nibbles = GCR::encode6and2(sector);

    std::set<uint8_t> validNibbles(GCR::ENCODE_6_AND_2.begin(), GCR::ENCODE_6_AND_2.end());

    for (size_t i = 0; i < nibbles.size(); i++) {
        INFO("Nibble at position " << i << " is 0x" << std::hex << (int)nibbles[i]);
        REQUIRE(validNibbles.count(nibbles[i]) == 1);
    }
}

TEST_CASE("encode6and2 different input produces different output", "[gcr][6and2]") {
    uint8_t sector_a[256], sector_b[256];
    memset(sector_a, 0x00, sizeof(sector_a));
    memset(sector_b, 0xFF, sizeof(sector_b));

    auto nibbles_a = GCR::encode6and2(sector_a);
    auto nibbles_b = GCR::encode6and2(sector_b);

    REQUIRE(nibbles_a.size() == 343);
    REQUIRE(nibbles_b.size() == 343);
    REQUIRE(nibbles_a != nibbles_b);
}

// ============================================================================
// buildSector tests
// ============================================================================

TEST_CASE("buildSector contains address prologue D5 AA 96", "[gcr][buildSector]") {
    uint8_t sector[256];
    memset(sector, 0, sizeof(sector));

    auto stream = GCR::buildSector(254, 0, 0, sector);

    bool found = false;
    for (size_t i = 0; i + 2 < stream.size(); i++) {
        if (stream[i] == 0xD5 && stream[i + 1] == 0xAA && stream[i + 2] == 0x96) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("buildSector contains data prologue D5 AA AD", "[gcr][buildSector]") {
    uint8_t sector[256];
    memset(sector, 0, sizeof(sector));

    auto stream = GCR::buildSector(254, 0, 0, sector);

    bool found = false;
    for (size_t i = 0; i + 2 < stream.size(); i++) {
        if (stream[i] == 0xD5 && stream[i + 1] == 0xAA && stream[i + 2] == 0xAD) {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("buildSector address prologue appears before data prologue", "[gcr][buildSector]") {
    uint8_t sector[256];
    memset(sector, 0, sizeof(sector));

    auto stream = GCR::buildSector(254, 10, 5, sector);

    size_t addrPos = 0, dataPos = 0;
    bool foundAddr = false, foundData = false;

    for (size_t i = 0; i + 2 < stream.size(); i++) {
        if (!foundAddr && stream[i] == 0xD5 && stream[i + 1] == 0xAA && stream[i + 2] == 0x96) {
            addrPos = i;
            foundAddr = true;
        }
        if (!foundData && stream[i] == 0xD5 && stream[i + 1] == 0xAA && stream[i + 2] == 0xAD) {
            dataPos = i;
            foundData = true;
        }
    }

    REQUIRE(foundAddr);
    REQUIRE(foundData);
    REQUIRE(addrPos < dataPos);
}

TEST_CASE("buildSector begins with sync bytes", "[gcr][buildSector]") {
    uint8_t sector[256];
    memset(sector, 0, sizeof(sector));

    auto stream = GCR::buildSector(254, 0, 0, sector);
    REQUIRE(stream.size() > 3);

    // First bytes should be sync bytes (0xFF)
    CHECK(stream[0] == GCR::SYNC_BYTE);
    CHECK(stream[1] == GCR::SYNC_BYTE);
}

// ============================================================================
// ENCODE_6_AND_2 table tests
// ============================================================================

TEST_CASE("ENCODE_6_AND_2 table has exactly 64 entries", "[gcr][table]") {
    REQUIRE(GCR::ENCODE_6_AND_2.size() == 64);
}

TEST_CASE("ENCODE_6_AND_2 all entries have bit 7 set", "[gcr][table]") {
    for (size_t i = 0; i < GCR::ENCODE_6_AND_2.size(); i++) {
        INFO("Entry " << i << " = 0x" << std::hex << (int)GCR::ENCODE_6_AND_2[i]);
        REQUIRE((GCR::ENCODE_6_AND_2[i] & 0x80) != 0);
    }
}

TEST_CASE("ENCODE_6_AND_2 all entries are unique", "[gcr][table]") {
    std::set<uint8_t> seen;
    for (size_t i = 0; i < GCR::ENCODE_6_AND_2.size(); i++) {
        INFO("Duplicate at index " << i << " value 0x" << std::hex << (int)GCR::ENCODE_6_AND_2[i]);
        REQUIRE(seen.insert(GCR::ENCODE_6_AND_2[i]).second);
    }
    REQUIRE(seen.size() == 64);
}

// ============================================================================
// Sync byte constant
// ============================================================================

TEST_CASE("SYNC_BYTE is 0xFF", "[gcr][constants]") {
    REQUIRE(GCR::SYNC_BYTE == 0xFF);
}

TEST_CASE("Address and data prologue constants are correct", "[gcr][constants]") {
    CHECK(GCR::ADDR_PROLOGUE[0] == 0xD5);
    CHECK(GCR::ADDR_PROLOGUE[1] == 0xAA);
    CHECK(GCR::ADDR_PROLOGUE[2] == 0x96);

    CHECK(GCR::DATA_PROLOGUE[0] == 0xD5);
    CHECK(GCR::DATA_PROLOGUE[1] == 0xAA);
    CHECK(GCR::DATA_PROLOGUE[2] == 0xAD);
}
