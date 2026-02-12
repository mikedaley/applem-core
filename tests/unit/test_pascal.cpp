/*
 * test_pascal.cpp - Unit tests for Apple Pascal filesystem parser
 *
 * Tests the Pascal filesystem reader including:
 * - Format detection (isPascal)
 * - Volume information parsing
 * - Catalog reading
 * - File data retrieval
 * - File type mapping for viewer
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "pascal.hpp"
#include "disk_image_builder.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// isPascal - Format detection
// ---------------------------------------------------------------------------

TEST_CASE("isPascal returns true for valid Pascal image", "[pascal][detection]") {
    test::PascalDiskBuilder builder("PASCAL");
    REQUIRE(Pascal::isPascal(builder.data(), builder.size()));
}

TEST_CASE("isPascal returns false for zeroed data", "[pascal][detection]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    REQUIRE_FALSE(Pascal::isPascal(zeroed.data(), zeroed.size()));
}

TEST_CASE("isPascal returns false for too-small data", "[pascal][detection]") {
    std::vector<uint8_t> small(512, 0x00);
    REQUIRE_FALSE(Pascal::isPascal(small.data(), small.size()));
}

TEST_CASE("isPascal returns false for null pointer", "[pascal][detection]") {
    REQUIRE_FALSE(Pascal::isPascal(nullptr, 0));
}

// ---------------------------------------------------------------------------
// parseVolumeInfo - Volume information
// ---------------------------------------------------------------------------

TEST_CASE("parseVolumeInfo returns correct volume name", "[pascal][volume]") {
    test::PascalDiskBuilder builder("MYDISC");

    PascalVolumeInfo info;
    bool result = Pascal::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(std::string(info.volumeName) == "MYDISC");
}

TEST_CASE("parseVolumeInfo returns correct total blocks", "[pascal][volume]") {
    test::PascalDiskBuilder builder("TEST");

    PascalVolumeInfo info;
    bool result = Pascal::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(info.totalBlocks == 280);
}

TEST_CASE("parseVolumeInfo file count reflects added files", "[pascal][volume]") {
    test::PascalDiskBuilder builder("TEST");

    const uint8_t data1[] = "FIRST";
    const uint8_t data2[] = "SECOND";
    builder.addFile("FILE1", 2, data1, sizeof(data1));  // 2 = Code file
    builder.addFile("FILE2", 3, data2, sizeof(data2));  // 3 = Text file

    PascalVolumeInfo info;
    bool result = Pascal::parseVolumeInfo(builder.data(), builder.size(), &info);
    REQUIRE(result);
    CHECK(info.fileCount == 2);
}

TEST_CASE("parseVolumeInfo returns false for zeroed data", "[pascal][volume]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    PascalVolumeInfo info;
    REQUIRE_FALSE(Pascal::parseVolumeInfo(zeroed.data(), zeroed.size(), &info));
}

// ---------------------------------------------------------------------------
// readCatalog - Catalog reading
// ---------------------------------------------------------------------------

TEST_CASE("readCatalog returns correct file count", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("TEST");

    const uint8_t data[] = "HELLO PASCAL";
    builder.addFile("GREETING", 3, data, sizeof(data));  // 3 = Text

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
}

TEST_CASE("Catalog entry has correct filename", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("TEST");

    const uint8_t data[] = "DATA";
    builder.addFile("MYCODE", 2, data, sizeof(data));  // 2 = Code

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(std::string(entries[0].filename) == "MYCODE");
}

TEST_CASE("Catalog entry has correct file type name", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("TEST");

    const uint8_t data[] = "CONTENT";
    builder.addFile("SOURCE", 3, data, sizeof(data));  // 3 = Text

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileType == 3);
    CHECK(std::string(entries[0].fileTypeName) == "TEXT");
}

TEST_CASE("readCatalog with empty disk returns zero", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("EMPTY");

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 0);
}

TEST_CASE("Multiple files in Pascal catalog", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("MULTI");

    const uint8_t d1[] = "AAA";
    const uint8_t d2[] = "BBB";
    const uint8_t d3[] = "CCC";
    builder.addFile("ALPHA", 3, d1, sizeof(d1));     // Text
    builder.addFile("BETA", 2, d2, sizeof(d2));      // Code
    builder.addFile("GAMMA", 5, d3, sizeof(d3));     // Data

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 3);
}

TEST_CASE("Catalog entry has valid block range", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("TEST");

    const uint8_t data[] = "BLOCK DATA";
    builder.addFile("BLKFILE", 2, data, sizeof(data));  // Code

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].startBlock >= 6);   // First data block is after directory
    CHECK(entries[0].nextBlock > entries[0].startBlock);
}

TEST_CASE("Catalog entry fileSize is computed correctly", "[pascal][catalog]") {
    test::PascalDiskBuilder builder("TEST");

    // Create a file smaller than one block
    std::vector<uint8_t> data(100, 0x42);
    builder.addFile("SMALL", 5, data.data(), static_cast<int>(data.size()));

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileSize == 100);
}

// ---------------------------------------------------------------------------
// readFile - File data retrieval
// ---------------------------------------------------------------------------

TEST_CASE("readFile retrieves correct data", "[pascal][readFile]") {
    test::PascalDiskBuilder builder("TEST");

    std::vector<uint8_t> fileData(64);
    for (int i = 0; i < 64; i++) {
        fileData[i] = static_cast<uint8_t>(i);
    }
    builder.addFile("SEQDATA", 5, fileData.data(), static_cast<int>(fileData.size()));

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);

    std::vector<uint8_t> outBuf(1024, 0);
    int bytesRead = Pascal::readFile(builder.data(), builder.size(),
                                      &entries[0], outBuf.data(),
                                      static_cast<int>(outBuf.size()));
    REQUIRE(bytesRead > 0);

    // Verify the data bytes match
    for (int i = 0; i < 64; i++) {
        INFO("Byte at offset " << i);
        CHECK(outBuf[i] == static_cast<uint8_t>(i));
    }
}

TEST_CASE("readFile with code file type", "[pascal][readFile]") {
    test::PascalDiskBuilder builder("TEST");

    std::vector<uint8_t> codeData = {0xEA, 0xEA, 0x60, 0x00};  // NOP NOP RTS BRK
    builder.addFile("PROG", 2, codeData.data(), static_cast<int>(codeData.size()));  // 2 = Code

    PascalCatalogEntry entries[32];
    int count = Pascal::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);

    std::vector<uint8_t> outBuf(1024, 0);
    int bytesRead = Pascal::readFile(builder.data(), builder.size(),
                                      &entries[0], outBuf.data(),
                                      static_cast<int>(outBuf.size()));
    REQUIRE(bytesRead >= 4);
    CHECK(outBuf[0] == 0xEA);
    CHECK(outBuf[1] == 0xEA);
    CHECK(outBuf[2] == 0x60);
    CHECK(outBuf[3] == 0x00);
}

// ---------------------------------------------------------------------------
// mapFileTypeForViewer - File type mapping
// ---------------------------------------------------------------------------

TEST_CASE("mapFileTypeForViewer maps Text type", "[pascal][fileType]") {
    // Pascal file type 3 = TEXT -> text viewer (0x00)
    int viewerType = Pascal::mapFileTypeForViewer(3);
    CHECK(viewerType == 0x00);  // Text viewer
}

TEST_CASE("mapFileTypeForViewer maps Code type to hex dump", "[pascal][fileType]") {
    // Pascal file type 2 = CODE -> not mapped (-1, hex dump)
    int viewerType = Pascal::mapFileTypeForViewer(2);
    CHECK(viewerType == -1);
}

TEST_CASE("mapFileTypeForViewer returns -1 for unknown type", "[pascal][fileType]") {
    int viewerType = Pascal::mapFileTypeForViewer(99);
    CHECK(viewerType == -1);
}
