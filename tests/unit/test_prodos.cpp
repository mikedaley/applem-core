/*
 * test_prodos.cpp - Unit tests for ProDOS filesystem parser
 *
 * Tests the ProDOS filesystem reader including:
 * - Format detection (isProDOS)
 * - Volume information parsing
 * - Catalog reading
 * - File data retrieval
 * - File type mapping for viewer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "prodos.hpp"
#include "disk_image_builder.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// isProDOS - Format detection
// ---------------------------------------------------------------------------

TEST_CASE("isProDOS returns true for valid ProDOS image", "[prodos][detection]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    REQUIRE(ProDOS::isProDOS(builder.data(), builder.size()));
}

TEST_CASE("isProDOS returns false for zeroed data", "[prodos][detection]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    REQUIRE_FALSE(ProDOS::isProDOS(zeroed.data(), zeroed.size()));
}

TEST_CASE("isProDOS returns false for too-small data", "[prodos][detection]") {
    std::vector<uint8_t> small(512, 0x00);
    REQUIRE_FALSE(ProDOS::isProDOS(small.data(), small.size()));
}

TEST_CASE("isProDOS returns false for null pointer", "[prodos][detection]") {
    REQUIRE_FALSE(ProDOS::isProDOS(nullptr, 0));
}

// ---------------------------------------------------------------------------
// parseVolumeInfo - Volume information
// ---------------------------------------------------------------------------

TEST_CASE("parseVolumeInfo returns correct volume name", "[prodos][volume]") {
    test::ProDOSDiskBuilder builder("MYVOLUME");

    ProDOSVolumeInfo info;
    bool result = ProDOS::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(std::string(info.volumeName) == "MYVOLUME");
}

TEST_CASE("parseVolumeInfo returns correct total blocks", "[prodos][volume]") {
    test::ProDOSDiskBuilder builder("TEST");

    ProDOSVolumeInfo info;
    bool result = ProDOS::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(info.totalBlocks == 280);
}

TEST_CASE("parseVolumeInfo file count reflects added files", "[prodos][volume]") {
    test::ProDOSDiskBuilder builder("TEST");

    const uint8_t data1[] = "FIRST FILE";
    const uint8_t data2[] = "SECOND FILE";
    builder.addFile("FILE1", 0x04, 0x0000, data1, sizeof(data1));
    builder.addFile("FILE2", 0x04, 0x0000, data2, sizeof(data2));

    ProDOSVolumeInfo info;
    bool result = ProDOS::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(info.fileCount == 2);
}

TEST_CASE("parseVolumeInfo returns false for zeroed data", "[prodos][volume]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    ProDOSVolumeInfo info;
    REQUIRE_FALSE(ProDOS::parseVolumeInfo(zeroed.data(), zeroed.size(), &info));
}

// ---------------------------------------------------------------------------
// readCatalog - Catalog reading
// ---------------------------------------------------------------------------

TEST_CASE("readCatalog returns correct file count", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("TEST");

    const uint8_t data[] = "HELLO";
    builder.addFile("GREETING", 0x04, 0x0000, data, sizeof(data));

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
}

TEST_CASE("Catalog entry has correct filename", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("TEST");

    const uint8_t data[] = "DATA";
    builder.addFile("MYPROGRAM", 0xFC, 0x0801, data, sizeof(data));

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(std::string(entries[0].filename) == "MYPROGRAM");
}

TEST_CASE("Catalog entry has correct file type", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("TEST");

    const uint8_t data[] = "CONTENT";
    builder.addFile("TEXTFILE", 0x04, 0x0000, data, sizeof(data));  // TXT

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileType == 0x04);
    CHECK(std::string(entries[0].fileTypeName) == "TXT");
}

TEST_CASE("Catalog entry has correct auxType for binary", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("TEST");

    const uint8_t data[] = {0xEA, 0x60};  // NOP RTS
    builder.addFile("CODE", 0x06, 0x2000, data, sizeof(data));  // BIN at $2000

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileType == 0x06);
    CHECK(entries[0].auxType == 0x2000);
}

TEST_CASE("readCatalog with empty disk returns zero", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("EMPTY");

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 0);
}

TEST_CASE("Multiple files in ProDOS catalog", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("MULTI");

    const uint8_t d1[] = "AAA";
    const uint8_t d2[] = "BBB";
    const uint8_t d3[] = "CCC";
    builder.addFile("ALPHA", 0x04, 0x0000, d1, sizeof(d1));
    builder.addFile("BETA", 0x06, 0x2000, d2, sizeof(d2));
    builder.addFile("GAMMA", 0xFC, 0x0801, d3, sizeof(d3));

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 3);
}

TEST_CASE("Catalog entry EOF matches data length", "[prodos][catalog]") {
    test::ProDOSDiskBuilder builder("TEST");

    std::vector<uint8_t> data(100, 0x42);
    builder.addFile("HUNDRED", 0x04, 0x0000, data.data(), static_cast<int>(data.size()));

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].eof == 100);
}

// ---------------------------------------------------------------------------
// readFile - File data retrieval
// ---------------------------------------------------------------------------

TEST_CASE("readFile retrieves correct data for seedling file", "[prodos][readFile]") {
    test::ProDOSDiskBuilder builder("TEST");

    std::vector<uint8_t> fileData(64);
    for (int i = 0; i < 64; i++) {
        fileData[i] = static_cast<uint8_t>(i);
    }
    builder.addFile("SEQDATA", 0x06, 0x0300, fileData.data(),
                     static_cast<int>(fileData.size()));

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);

    std::vector<uint8_t> outBuf(512, 0);
    int bytesRead = ProDOS::readFile(builder.data(), builder.size(),
                                      &entries[0], outBuf.data(),
                                      static_cast<int>(outBuf.size()));
    REQUIRE(bytesRead == 64);

    // Verify first bytes match
    for (int i = 0; i < 64; i++) {
        INFO("Byte at offset " << i);
        CHECK(outBuf[i] == static_cast<uint8_t>(i));
    }
}

TEST_CASE("readFile returns zero for empty entry", "[prodos][readFile]") {
    test::ProDOSDiskBuilder builder("TEST");

    ProDOSCatalogEntry emptyEntry;
    memset(&emptyEntry, 0, sizeof(emptyEntry));

    std::vector<uint8_t> outBuf(512, 0);
    int bytesRead = ProDOS::readFile(builder.data(), builder.size(),
                                      &emptyEntry, outBuf.data(),
                                      static_cast<int>(outBuf.size()));
    CHECK(bytesRead == 0);
}

// ---------------------------------------------------------------------------
// mapFileTypeForViewer - File type mapping
// ---------------------------------------------------------------------------

TEST_CASE("mapFileTypeForViewer maps TXT to text viewer", "[prodos][fileType]") {
    // TXT (0x04) should map to text viewer type (0)
    int viewerType = ProDOS::mapFileTypeForViewer(0x04);
    CHECK(viewerType == 0);
}

TEST_CASE("mapFileTypeForViewer maps BIN to hex viewer", "[prodos][fileType]") {
    // BIN (0x06) should map to hex/binary viewer type (4)
    int viewerType = ProDOS::mapFileTypeForViewer(0x06);
    CHECK(viewerType == 4);
}

TEST_CASE("mapFileTypeForViewer maps BAS to BASIC viewer", "[prodos][fileType]") {
    // BAS (0xFC) should map to BASIC viewer type (2)
    int viewerType = ProDOS::mapFileTypeForViewer(0xFC);
    CHECK(viewerType == 2);
}

TEST_CASE("mapFileTypeForViewer returns -1 for unknown type", "[prodos][fileType]") {
    // Unknown/unmapped type should return -1
    int viewerType = ProDOS::mapFileTypeForViewer(0x0F);
    CHECK(viewerType == -1);
}

TEST_CASE("mapFileTypeForViewer maps SYS to binary viewer", "[prodos][fileType]") {
    // SYS (0xFF) should map to binary viewer type (4)
    int sysType = ProDOS::mapFileTypeForViewer(0xFF);
    CHECK(sysType == 4);
}

TEST_CASE("mapFileTypeForViewer maps INT to Integer BASIC viewer", "[prodos][fileType]") {
    // INT (0xFA) -> Integer BASIC viewer (1)
    int intType = ProDOS::mapFileTypeForViewer(0xFA);
    CHECK(intType == 1);
}
