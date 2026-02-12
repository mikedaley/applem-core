/*
 * test_dos33.cpp - Unit tests for DOS 3.3 filesystem parser
 *
 * Tests the DOS 3.3 filesystem reader including:
 * - Format detection (isDOS33)
 * - Catalog reading
 * - File data retrieval
 * - Binary file header extraction
 * - Multiple files in catalog
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "dos33.hpp"
#include "disk_image_builder.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace a2e;

// ---------------------------------------------------------------------------
// isDOS33 - Format detection
// ---------------------------------------------------------------------------

TEST_CASE("isDOS33 returns true for valid DOS 3.3 image", "[dos33][detection]") {
    test::DOS33DiskBuilder builder;
    REQUIRE(DOS33::isDOS33(builder.data(), builder.size()));
}

TEST_CASE("isDOS33 returns false for zeroed data", "[dos33][detection]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    REQUIRE_FALSE(DOS33::isDOS33(zeroed.data(), zeroed.size()));
}

TEST_CASE("isDOS33 returns false for too-small data", "[dos33][detection]") {
    std::vector<uint8_t> small(1024, 0x00);
    REQUIRE_FALSE(DOS33::isDOS33(small.data(), small.size()));
}

TEST_CASE("isDOS33 returns false for null pointer", "[dos33][detection]") {
    REQUIRE_FALSE(DOS33::isDOS33(nullptr, 0));
}

// ---------------------------------------------------------------------------
// readCatalog - Catalog reading
// ---------------------------------------------------------------------------

TEST_CASE("readCatalog returns correct file count after addFile", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t fileData[] = "HELLO WORLD";
    builder.addFile("TESTFILE", 0x00, fileData, sizeof(fileData));

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
}

TEST_CASE("Catalog entry has correct filename", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t fileData[] = "CONTENT";
    builder.addFile("MYFILE", 0x00, fileData, sizeof(fileData));

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    REQUIRE(std::string(entries[0].filename) == "MYFILE");
}

TEST_CASE("Catalog entry has correct file type for text file", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t fileData[] = "TEXT DATA";
    builder.addFile("README", 0x00, fileData, sizeof(fileData));  // 0x00 = Text

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileType == 0x00);
    CHECK(std::string(entries[0].fileTypeName) == "T");
}

TEST_CASE("Catalog entry has correct file type for binary file", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t fileData[] = {0x00, 0x20, 0x05, 0x00, 0xEA, 0xEA, 0xEA, 0x60, 0x00};
    builder.addFile("BINFILE", 0x04, fileData, sizeof(fileData));  // 0x04 = Binary

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].fileType == 0x04);
    CHECK(std::string(entries[0].fileTypeName) == "B");
}

TEST_CASE("Catalog entry locked status is correctly set", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t data1[] = "UNLOCKED";
    const uint8_t data2[] = "LOCKED";
    builder.addFile("FREE", 0x00, data1, sizeof(data1), false);
    builder.addFile("PROT", 0x00, data2, sizeof(data2), true);

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 2);

    // Find each entry by name
    bool foundFree = false, foundProt = false;
    for (int i = 0; i < count; i++) {
        if (std::string(entries[i].filename) == "FREE") {
            CHECK_FALSE(entries[i].isLocked);
            foundFree = true;
        }
        if (std::string(entries[i].filename) == "PROT") {
            CHECK(entries[i].isLocked);
            foundProt = true;
        }
    }
    REQUIRE(foundFree);
    REQUIRE(foundProt);
}

TEST_CASE("readCatalog with empty disk returns zero", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 0);
}

// ---------------------------------------------------------------------------
// Multiple files in catalog
// ---------------------------------------------------------------------------

TEST_CASE("Multiple files in catalog are all enumerated", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t data1[] = "FILE ONE DATA";
    const uint8_t data2[] = "FILE TWO DATA";
    const uint8_t data3[] = "FILE THREE DATA";

    builder.addFile("FILE1", 0x00, data1, sizeof(data1));
    builder.addFile("FILE2", 0x02, data2, sizeof(data2));    // 0x02 = Applesoft
    builder.addFile("FILE3", 0x04, data3, sizeof(data3));    // 0x04 = Binary

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 3);

    // Verify filenames are present (order may vary)
    bool found1 = false, found2 = false, found3 = false;
    for (int i = 0; i < count; i++) {
        std::string name(entries[i].filename);
        if (name == "FILE1") found1 = true;
        if (name == "FILE2") found2 = true;
        if (name == "FILE3") found3 = true;
    }
    CHECK(found1);
    CHECK(found2);
    CHECK(found3);
}

TEST_CASE("Catalog entries have non-zero sector count", "[dos33][catalog]") {
    test::DOS33DiskBuilder builder;

    const uint8_t fileData[] = "SOME DATA CONTENT";
    builder.addFile("HASDATA", 0x00, fileData, sizeof(fileData));

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].sectorCount > 0);
}

// ---------------------------------------------------------------------------
// readFile - File data retrieval
// ---------------------------------------------------------------------------

TEST_CASE("readFile retrieves file data correctly", "[dos33][readFile]") {
    test::DOS33DiskBuilder builder;

    // Create a file with known content
    std::vector<uint8_t> fileData(64);
    for (int i = 0; i < 64; i++) {
        fileData[i] = static_cast<uint8_t>(i);
    }
    builder.addFile("SEQDATA", 0x00, fileData.data(), static_cast<int>(fileData.size()));

    // Read catalog to get the file's first track/sector
    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(builder.data(), builder.size(), entries, 32);
    REQUIRE(count == 1);

    // Read the file data
    std::vector<uint8_t> outBuf(4096, 0);
    int bytesRead = DOS33::readFile(builder.data(), builder.size(),
                                     entries[0].firstTrack, entries[0].firstSector,
                                     outBuf.data(), static_cast<int>(outBuf.size()));
    REQUIRE(bytesRead > 0);

    // Verify the data is present in the output
    // The raw file data starts from the first data sector
    bool found = false;
    for (int offset = 0; offset <= bytesRead - 64; offset++) {
        if (memcmp(&outBuf[offset], fileData.data(), 64) == 0) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("readFile returns zero for invalid track/sector", "[dos33][readFile]") {
    test::DOS33DiskBuilder builder;

    std::vector<uint8_t> outBuf(4096, 0);
    int bytesRead = DOS33::readFile(builder.data(), builder.size(),
                                     0, 0, outBuf.data(),
                                     static_cast<int>(outBuf.size()));
    CHECK(bytesRead == 0);
}

// ---------------------------------------------------------------------------
// getBinaryFileInfo - Binary file header extraction
// ---------------------------------------------------------------------------

TEST_CASE("getBinaryFileInfo extracts address and length from binary header", "[dos33][binaryInfo]") {
    // Binary file format: first 4 bytes are [addrLo, addrHi, lenLo, lenHi]
    uint8_t binaryFile[] = {
        0x00, 0x20,  // Load address = $2000
        0x05, 0x00,  // Length = 5 bytes
        0xEA, 0xEA, 0xEA, 0xEA, 0x60  // NOP NOP NOP NOP RTS
    };

    uint16_t address = 0, length = 0;
    bool result = DOS33::getBinaryFileInfo(binaryFile, sizeof(binaryFile),
                                            &address, &length);
    REQUIRE(result);
    CHECK(address == 0x2000);
    CHECK(length == 5);
}

TEST_CASE("getBinaryFileInfo returns false for too-small data", "[dos33][binaryInfo]") {
    uint8_t tinyData[] = {0x00, 0x20};

    uint16_t address = 0, length = 0;
    bool result = DOS33::getBinaryFileInfo(tinyData, sizeof(tinyData),
                                            &address, &length);
    CHECK_FALSE(result);
}

TEST_CASE("getBinaryFileInfo with various load addresses", "[dos33][binaryInfo]") {
    SECTION("Zero page address") {
        uint8_t data[] = {0x00, 0x00, 0x10, 0x00, 0x00};
        uint16_t addr, len;
        REQUIRE(DOS33::getBinaryFileInfo(data, sizeof(data), &addr, &len));
        CHECK(addr == 0x0000);
        CHECK(len == 0x0010);
    }

    SECTION("High memory address") {
        uint8_t data[] = {0x00, 0xBF, 0x00, 0x01, 0x00};
        uint16_t addr, len;
        REQUIRE(DOS33::getBinaryFileInfo(data, sizeof(data), &addr, &len));
        CHECK(addr == 0xBF00);
        CHECK(len == 0x0100);
    }
}
