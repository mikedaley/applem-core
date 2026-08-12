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

// ---------------------------------------------------------------------------
// writeFile - Writing
// ---------------------------------------------------------------------------

namespace {

const ProDOSCatalogEntry* findEntry(const ProDOSCatalogEntry* entries, int count,
                                    const char* name) {
    for (int i = 0; i < count; i++) {
        if (std::strcmp(entries[i].filename, name) == 0) return &entries[i];
    }
    return nullptr;
}

int countFreeBlocks(const std::vector<uint8_t>& disk) {
    // The builder writes ProDOS-order images, so block 6 is at a plain offset
    const uint8_t* bitmap = disk.data() + 6 * 512;
    int free = 0;
    for (int b = 0; b < 280; b++) {
        if ((bitmap[b / 8] >> (7 - (b % 8))) & 1) free++;
    }
    return free;
}

} // namespace

TEST_CASE("writeFile stores a seedling file readable by the parser", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> payload(300);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>(i & 0xFF);

    REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "OBJ.CODE", 0x06, 0x2000,
                              payload.data(), static_cast<uint32_t>(payload.size()))
            == FsWriteStatus::OK);

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(disk.data(), disk.size(), entries, 32);
    const ProDOSCatalogEntry* entry = findEntry(entries, count, "OBJ.CODE");
    REQUIRE(entry != nullptr);
    CHECK(entry->fileType == 0x06);
    CHECK(std::strcmp(entry->fileTypeName, "BIN") == 0);
    CHECK(entry->auxType == 0x2000);
    CHECK(entry->storageType == 1); // Seedling
    CHECK(entry->blocksUsed == 1);
    CHECK(entry->eof == payload.size());
    CHECK_FALSE(entry->isLocked);

    std::vector<uint8_t> readBack(4096);
    int bytes = ProDOS::readFile(disk.data(), disk.size(), entry, readBack.data(),
                                 static_cast<int>(readBack.size()));
    REQUIRE(bytes == static_cast<int>(payload.size()));
    CHECK(std::memcmp(readBack.data(), payload.data(), payload.size()) == 0);

    ProDOSVolumeInfo info;
    REQUIRE(ProDOS::parseVolumeInfo(disk.data(), disk.size(), &info));
    CHECK(info.fileCount == 1);
}

TEST_CASE("writeFile stores a sapling file readable by the parser", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> payload(5000);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>((i * 7) & 0xFF);

    REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "BIG", 0x06, 0x0300,
                              payload.data(), static_cast<uint32_t>(payload.size()))
            == FsWriteStatus::OK);

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(disk.data(), disk.size(), entries, 32);
    const ProDOSCatalogEntry* entry = findEntry(entries, count, "BIG");
    REQUIRE(entry != nullptr);
    CHECK(entry->storageType == 2);   // Sapling
    CHECK(entry->blocksUsed == 11);   // 10 data blocks + 1 index block

    std::vector<uint8_t> readBack(8192);
    int bytes = ProDOS::readFile(disk.data(), disk.size(), entry, readBack.data(),
                                 static_cast<int>(readBack.size()));
    REQUIRE(bytes == static_cast<int>(payload.size()));
    CHECK(std::memcmp(readBack.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("writeFile works on a DOS-ordered ProDOS image", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> prodosOrder = builder.build();

    // Re-lay the image in DOS sector order, the way a .dsk of a ProDOS volume
    // is stored, and check the writer follows the reader's order detection.
    static const uint8_t PRODOS_TO_DOS[16] = {0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15};
    std::vector<uint8_t> dosOrder(prodosOrder.size(), 0);
    for (int track = 0; track < 35; track++) {
        for (int s = 0; s < 16; s++) {
            std::memcpy(&dosOrder[(track * 16 + PRODOS_TO_DOS[s]) * 256],
                        &prodosOrder[(track * 16 + s) * 256], 256);
        }
    }
    REQUIRE(ProDOS::isProDOS(dosOrder.data(), dosOrder.size()));

    std::vector<uint8_t> payload(900, 0x5A);
    REQUIRE(ProDOS::writeFile(dosOrder.data(), dosOrder.size(), "OBJ", 0x06, 0x0800,
                              payload.data(), static_cast<uint32_t>(payload.size()))
            == FsWriteStatus::OK);

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(dosOrder.data(), dosOrder.size(), entries, 32);
    const ProDOSCatalogEntry* entry = findEntry(entries, count, "OBJ");
    REQUIRE(entry != nullptr);

    std::vector<uint8_t> readBack(4096);
    int bytes = ProDOS::readFile(dosOrder.data(), dosOrder.size(), entry,
                                 readBack.data(), static_cast<int>(readBack.size()));
    REQUIRE(bytes == static_cast<int>(payload.size()));
    CHECK(std::memcmp(readBack.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("writeFile replaces a file of the same name without leaking blocks",
          "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> first(5000, 0xAA);
    std::vector<uint8_t> second(100, 0x55);

    REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "OBJ", 0x06, 0x0300,
                              first.data(), static_cast<uint32_t>(first.size()))
            == FsWriteStatus::OK);
    REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "OBJ", 0x06, 0x0300,
                              second.data(), static_cast<uint32_t>(second.size()))
            == FsWriteStatus::OK);
    int afterSecond = countFreeBlocks(disk);

    for (int i = 0; i < 5; i++) {
        REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "OBJ", 0x06, 0x0300,
                                  second.data(), static_cast<uint32_t>(second.size()))
                == FsWriteStatus::OK);
    }
    CHECK(countFreeBlocks(disk) == afterSecond);

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(disk.data(), disk.size(), entries, 32);
    CHECK(count == 1);

    ProDOSVolumeInfo info;
    REQUIRE(ProDOS::parseVolumeInfo(disk.data(), disk.size(), &info));
    CHECK(info.fileCount == 1);
}

TEST_CASE("writeFile uppercases names and rejects unusable ones", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();
    const uint8_t body[] = {1, 2, 3};

    REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), "obj.code", 0x06, 0, body, 3)
            == FsWriteStatus::OK);

    ProDOSCatalogEntry entries[32];
    int count = ProDOS::readCatalog(disk.data(), disk.size(), entries, 32);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ.CODE") == 0);

    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "", 0x06, 0, body, 3)
          == FsWriteStatus::InvalidName);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "9LIVES", 0x06, 0, body, 3)
          == FsWriteStatus::InvalidName);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "HAS SPACE", 0x06, 0, body, 3)
          == FsWriteStatus::InvalidName);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "SIXTEENCHARSXXX1", 0x06, 0, body, 3)
          == FsWriteStatus::InvalidName);
}

TEST_CASE("writeFile refuses to replace a locked file", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    const uint8_t body[] = {1, 2, 3};
    builder.addFile("LOCKED", 0x06, 0x0300, body, sizeof(body));
    std::vector<uint8_t> disk = builder.build();

    // Clear the write-enable bit the way ProDOS's LOCK does
    disk[2 * 512 + 4 + 0x27 + 0x1E] = 0x01;

    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "LOCKED", 0x06, 0x0300, body, 3)
          == FsWriteStatus::FileLocked);
}

TEST_CASE("writeFile reports a full disk and leaves the bitmap intact", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();

    // Leave two free blocks
    uint8_t* bitmap = disk.data() + 6 * 512;
    for (int i = 0; i < 35; i++) bitmap[i] = 0x00;
    bitmap[2] = 0xC0; // blocks 16 and 17

    std::vector<uint8_t> before = disk;
    std::vector<uint8_t> big(20000, 0x11);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "TOOBIG", 0x06, 0,
                            big.data(), static_cast<uint32_t>(big.size()))
          == FsWriteStatus::DiskFull);
    CHECK(disk == before);

    std::vector<uint8_t> small(400, 0x22);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "FITS", 0x06, 0,
                            small.data(), static_cast<uint32_t>(small.size()))
          == FsWriteStatus::OK);
}

TEST_CASE("writeFile rejects files past sapling size", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();

    std::vector<uint8_t> huge(256 * 512 + 1, 0x00);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "HUGE", 0x06, 0,
                            huge.data(), static_cast<uint32_t>(huge.size()))
          == FsWriteStatus::FileTooLarge);
}

TEST_CASE("writeFile rejects unformatted and undersized images", "[prodos][write]") {
    const uint8_t body[] = {1};
    std::vector<uint8_t> zeroed(143360, 0x00);
    CHECK(ProDOS::writeFile(zeroed.data(), zeroed.size(), "OBJ", 0x06, 0, body, 1)
          == FsWriteStatus::NotFormatted);

    std::vector<uint8_t> small(1024, 0x00);
    CHECK(ProDOS::writeFile(small.data(), small.size(), "OBJ", 0x06, 0, body, 1)
          == FsWriteStatus::ImageTooSmall);
}

TEST_CASE("writeFile fills the volume directory then reports it full", "[prodos][write]") {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();
    const uint8_t body[] = {1, 2, 3};

    // The volume directory is four blocks: 12 entries in the first (the header
    // takes one slot) plus 13 in each of the other three.
    for (int i = 0; i < 51; i++) {
        std::string name = "FILE" + std::to_string(i);
        REQUIRE(ProDOS::writeFile(disk.data(), disk.size(), name.c_str(), 0x06, 0, body, 3)
                == FsWriteStatus::OK);
    }
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "ONEMORE", 0x06, 0, body, 3)
          == FsWriteStatus::DirectoryFull);
    CHECK(ProDOS::writeFile(disk.data(), disk.size(), "FILE7", 0x06, 0, body, 3)
          == FsWriteStatus::OK);
}
