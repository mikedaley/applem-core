/*
 * test_disk35.cpp - The Apple 3.5" drive and the IWM's 3.5" port
 *
 * The recording (zones, interleave, the 524-byte sector and its checksum),
 * the drive's sixteen status bits and its control strobes, and the IWM's own
 * data path: a read in latch mode holds a byte until it is taken, and an
 * asynchronous write empties a byte buffer onto the disk and says so through
 * the handshake register. test_iigs_boot runs the firmware over all of it.
 */
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "cards/iwm/iwm.hpp"
#include "cards/iwm/sony_drive.hpp"
#include "disk-image/disk_inspection.hpp"
#include "disk-image/gcr35.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

namespace {

std::vector<uint8_t> patternedImage(size_t size) {
  std::vector<uint8_t> image(size);
  for (size_t i = 0; i < size; i++) image[i] = static_cast<uint8_t>(i * 13 + i / 512 * 7);
  return image;
}

// A drive's status bit, through the selector: CA2 CA1 CA0 SEL.
bool status(SonyDrive &drive, uint8_t selector, uint64_t now = 0) {
  drive.setLine(0, (selector >> 1) & 1, now);
  drive.setLine(1, (selector >> 2) & 1, now);
  drive.setLine(2, (selector >> 3) & 1, now);
  drive.setSel(selector & 1);
  return drive.sense(now);
}

void control(SonyDrive &drive, uint8_t selector, uint64_t now = 0) {
  status(drive, selector, now);
  drive.setLine(3, true, now);
  drive.setLine(3, false, now);
}

} // namespace

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

TEST_CASE("A 3.5\" disk has five zones and its blocks run track by track, side by side",
          "[disk35][gcr]") {
  CHECK(GCR35::sectorsOnTrack(0) == 12);
  CHECK(GCR35::sectorsOnTrack(15) == 12);
  CHECK(GCR35::sectorsOnTrack(16) == 11);
  CHECK(GCR35::sectorsOnTrack(79) == 8);
  CHECK(GCR35::blockCount(2) == 1600);
  CHECK(GCR35::blockCount(1) == 800);
  // Track 0 side 0 is blocks 0-11, side 1 is 12-23, track 1 starts at 24.
  CHECK(GCR35::blockFor(0, 1, 0, 2) == 12);
  CHECK(GCR35::blockFor(1, 0, 0, 2) == 24);
  CHECK(GCR35::blockFor(16, 0, 0, 2) == 384);
  CHECK(GCR35::blockFor(79, 1, 7, 2) == 1599);
  CHECK(GCR35::blockFor(0, 1, 0, 1) == -1); // a 400K disk has no side 1
}

TEST_CASE("A 3.5\" sector is 703 nibbles and decodes back to its 524 bytes",
          "[disk35][gcr]") {
  uint8_t raw[GCR35::RAW_SECTOR_BYTES];
  for (int i = 0; i < GCR35::RAW_SECTOR_BYTES; i++) raw[i] = static_cast<uint8_t>(i * 37 + 5);
  uint8_t nibbles[GCR35::ENCODED_SECTOR_NIBBLES];
  GCR35::encodeSector(raw, nibbles);
  for (uint8_t n : nibbles) REQUIRE((n & 0x80) != 0);

  uint8_t back[GCR35::RAW_SECTOR_BYTES];
  REQUIRE(GCR35::decodeSector(nibbles, back));
  REQUIRE(std::memcmp(raw, back, sizeof(raw)) == 0);

  // One wrong nibble and the checksum says so.
  nibbles[300] = nibbles[300] == 0x96 ? 0x97 : 0x96;
  REQUIRE_FALSE(GCR35::decodeSector(nibbles, back));
}

TEST_CASE("Every track of an 800K image encodes and reads back", "[disk35][gcr]") {
  const auto image = patternedImage(GCR35::SIZE_800K);
  std::vector<uint8_t> back(image.size(), 0);
  std::vector<uint8_t> bits;
  for (int track = 0; track < GCR35::TRACKS; track++) {
    for (int side = 0; side < 2; side++) {
      const uint32_t count = GCR35::encodeTrack(image.data(), track, side, 2, bits);
      REQUIRE(GCR35::decodeTrack(bits.data(), count, track, side, 2, back.data()) ==
              GCR35::sectorsOnTrack(track));
    }
  }
  REQUIRE(back == image);
}

TEST_CASE("The Disk Inspector reads a 3.5\" track's fields the 3.5\" way", "[disk35][inspect]") {
  const auto image = patternedImage(GCR35::SIZE_800K);
  std::vector<uint8_t> bits;
  const uint32_t count = GCR35::encodeTrack(image.data(), 20, 1, 2, bits);
  const auto analysis = inspect::analyzeTrack(bits.data(), count, inspect::Recording::ThreeAndAHalf);
  REQUIRE(analysis.sectors.size() == 11);
  for (const inspect::Sector &s : analysis.sectors) {
    CHECK(s.address_ok);
    CHECK(s.data == inspect::DataState::Good);
    CHECK(s.track == 20);
    CHECK(s.volume == 1); // the side
    // The first 256 bytes of the block it holds.
    const size_t block = static_cast<size_t>(GCR35::blockFor(20, 1, s.sector, 2));
    CHECK(std::memcmp(s.bytes.data(), image.data() + block * 512, 256) == 0);
  }
  // Read as a 5.25" track not one address field verifies, so the track is in
  // a format the 5.25" rules do not know: no sectors, nothing called bad.
  const auto wrong = inspect::analyzeTrack(bits.data(), count);
  CHECK(wrong.sectors.empty());
  for (const inspect::Nibble &n : wrong.nibbles) CHECK((n.kind & inspect::BAD) == 0);
}

TEST_CASE("A 3.5\" overview gives each track two rings, one side at a time", "[disk35][inspect]") {
  SonyDrive drive;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(drive.insert(image.data(), image.size(), "a.po"));
  const auto overview = drive.overview(0, 64);
  REQUIRE(overview.size() == 16u + 160u * (12u + 64u * 3u));
  const size_t record = 12 + 64 * 3;
  // Rings 0 and 1 are track 0, ring 158 and 159 track 79: 12 and 8 sectors,
  // every one of them good.
  CHECK(overview[16 + 0 * record + 2] == 12);
  CHECK(overview[16 + 1 * record + 2] == 12);
  CHECK(overview[16 + 0 * record + 3] == 12);
  CHECK(overview[16 + 159 * record + 2] == 8);
  CHECK(overview[16 + 159 * record + 4] == 0); // no bad address fields
}

// ---------------------------------------------------------------------------
// The drive
// ---------------------------------------------------------------------------

TEST_CASE("A 3.5\" drive takes WOZ, 400K, 800K and 2MG, and nothing else",
          "[disk35][drive]") {
  SonyDrive drive;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(drive.insert(image.data(), image.size(), "a.po"));

  const auto small = patternedImage(GCR35::SIZE_400K);
  REQUIRE(drive.insert(small.data(), small.size(), "b.dsk"));

  // A 2MG's header and anything after its data go back out with it.
  std::vector<uint8_t> twoimg(64, 0);
  std::memcpy(twoimg.data(), "2IMG", 4);
  twoimg[0x0C] = 1; // ProDOS order
  twoimg[0x18] = 64;
  const uint32_t length = static_cast<uint32_t>(image.size());
  for (int i = 0; i < 4; i++) twoimg[0x1C + i] = static_cast<uint8_t>(length >> (8 * i));
  twoimg.insert(twoimg.end(), image.begin(), image.end());
  twoimg.push_back('!');
  REQUIRE(drive.insert(twoimg.data(), twoimg.size(), "c.2mg"));
  size_t size = 0;
  const uint8_t *saved = drive.exportData(&size);
  REQUIRE(size == twoimg.size());
  REQUIRE(std::memcmp(saved, twoimg.data(), size) == 0);

  const std::vector<uint8_t> floppy(143360, 0);
  REQUIRE_FALSE(drive.insert(floppy.data(), floppy.size(), "d.po"));
  REQUIRE_FALSE(SonyDrive::isDiskImage(floppy.data(), floppy.size()));
}

TEST_CASE("A 3.5\" drive answers its status bits", "[disk35][drive]") {
  SonyDrive drive;
  drive.setEnabled(true, 0);
  CHECK(status(drive, 0x1));        // no disk in place
  CHECK_FALSE(status(drive, 0xF));  // installed
  CHECK(status(drive, 0xC));        // double-sided
  CHECK_FALSE(status(drive, 0x5));  // at track 0
  CHECK(status(drive, 0x4));        // spindle off

  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(drive.insert(image.data(), image.size(), "a.po"));
  CHECK_FALSE(status(drive, 0x1));  // a disk in place
  CHECK(status(drive, 0x3));        // write-enabled
  CHECK(status(drive, 0xD));        // not ready: the spindle is off

  control(drive, 0x4);              // spindle on
  CHECK_FALSE(status(drive, 0x4));
  CHECK(status(drive, 0xD, 10));    // spinning up
  CHECK_FALSE(status(drive, 0xD, 100000));
}

TEST_CASE("A 3.5\" drive steps, chooses a head and ejects", "[disk35][drive]") {
  SonyDrive drive;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(drive.insert(image.data(), image.size(), "a.po"));

  // Deaf until selected.
  control(drive, 0x2);
  REQUIRE(drive.track() == 0);

  drive.setEnabled(true, 0);
  control(drive, 0x0);              // inward
  control(drive, 0x2, 0);
  REQUIRE(drive.track() == 1);
  CHECK_FALSE(status(drive, 0x2, 1));   // stepping
  CHECK(status(drive, 0x2, 100000));    // done
  CHECK(status(drive, 0x5));            // not at track 0
  control(drive, 0x8);              // outward
  control(drive, 0x2);
  control(drive, 0x2);              // stops at track 0
  REQUIRE(drive.track() == 0);

  status(drive, 0x9);               // the upper head's data line selects it
  REQUIRE(drive.side() == 1);
  status(drive, 0x8);
  REQUIRE(drive.side() == 0);

  // Eject: the disk leaves, and is kept for the host to save.
  control(drive, 0xE);
  REQUIRE_FALSE(drive.hasDisk());
  REQUIRE(drive.hasEjected());
  size_t size = 0;
  REQUIRE(drive.exportEjected(&size) != nullptr);
  REQUIRE(size == image.size());
  drive.clearEjected();
  REQUIRE_FALSE(drive.hasEjected());
}

TEST_CASE("A 3.5\" drive stops its spindle half a second after it is deselected",
          "[disk35][drive]") {
  SonyDrive drive;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(drive.insert(image.data(), image.size(), "a.po"));
  drive.setEnabled(true, 0);
  control(drive, 0x4);
  drive.setEnabled(false, 1000);
  CHECK(drive.isMotorOn(1000 + 400000));
  CHECK_FALSE(drive.isMotorOn(1000 + 600000));
}

// ---------------------------------------------------------------------------
// The IWM's 3.5" port
// ---------------------------------------------------------------------------

namespace {

struct Port {
  IWM iwm;
  uint64_t cycle = 0;
  Port() {
    iwm.setCycleCallback([this]() { return cycle; });
    iwm.reset();
  }
  uint8_t read(uint8_t offset) { return iwm.readIO(offset); }
  void write(uint8_t offset, uint8_t value = 0) { iwm.writeIO(offset, value); }
  // The firmware's way in: the 3.5" port, mode $0F, drive 1 on, spindle on.
  void start() {
    iwm.setDiskRegister(0x40);
    write(0x08);       // ENABLE off, so the mode register takes a byte
    read(0x0D);        // Q6 on
    write(0x0F, 0x0F); // Q7 on: mode $0F
    read(0x0E);        // Q7 off
    read(0x0C);        // Q6 off
    read(0x0A);        // drive 1
    read(0x09);        // ENABLE
    // CONT35 $08: CA1 on, CA0, SEL and CA2 off, then the strobe.
    read(0x00);
    read(0x03);
    read(0x04);
    read(0x07);
    read(0x06);
    cycle += 200000; // spun up
  }
};

} // namespace

TEST_CASE("The IWM's status register reads the 3.5\" drive's selector", "[disk35][iwm]") {
  Port port;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(port.iwm.sonyDrive(0).insert(image.data(), image.size(), "a.po"));
  port.start();
  REQUIRE(port.iwm.getModeRegister() == 0x0F);

  // Disk in place: CA2 CA1 CA0 SEL = 0001, so SEL on through $C031 bit 7.
  port.read(0x00);
  port.read(0x02);
  port.read(0x04);
  port.iwm.setDiskRegister(0xC0);
  port.read(0x0D); // Q6 on, Q7 off: status
  REQUIRE((port.read(0x0E) & 0x80) == 0); // a disk is in place
  REQUIRE((port.read(0x0E) & 0x20) != 0); // the drive is enabled
  port.iwm.sonyDrive(0).eject();
  REQUIRE((port.read(0x0E) & 0x80) != 0);
}

TEST_CASE("The IWM latches a whole 3.5\" byte until it is read", "[disk35][iwm]") {
  Port port;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(port.iwm.sonyDrive(0).insert(image.data(), image.size(), "a.po"));
  port.start();

  // Read for a while, one poll every few cycles: every byte read has its top
  // bit set, a byte is never read twice, and the address marks go past.
  std::vector<uint8_t> bytes;
  for (int i = 0; i < 40000; i++) {
    port.cycle += 3;
    const uint8_t value = port.read(0x0C);
    if (value & 0x80) bytes.push_back(value);
  }
  int marks = 0;
  for (size_t i = 2; i < bytes.size(); i++) {
    if (bytes[i - 2] == 0xD5 && bytes[i - 1] == 0xAA && bytes[i] == 0x96) marks++;
  }
  // 120,000 cycles is 60,000 cells: most of a turn of a 12-sector track.
  REQUIRE(marks >= 8);
  // A byte every 16 cycles or so, each read exactly once.
  REQUIRE(bytes.size() > 6000);
  REQUIRE(bytes.size() < 8000);
}

TEST_CASE("The IWM writes a 3.5\" byte at a time and reports the underrun", "[disk35][iwm]") {
  Port port;
  const auto image = patternedImage(GCR35::SIZE_800K);
  REQUIRE(port.iwm.sonyDrive(0).insert(image.data(), image.size(), "a.po"));
  port.start();

  // The handshake register is Q6 off with Q7 still on: a read of $C0EC.
  port.read(0x0D);           // Q6 on
  port.write(0x0F, 0xFF);    // Q7 on, the first byte
  REQUIRE((port.read(0x0C) & 0xC0) == 0x40); // the buffer is full, no underrun

  port.cycle += 2;           // the byte goes into the shifter
  REQUIRE((port.read(0x0C) & 0xC0) == 0xC0); // ready for the next
  port.read(0x0D);
  port.write(0x0F, 0xD5);
  port.read(0x0C);
  port.cycle += 40;          // the shifter empties with nothing waiting
  REQUIRE((port.read(0x0C) & 0x40) == 0);    // underrun
  REQUIRE(port.iwm.sonyDrive(0).isModified());
}
