/*
 * test_disk_converter.cpp - Unit tests for disk image format conversion
 *
 * Tests converting a loaded image between the formats the save dialog offers:
 * - DOS 3.3 sector order (.dsk/.do)
 * - ProDOS sector order (.po)
 * - WOZ 2.0 bit stream
 *
 * The round trips are checked by reading the converted image back through the
 * real filesystem parsers and the real WOZ loader, so a conversion only counts
 * as correct if the emulator itself can read what came out.
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "disk_converter.hpp"
#include "dsk_disk_image.hpp"
#include "woz_disk_image.hpp"
#include "dos33.hpp"
#include "prodos.hpp"
#include "disk_image_builder.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace a2e;

namespace {

// A DOS 3.3 image carrying one known file.
//
// The catalog is chained across track 17 the way DOS's INIT lays it out
// (sector 15 down to sector 1). The builder writes only the first catalog
// sector, which is enough to read a catalog but leaves the sector order
// undecidable — sectors 0 and 15 sit at the same file offset in both orders,
// so a one-sector chain looks identical either way.
std::vector<uint8_t> makeDOS33Disk() {
    test::DOS33DiskBuilder builder;
    std::vector<uint8_t> disk = builder.build();

    for (int sector = 15; sector >= 1; sector--) {
        size_t offset = static_cast<size_t>(17 * 16 + sector) * 256;
        disk[offset + 0x01] = (sector > 1) ? 17 : 0;
        disk[offset + 0x02] = (sector > 1) ? static_cast<uint8_t>(sector - 1) : 0;
    }

    std::vector<uint8_t> payload(700);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>(i & 0xFF);
    DOS33::writeBinaryFile(disk.data(), disk.size(), "OBJ", 0x0300,
                           payload.data(), payload.size());
    return disk;
}

// A ProDOS-order image carrying one known file
std::vector<uint8_t> makeProDOSDisk() {
    test::ProDOSDiskBuilder builder("TESTDISK");
    std::vector<uint8_t> disk = builder.build();
    std::vector<uint8_t> payload(700, 0x5A);
    ProDOS::writeFile(disk.data(), disk.size(), "OBJ", 0x06, 0x0300,
                      payload.data(), static_cast<uint32_t>(payload.size()));
    return disk;
}

} // namespace

// ---------------------------------------------------------------------------
// Sector reordering
// ---------------------------------------------------------------------------

TEST_CASE("reorderSectors is reversible", "[converter][order]") {
    std::vector<uint8_t> original = makeDOS33Disk();

    std::vector<uint8_t> prodosOrder;
    DiskConverter::reorderSectors(original.data(), original.size(), true, prodosOrder);
    REQUIRE(prodosOrder.size() == original.size());
    CHECK(prodosOrder != original);

    std::vector<uint8_t> backToDOS;
    DiskConverter::reorderSectors(prodosOrder.data(), prodosOrder.size(), false, backToDOS);
    CHECK(backToDOS == original);
}

// ---------------------------------------------------------------------------
// Native format reporting
// ---------------------------------------------------------------------------

TEST_CASE("nativeFormat reports the format an image came from", "[converter][format]") {
    SECTION("DOS-ordered DSK") {
        auto disk = makeDOS33Disk();
        DskDiskImage image;
        REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));
        CHECK(DiskConverter::nativeFormat(image) == DiskSaveFormat::DOSOrder);
    }

    SECTION("ProDOS-ordered image") {
        auto disk = makeProDOSDisk();
        DskDiskImage image;
        REQUIRE(image.load(disk.data(), disk.size(), "test.po"));
        REQUIRE(image.getFormat() == DiskImage::Format::PO);
        CHECK(DiskConverter::nativeFormat(image) == DiskSaveFormat::ProDOSOrder);
    }

    SECTION("WOZ") {
        WozDiskImage image;
        image.createBlank();
        CHECK(DiskConverter::nativeFormat(image) == DiskSaveFormat::WOZ);
    }
}

// ---------------------------------------------------------------------------
// Sector format conversions
// ---------------------------------------------------------------------------

TEST_CASE("Converting a DOS-ordered image to DOS order returns it unchanged",
          "[converter][dsk]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    std::vector<uint8_t> out;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::DOSOrder, out));
    CHECK(out == disk);
}

TEST_CASE("A DOS 3.3 disk saved as ProDOS order round-trips", "[converter][dsk]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    std::vector<uint8_t> po;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::ProDOSOrder, po));
    REQUIRE(po.size() == disk.size());

    // The bytes moved. Sectors 0 and 15 of each track map to themselves, so a
    // DOS-order reader still finds the VTOC and the first catalog sector — but
    // the file's own sectors have moved, so what it reads back is not the file.
    CHECK(po != disk);
    DOS33CatalogEntry misread[16];
    REQUIRE(DOS33::readCatalog(po.data(), po.size(), misread, 16) == 1);
    std::vector<uint8_t> wrong(4096);
    DOS33::readFile(po.data(), po.size(), misread[0].firstTrack,
                    misread[0].firstSector, wrong.data(),
                    static_cast<int>(wrong.size()));
    uint16_t addr = 0, len = 0;
    DOS33::getBinaryFileInfo(wrong.data(), wrong.size(), &addr, &len);
    CHECK_FALSE((addr == 0x0300 && len == 700));

    // Loading it back as a ProDOS-order image and converting to DOS order
    // returns exactly the disk we started with
    DskDiskImage reloaded;
    REQUIRE(reloaded.loadAs(po.data(), po.size(), "test.po", DiskImage::Format::PO));
    std::vector<uint8_t> back;
    REQUIRE(DiskConverter::convert(reloaded, DiskSaveFormat::DOSOrder, back));
    CHECK(back == disk);
    CHECK(DOS33::isDOS33(back.data(), back.size()));
}

TEST_CASE("A ProDOS volume converts between both sector orders", "[converter][prodos]") {
    auto disk = makeProDOSDisk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.po"));

    // ProDOS order out: the file is the one that went in
    std::vector<uint8_t> po;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::ProDOSOrder, po));
    CHECK(po == disk);

    // DOS order out: still a ProDOS volume, and the parser follows it there
    std::vector<uint8_t> dos;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::DOSOrder, dos));
    REQUIRE(ProDOS::isProDOS(dos.data(), dos.size()));

    ProDOSVolumeInfo info;
    REQUIRE(ProDOS::parseVolumeInfo(dos.data(), dos.size(), &info));
    CHECK(info.useDOSSectorOrder);
    CHECK(std::strcmp(info.volumeName, "TESTDISK") == 0);

    ProDOSCatalogEntry entries[16];
    int count = ProDOS::readCatalog(dos.data(), dos.size(), entries, 16);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ") == 0);
}

// ---------------------------------------------------------------------------
// WOZ conversion
// ---------------------------------------------------------------------------

TEST_CASE("A DOS 3.3 disk saved as WOZ loads and decodes back", "[converter][woz]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    std::vector<uint8_t> woz;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::WOZ, woz));
    REQUIRE(woz.size() > 1536);
    CHECK(std::memcmp(woz.data(), "WOZ2", 4) == 0);

    // The real loader must accept it
    WozDiskImage loaded;
    REQUIRE(loaded.load(woz.data(), woz.size(), "converted.woz"));
    CHECK(loaded.getFormat() == DiskImage::Format::WOZ2);

    // And its decoded sectors must be the disk we started from
    size_t size = 0;
    const uint8_t* sectors = loaded.getSectorData(&size);
    REQUIRE(sectors != nullptr);
    REQUIRE(size == disk.size());
    CHECK(std::memcmp(sectors, disk.data(), disk.size()) == 0);

    DOS33CatalogEntry entries[16];
    int count = DOS33::readCatalog(sectors, size, entries, 16);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ") == 0);
}

TEST_CASE("A ProDOS-ordered disk saved as WOZ decodes to the same volume",
          "[converter][woz]") {
    auto disk = makeProDOSDisk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.po"));

    std::vector<uint8_t> woz;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::WOZ, woz));

    WozDiskImage loaded;
    REQUIRE(loaded.load(woz.data(), woz.size(), "converted.woz"));

    // A WOZ holds no sector order of its own — it decodes to DOS order, which
    // for a ProDOS volume is exactly the DOS-ordered form of the same disk.
    size_t size = 0;
    const uint8_t* sectors = loaded.getSectorData(&size);
    REQUIRE(sectors != nullptr);

    std::vector<uint8_t> expected;
    DiskConverter::reorderSectors(disk.data(), disk.size(), false, expected);
    CHECK(std::memcmp(sectors, expected.data(), expected.size()) == 0);

    ProDOSCatalogEntry entries[16];
    int count = ProDOS::readCatalog(sectors, size, entries, 16);
    REQUIRE(count == 1);
    CHECK(std::strcmp(entries[0].filename, "OBJ") == 0);
}

TEST_CASE("A WOZ saved as WOZ is the native export", "[converter][woz]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    std::vector<uint8_t> woz;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::WOZ, woz));

    WozDiskImage loaded;
    REQUIRE(loaded.load(woz.data(), woz.size(), "converted.woz"));

    std::vector<uint8_t> again;
    REQUIRE(DiskConverter::convert(loaded, DiskSaveFormat::WOZ, again));

    size_t nativeSize = 0;
    const uint8_t* native = loaded.exportData(&nativeSize);
    REQUIRE(native != nullptr);
    REQUIRE(again.size() == nativeSize);
    CHECK(std::memcmp(again.data(), native, nativeSize) == 0);
}

TEST_CASE("A WOZ converts back to a sector image", "[converter][woz]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    std::vector<uint8_t> woz;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::WOZ, woz));

    WozDiskImage loaded;
    REQUIRE(loaded.load(woz.data(), woz.size(), "converted.woz"));

    std::vector<uint8_t> dsk;
    REQUIRE(DiskConverter::convert(loaded, DiskSaveFormat::DOSOrder, dsk));
    CHECK(dsk == disk);

    std::vector<uint8_t> po;
    REQUIRE(DiskConverter::convert(loaded, DiskSaveFormat::ProDOSOrder, po));
    std::vector<uint8_t> expected;
    DiskConverter::reorderSectors(disk.data(), disk.size(), true, expected);
    CHECK(po == expected);
}

// ---------------------------------------------------------------------------
// canConvert
// ---------------------------------------------------------------------------

TEST_CASE("canConvert refuses formats an image cannot honour", "[converter][limits]") {
    SECTION("A sector image converts to anything") {
        auto disk = makeDOS33Disk();
        DskDiskImage image;
        REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));
        CHECK(DiskConverter::canConvert(image, DiskSaveFormat::DOSOrder));
        CHECK(DiskConverter::canConvert(image, DiskSaveFormat::ProDOSOrder));
        CHECK(DiskConverter::canConvert(image, DiskSaveFormat::WOZ));
    }

    SECTION("An undecodable WOZ offers only WOZ") {
        // A blank WOZ has no address fields at all, so nothing decodes
        WozDiskImage blank;
        blank.createBlank();
        CHECK(DiskConverter::canConvert(blank, DiskSaveFormat::WOZ));
        CHECK_FALSE(DiskConverter::canConvert(blank, DiskSaveFormat::DOSOrder));
        CHECK_FALSE(DiskConverter::canConvert(blank, DiskSaveFormat::ProDOSOrder));

        std::vector<uint8_t> out;
        CHECK_FALSE(DiskConverter::convert(blank, DiskSaveFormat::DOSOrder, out));
    }

    SECTION("An empty drive converts to nothing") {
        DskDiskImage empty;
        CHECK_FALSE(DiskConverter::canConvert(empty, DiskSaveFormat::WOZ));
        std::vector<uint8_t> out;
        CHECK_FALSE(DiskConverter::convert(empty, DiskSaveFormat::WOZ, out));
    }
}

// ---------------------------------------------------------------------------
// Reloading what was saved
// ---------------------------------------------------------------------------

TEST_CASE("A DOS 3.3 disk saved as ProDOS order is detected as such on reload",
          "[converter][detection]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));
    REQUIRE(image.getFormat() == DiskImage::Format::DSK);

    std::vector<uint8_t> po;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::ProDOSOrder, po));

    // The VTOC sits at the same file offset in both orders, so detection has
    // to follow the catalog chain to tell them apart. Nothing but the content
    // is available here — the name deliberately says nothing.
    DskDiskImage reloaded;
    REQUIRE(reloaded.load(po.data(), po.size(), "saved"));
    CHECK(reloaded.getFormat() == DiskImage::Format::PO);

    // And so what comes back out is the disk that went in
    std::vector<uint8_t> back;
    REQUIRE(DiskConverter::convert(reloaded, DiskSaveFormat::DOSOrder, back));
    CHECK(back == disk);
}

TEST_CASE("A DOS-ordered image is still detected as DOS order", "[converter][detection]") {
    auto disk = makeDOS33Disk();

    DskDiskImage byName;
    REQUIRE(byName.load(disk.data(), disk.size(), "test.po"));
    // Content wins over a misleading extension
    CHECK(byName.getFormat() == DiskImage::Format::DSK);

    DskDiskImage noName;
    REQUIRE(noName.load(disk.data(), disk.size(), ""));
    CHECK(noName.getFormat() == DiskImage::Format::DSK);
}

TEST_CASE("Every save format survives a round trip through the loader",
          "[converter][detection]") {
    auto disk = makeDOS33Disk();
    DskDiskImage image;
    REQUIRE(image.load(disk.data(), disk.size(), "test.dsk"));

    for (auto format : {DiskSaveFormat::DOSOrder, DiskSaveFormat::ProDOSOrder}) {
        std::vector<uint8_t> saved;
        REQUIRE(DiskConverter::convert(image, format, saved));

        DskDiskImage reloaded;
        REQUIRE(reloaded.load(saved.data(), saved.size(), "saved"));
        CHECK(DiskConverter::nativeFormat(reloaded) == format);

        std::vector<uint8_t> back;
        REQUIRE(DiskConverter::convert(reloaded, DiskSaveFormat::DOSOrder, back));
        CHECK(back == disk);
    }

    std::vector<uint8_t> woz;
    REQUIRE(DiskConverter::convert(image, DiskSaveFormat::WOZ, woz));
    WozDiskImage reloadedWoz;
    REQUIRE(reloadedWoz.load(woz.data(), woz.size(), "saved.woz"));
    std::vector<uint8_t> backFromWoz;
    REQUIRE(DiskConverter::convert(reloadedWoz, DiskSaveFormat::DOSOrder, backFromWoz));
    CHECK(backFromWoz == disk);
}
