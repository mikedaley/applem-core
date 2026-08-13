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
#include <string>

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

// ---------------------------------------------------------------------------
// writeFile / writeBinaryFile - Writing
// ---------------------------------------------------------------------------

// The builder allocates its own files from track 20 upward without touching the
// VTOC bitmap, while the writer allocates from track 1 upward using the bitmap,
// so builder-made and writer-made files never contend for the same sectors.

TEST_CASE("writeBinaryFile round-trips through the catalog", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> payload(600);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>(i & 0xFF);

    REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "OBJ", 0x04, nullptr, 0)
            == FsWriteStatus::OK);
    REQUIRE(DOS33::writeBinaryFile(disk.data(), disk.size(), "OBJ.FILE", 0x0300,
                                   payload.data(), payload.size())
            == FsWriteStatus::OK);

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(disk.data(), disk.size(), entries, 32);
    REQUIRE(count == 2);

    const DOS33CatalogEntry* written = nullptr;
    for (int i = 0; i < count; i++) {
        if (std::strcmp(entries[i].filename, "OBJ.FILE") == 0) written = &entries[i];
    }
    REQUIRE(written != nullptr);
    CHECK(written->fileType == 0x04);
    CHECK(std::strcmp(written->fileTypeName, "B") == 0);
    CHECK_FALSE(written->isLocked);
    // 604 bytes of file = 3 data sectors + 1 T/S list sector
    CHECK(written->sectorCount == 4);

    std::vector<uint8_t> readBack(4096);
    int bytes = DOS33::readFile(disk.data(), disk.size(), written->firstTrack,
                                written->firstSector, readBack.data(),
                                static_cast<int>(readBack.size()));
    REQUIRE(bytes >= static_cast<int>(payload.size()) + 4);

    uint16_t addr = 0, len = 0;
    REQUIRE(DOS33::getBinaryFileInfo(readBack.data(), bytes, &addr, &len));
    CHECK(addr == 0x0300);
    CHECK(len == payload.size());
    CHECK(std::memcmp(readBack.data() + 4, payload.data(), payload.size()) == 0);
}

TEST_CASE("writeFile lowercases and pads the name as DOS stores it", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    const uint8_t body[] = {1, 2, 3};
    REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "lower.name", 0x00, body, sizeof(body))
            == FsWriteStatus::OK);

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(disk.data(), disk.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "LOWER.NAME") == 0);
}

TEST_CASE("writeFile replaces a file of the same name in place", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> first(1000, 0xAA);
    std::vector<uint8_t> second(200, 0x55);

    REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "OBJ", 0x04, first.data(), first.size())
            == FsWriteStatus::OK);
    REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "OBJ", 0x04, second.data(), second.size())
            == FsWriteStatus::OK);

    DOS33CatalogEntry entries[32];
    int count = DOS33::readCatalog(disk.data(), disk.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(entries[0].sectorCount == 2); // 1 data sector + 1 T/S list

    std::vector<uint8_t> readBack(4096);
    int bytes = DOS33::readFile(disk.data(), disk.size(), entries[0].firstTrack,
                                entries[0].firstSector, readBack.data(),
                                static_cast<int>(readBack.size()));
    REQUIRE(bytes >= static_cast<int>(second.size()));
    CHECK(std::memcmp(readBack.data(), second.data(), second.size()) == 0);
}

TEST_CASE("rewriting a file does not leak sectors", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    auto freeSectors = [&]() {
        const uint8_t* vtoc = disk.data() + (17 * 16 + 0) * 256;
        int free = 0;
        for (int t = 0; t < 35; t++) {
            for (int b = 0; b < 2; b++) {
                uint8_t bits = vtoc[0x38 + t * 4 + b];
                for (int i = 0; i < 8; i++) if (bits & (1 << i)) free++;
            }
        }
        return free;
    };

    std::vector<uint8_t> body(1000, 0x11);
    REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "OBJ", 0x04, body.data(), body.size())
            == FsWriteStatus::OK);
    int afterFirst = freeSectors();

    for (int i = 0; i < 5; i++) {
        REQUIRE(DOS33::writeFile(disk.data(), disk.size(), "OBJ", 0x04, body.data(), body.size())
                == FsWriteStatus::OK);
    }
    CHECK(freeSectors() == afterFirst);
}

TEST_CASE("writeFile refuses to replace a locked file", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    const uint8_t body[] = {1, 2, 3};
    builder.addFile("LOCKED", 0x04, body, sizeof(body), true);
    std::vector<uint8_t> disk = builder.build();

    CHECK(DOS33::writeFile(disk.data(), disk.size(), "LOCKED", 0x04, body, sizeof(body))
          == FsWriteStatus::FileLocked);
}

TEST_CASE("writeFile rejects unusable names", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();
    const uint8_t body[] = {1};

    CHECK(DOS33::writeFile(disk.data(), disk.size(), "", 0x04, body, 1)
          == FsWriteStatus::InvalidName);
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "   ", 0x04, body, 1)
          == FsWriteStatus::InvalidName);
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "HAS,COMMA", 0x04, body, 1)
          == FsWriteStatus::InvalidName);
    CHECK(DOS33::writeFile(disk.data(), disk.size(),
                           "THIS.NAME.IS.LONGER.THAN.THIRTY.CHARACTERS", 0x04, body, 1)
          == FsWriteStatus::InvalidName);
}

TEST_CASE("writeFile reports a full disk without disturbing the image", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    // Leave four free sectors: enough for a small file, not for a large one
    uint8_t* vtoc = disk.data() + (17 * 16 + 0) * 256;
    for (int t = 0; t < 35; t++) {
        vtoc[0x38 + t * 4 + 0] = 0x00;
        vtoc[0x38 + t * 4 + 1] = 0x00;
    }
    vtoc[0x38 + 20 * 4 + 1] = 0x0F; // sectors 0-3 of track 20

    std::vector<uint8_t> big(4000, 0x22);
    std::vector<uint8_t> before = disk;
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "TOOBIG", 0x04, big.data(), big.size())
          == FsWriteStatus::DiskFull);
    CHECK(disk == before);

    std::vector<uint8_t> small(700, 0x33);
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "FITS", 0x04, small.data(), small.size())
          == FsWriteStatus::OK);
}

TEST_CASE("writeFile rejects an unformatted image", "[dos33][write]") {
    std::vector<uint8_t> zeroed(143360, 0x00);
    const uint8_t body[] = {1};
    CHECK(DOS33::writeFile(zeroed.data(), zeroed.size(), "OBJ", 0x04, body, 1)
          == FsWriteStatus::NotFormatted);

    std::vector<uint8_t> small(1024, 0x00);
    CHECK(DOS33::writeFile(small.data(), small.size(), "OBJ", 0x04, body, 1)
          == FsWriteStatus::ImageTooSmall);
}

TEST_CASE("writeFile fills the catalog then reports it full", "[dos33][write]") {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    // The builder lays down a single catalog sector, so seven entries fill it
    const uint8_t body[] = {1, 2, 3};
    for (int i = 0; i < 7; i++) {
        std::string name = "FILE" + std::to_string(i);
        REQUIRE(DOS33::writeFile(disk.data(), disk.size(), name.c_str(), 0x04, body, sizeof(body))
                == FsWriteStatus::OK);
    }
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "ONEMORE", 0x04, body, sizeof(body))
          == FsWriteStatus::DirectoryFull);
    // An existing name still writes, since it needs no new slot
    CHECK(DOS33::writeFile(disk.data(), disk.size(), "FILE3", 0x04, body, sizeof(body))
          == FsWriteStatus::OK);
}
