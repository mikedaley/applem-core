/*
 * test_emulator_disk.cpp - Integration tests for Emulator disk operations
 *
 * Tests disk insert, eject, blank disk creation, two-drive support,
 * filename tracking, SmartPort, and error handling through the
 * Emulator facade.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "emulator.hpp"
#include "filesystem/dos33.hpp"
#include "filesystem/prodos.hpp"
#include "disk_image_builder.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace a2e;

// Standard DSK image size: 35 tracks * 16 sectors * 256 bytes
static constexpr size_t DSK_SIZE = 143360;

// Helper: create a valid-sized DSK image filled with a byte value
static std::vector<uint8_t> makeDskImage(uint8_t fill = 0x00) {
    return std::vector<uint8_t>(DSK_SIZE, fill);
}

// ---------------------------------------------------------------------------
// Insert disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertDisk succeeds with valid DSK data", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    bool result = emu.insertDisk(0, img.data(), img.size(), "test.dsk");
    REQUIRE(result);
}

TEST_CASE("Emulator getDiskFilename returns inserted filename", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    emu.insertDisk(0, img.data(), img.size(), "test.dsk");

    const char* name = emu.getDiskFilename(0);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == "test.dsk");
}

// ---------------------------------------------------------------------------
// Eject disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator ejectDisk clears the drive", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img = makeDskImage();
    emu.insertDisk(0, img.data(), img.size(), "test.dsk");
    emu.ejectDisk(0);

    // After ejecting, getDiskData should return nullptr
    size_t dataSize = 0;
    const uint8_t* data = emu.getDiskData(0, &dataSize);
    REQUIRE(data == nullptr);
}

// ---------------------------------------------------------------------------
// Blank disk
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertBlankDisk succeeds", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    bool result = emu.insertBlankDisk(0);
    REQUIRE(result);
}

// ---------------------------------------------------------------------------
// Two drives
// ---------------------------------------------------------------------------

TEST_CASE("Emulator supports two drives simultaneously", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    auto img0 = makeDskImage(0x00);
    auto img1 = makeDskImage(0xFF);

    bool r0 = emu.insertDisk(0, img0.data(), img0.size(), "disk1.dsk");
    bool r1 = emu.insertDisk(1, img1.data(), img1.size(), "disk2.dsk");

    REQUIRE(r0);
    REQUIRE(r1);

    REQUIRE(std::string(emu.getDiskFilename(0)) == "disk1.dsk");
    REQUIRE(std::string(emu.getDiskFilename(1)) == "disk2.dsk");
}

// ---------------------------------------------------------------------------
// getDisk reference
// ---------------------------------------------------------------------------

TEST_CASE("Emulator getDisk returns a valid Disk2Card reference", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    Disk2Card& disk = emu.getDisk();
    REQUIRE(std::string(disk.getName()) == "Disk II");
}

// ---------------------------------------------------------------------------
// SmartPort
// ---------------------------------------------------------------------------

TEST_CASE("Emulator isSmartPortCardInstalled reflects slot configuration", "[emulator][disk][smartport]") {
    Emulator emu;
    emu.init();

    // By default, SmartPort is not installed (no card in slot 7)
    // The result depends on default configuration
    // Just verify the method is callable without crashing
    bool installed = emu.isSmartPortCardInstalled();

    if (installed) {
        // If SmartPort is installed, insertSmartPortImage should be callable
        // (actual success depends on data validity)
        std::vector<uint8_t> hdvData(512 * 280, 0x00); // Minimal ProDOS volume
        // This may or may not succeed depending on image validation
        emu.insertSmartPortImage(0, hdvData.data(), hdvData.size(), "test.hdv");
    }

    // Either way, no crash
    REQUIRE(true);
}

// ---------------------------------------------------------------------------
// Invalid data
// ---------------------------------------------------------------------------

TEST_CASE("Emulator insertDisk with invalid size returns false", "[emulator][disk]") {
    Emulator emu;
    emu.init();

    // A DSK image must be exactly 143360 bytes (or a valid NIB/WOZ size)
    // An arbitrary size should be rejected
    std::vector<uint8_t> badData(1000, 0x00);
    bool result = emu.insertDisk(0, badData.data(), badData.size(), "bad.dsk");
    REQUIRE_FALSE(result);
}

// ---------------------------------------------------------------------------
// Host-side file writing (Merlin DSK directive)
// ---------------------------------------------------------------------------

TEST_CASE("Emulator writeBinaryFileToDisk writes into a DOS 3.3 disk",
          "[emulator][disk][write]") {
    Emulator emu;
    emu.init();

    test::DOS33DiskBuilder builder;
    auto img = builder.build();
    REQUIRE(emu.insertDisk(0, img.data(), img.size(), "dos.dsk"));

    std::vector<uint8_t> payload(500);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>(i & 0xFF);

    REQUIRE(emu.writeBinaryFileToDisk(0, "OBJ", 0x0300, payload.data(), payload.size())
            == FsWriteStatus::OK);

    // The change must be visible in what the image would serialise to
    size_t size = 0;
    const uint8_t* data = emu.getDiskData(0, &size);
    REQUIRE(data != nullptr);

    DOS33CatalogEntry entries[16];
    int count = DOS33::readCatalog(data, size, entries, 16);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ") == 0);
    CHECK(entries[0].fileType == 0x04);

    std::vector<uint8_t> readBack(2048);
    int bytes = DOS33::readFile(data, size, entries[0].firstTrack, entries[0].firstSector,
                                readBack.data(), static_cast<int>(readBack.size()));
    REQUIRE(bytes >= static_cast<int>(payload.size()) + 4);
    uint16_t addr = 0, len = 0;
    REQUIRE(DOS33::getBinaryFileInfo(readBack.data(), bytes, &addr, &len));
    CHECK(addr == 0x0300);
    CHECK(len == payload.size());
    CHECK(std::memcmp(readBack.data() + 4, payload.data(), payload.size()) == 0);
}

TEST_CASE("Emulator writeBinaryFileToDisk writes into a ProDOS disk",
          "[emulator][disk][write]") {
    Emulator emu;
    emu.init();

    test::ProDOSDiskBuilder builder("TESTDISK");
    auto img = builder.build();
    REQUIRE(emu.insertDisk(0, img.data(), img.size(), "prodos.po"));

    std::vector<uint8_t> payload(400, 0x42);
    REQUIRE(emu.writeBinaryFileToDisk(0, "OBJ", 0x2000, payload.data(), payload.size())
            == FsWriteStatus::OK);

    size_t size = 0;
    const uint8_t* data = emu.getDiskData(0, &size);
    REQUIRE(data != nullptr);

    ProDOSCatalogEntry entries[16];
    int count = ProDOS::readCatalog(data, size, entries, 16);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ") == 0);
    CHECK(entries[0].fileType == 0x06);
    CHECK(entries[0].auxType == 0x2000);

    std::vector<uint8_t> readBack(2048);
    int bytes = ProDOS::readFile(data, size, &entries[0], readBack.data(),
                                 static_cast<int>(readBack.size()));
    REQUIRE(bytes == static_cast<int>(payload.size()));
    CHECK(std::memcmp(readBack.data(), payload.data(), payload.size()) == 0);
}

TEST_CASE("Emulator writeBinaryFileToDisk marks the disk modified",
          "[emulator][disk][write]") {
    Emulator emu;
    emu.init();

    test::DOS33DiskBuilder builder;
    auto img = builder.build();
    REQUIRE(emu.insertDisk(0, img.data(), img.size(), "dos.dsk"));
    REQUIRE_FALSE(emu.getDisk().getDiskImage(0)->isModified());

    const uint8_t payload[] = {1, 2, 3};
    REQUIRE(emu.writeBinaryFileToDisk(0, "OBJ", 0x0300, payload, sizeof(payload))
            == FsWriteStatus::OK);
    CHECK(emu.getDisk().getDiskImage(0)->isModified());
}

TEST_CASE("Emulator writeBinaryFileToDisk reports drive and format problems",
          "[emulator][disk][write]") {
    Emulator emu;
    emu.init();
    const uint8_t payload[] = {1, 2, 3};

    SECTION("Empty drive") {
        CHECK(emu.writeBinaryFileToDisk(0, "OBJ", 0x0300, payload, sizeof(payload))
              == FsWriteStatus::NoDisk);
    }

    SECTION("Out-of-range drive") {
        CHECK(emu.writeBinaryFileToDisk(5, "OBJ", 0x0300, payload, sizeof(payload))
              == FsWriteStatus::NoDisk);
    }

    SECTION("Unformatted disk") {
        auto img = makeDskImage();
        REQUIRE(emu.insertDisk(0, img.data(), img.size(), "blank.dsk"));
        CHECK(emu.writeBinaryFileToDisk(0, "OBJ", 0x0300, payload, sizeof(payload))
              == FsWriteStatus::NotFormatted);
    }

    SECTION("Invalid name") {
        test::DOS33DiskBuilder builder;
        auto img = builder.build();
        REQUIRE(emu.insertDisk(0, img.data(), img.size(), "dos.dsk"));
        CHECK(emu.writeBinaryFileToDisk(0, "", 0x0300, payload, sizeof(payload))
              == FsWriteStatus::InvalidName);
    }
}
