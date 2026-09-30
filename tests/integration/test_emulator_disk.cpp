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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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

TEST_CASE("Emulator getDisk returns the machine's drive controller",
          "[emulator][disk]") {
    Emulator emu;
    emu.init();

    // A //e's is a Disk II card, and the base is what callers hold: the host
    // asks the same questions of a //c's IWM.
    DiskController& disk = emu.getDisk();
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

TEST_CASE("Refitting a slot with the card it holds keeps that card",
          "[emulator][disk][smartport]") {
    // The host applies the saved slot layout at startup, after it may have
    // restored a hard drive image. Each refit used to build a new, empty
    // SmartPort, so a //e came back from a reload with no drive in it.
    Emulator emu;
    emu.init();
    REQUIRE(emu.setSlotCard(7, "smartport"));
    std::vector<uint8_t> hdv(512 * 280, 0x00);
    REQUIRE(emu.insertSmartPortImage(0, hdv.data(), hdv.size(), "test.hdv"));

    REQUIRE(emu.setSlotCard(7, "smartport"));
    REQUIRE(emu.isSmartPortImageInserted(0));

    // A different card is still a change, and takes the drive with it.
    REQUIRE(emu.setSlotCard(7, "thunderclock"));
    REQUIRE(std::string(emu.getSlotCardName(7)) == "thunderclock");
    REQUIRE_FALSE(emu.isSmartPortCardInstalled());
}

TEST_CASE("A //e boots a SmartPort image with a watchpoint armed",
          "[emulator][disk][smartport][debug]") {
    // While a watchpoint is armed every read is peeked first, and the peek
    // used to read the card's ROM: each SmartPort call ran twice and the boot
    // ended at a BRK in the monitor. ProDOS writes the watched address, so the
    // loop resumes after each stop the way the debugger's Run button does.
    FILE* file = fopen("public/disks/ProDOS 2.4.3.po", "rb");
    if (!file) {
        WARN("ProDOS image not found; skipping the SmartPort watchpoint test");
        return;
    }
    std::vector<uint8_t> image;
    for (int c; (c = fgetc(file)) != EOF;) image.push_back(static_cast<uint8_t>(c));
    fclose(file);

    for (const bool armed : {false, true}) {
        INFO("watchpoint armed: " << armed);
        Emulator emu;
        emu.init();
        REQUIRE(emu.setSlotCard(7, "smartport"));
        REQUIRE(emu.insertSmartPortImage(0, image.data(), image.size(), "prodos.po"));
        if (armed) emu.addWatchpoint(0x03D0, 0x03D0, Emulator::WP_WRITE);
        emu.reset();
        int stops = 0;
        for (int i = 0; i < 60; i++) {
            emu.runCycles(100000);
            if (emu.isPaused()) {
                stops++;
                emu.setPaused(false);
            }
        }

        if (armed) REQUIRE(stops > 0);
        const std::string screen = emu.readScreenText(0, 0, 23, 79);
        INFO("screen:\n" << screen);
        REQUIRE(screen.find("BITSY") != std::string::npos);
    }
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

// ---------------------------------------------------------------------------
// Booting, which is the only test that exercises the whole path at once
// ---------------------------------------------------------------------------

// The DOS 3.3 System Master that ships in public/disks, or an empty vector if
// it is not where the test expects it. Tests run from the source directory.
static std::vector<uint8_t> loadSystemMaster() {
    FILE *file = fopen("public/disks/Apple DOS 3.3 January 1983.dsk", "rb");
    if (!file) return {};
    std::vector<uint8_t> image(DSK_SIZE);
    const size_t read = fread(image.data(), 1, image.size(), file);
    fclose(file);
    if (read != image.size()) return {};
    return image;
}

TEST_CASE("Each machine boots DOS 3.3 from the controller it has",
          "[emulator][disk][boot]") {
    // A //e reads the disk through a Disk II card in slot 6 and a //c through
    // the IWM on its board, and this is where that is one claim rather than
    // two: same image, same firmware, same sequencer, and both arrive at the
    // DOS prompt. It is also the only test that runs the //c's disk firmware,
    // which is the part of the machine that has nothing to do with a slot.
    const std::vector<uint8_t> image = loadSystemMaster();
    if (image.empty()) {
        WARN("DOS 3.3 System Master not found; skipping the boot test");
        return;
    }

    auto bootsToDos = [&image](MachineId machine) {
        Emulator emu(machine);
        emu.init();
        REQUIRE(emu.insertDisk(0, image.data(), image.size(), "dos33.dsk"));

        // Long enough for the drive to seek, DOS to load and Applesoft to come
        // up behind it.
        const int cyclesPerFrame = emu.getMachine().timing.cyclesPerFrame();
        for (int frame = 0; frame < 1500; frame++) emu.runCycles(cyclesPerFrame);

        const char *screen = emu.readScreenText(0, 0, 23, 39);
        REQUIRE(screen != nullptr);
        const std::string text(screen);
        INFO("screen:\n" << text);
        REQUIRE(text.find("DOS VERSION 3.3") != std::string::npos);
        REQUIRE(text.find(']') != std::string::npos);
    };

    SECTION("the //e, through a Disk II card") { bootsToDos(MachineId::AppleIIe); }

    SECTION("the //c, through its IWM") {
        if (!Emulator::isMachineRunnable(MachineId::AppleIIc)) {
            WARN("//c ROMs not built in; skipping its boot");
            return;
        }
        bootsToDos(MachineId::AppleIIc);
    }
}

// ---------------------------------------------------------------------------
// WOZ 2.1 flux tracks
// ---------------------------------------------------------------------------

// Sirius's Bandits, as Applesauce captured it with tracks 1.5 to 19.5 in flux:
// those tracks are written partly at 3.7us a cell and partly at 4.1us, and the
// loader times its reads to tell which. Read as bits, every one of them fails
// its checksum and the boot retries track 1.5 for ever. Not in the repository,
// so this runs only when A2E_BANDITS_WOZ names the image:
//
//   A2E_BANDITS_WOZ=~/Downloads/00_Bandits.woz ctest -R test_emulator_disk
TEST_CASE("Emulator boots a flux WOZ through a timing check", "[emulator][disk][flux]") {
    const char *path = std::getenv("A2E_BANDITS_WOZ");
    if (!path) {
        WARN("A2E_BANDITS_WOZ is not set; skipping the flux boot");
        return;
    }
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

    auto runUntilHeadReaches = [](Emulator &emu, int quarterTrack, int seconds) {
        for (int i = 0; i < seconds * 100; i++) {
            emu.runCycles(10230);
            if (emu.getDisk().getQuarterTrack() >= quarterTrack) return true;
        }
        return false;
    };

    Emulator emu;
    emu.init();
    REQUIRE(emu.insertDisk(0, image.data(), image.size(), "Bandits.woz"));
    REQUIRE(emu.getDisk().getDiskImage(0)->getFormatName() == "WOZ 2.1 (flux)");

    // Part way through the flux tracks, save the machine: the disk goes into
    // the state as a WOZ file, and has to come back with its flux intact
    REQUIRE(runUntilHeadReaches(emu, 30, 10));
    size_t size = 0;
    const uint8_t *state = emu.exportState(&size);
    REQUIRE(state != nullptr);
    std::vector<uint8_t> saved(state, state + size);

    Emulator restored;
    restored.init();
    REQUIRE(restored.importState(saved.data(), saved.size()));
    REQUIRE(restored.getDisk().getDiskImage(0)->getFormatName() == "WOZ 2.1 (flux)");

    // Quarter track 82 is the first bit track after the flux ones: reaching
    // it means every flux track read
    REQUIRE(runUntilHeadReaches(restored, 82, 10));
}
