/*
 * test_block_device.cpp - Unit tests for SmartPort BlockDevice
 */

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "block_device.hpp"

#include <cstring>
#include <vector>

using namespace a2e;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("BlockDevice default constructed is not loaded", "[blockdev][ctor]") {
    BlockDevice dev;
    CHECK(dev.isLoaded() == false);
    CHECK(dev.getTotalBlocks() == 0);
    CHECK(dev.isModified() == false);
    CHECK(dev.isWriteProtected() == false);
    CHECK(dev.getFilename().empty());
}

// ============================================================================
// load with valid HDV data
// ============================================================================

TEST_CASE("BlockDevice load with raw block data", "[blockdev][load]") {
    BlockDevice dev;

    // Create a minimal HDV image: 4 blocks = 2048 bytes
    const size_t numBlocks = 4;
    const size_t dataSize = numBlocks * BlockDevice::BLOCK_SIZE;
    std::vector<uint8_t> data(dataSize, 0x00);

    // Fill first block with a pattern
    for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    bool loaded = dev.load(data.data(), data.size(), "test.hdv");
    REQUIRE(loaded == true);

    CHECK(dev.isLoaded() == true);
    CHECK(dev.getTotalBlocks() == numBlocks);
    CHECK(dev.getFilename() == "test.hdv");
    CHECK(dev.isModified() == false);
}

TEST_CASE("BlockDevice load with zero-size data fails", "[blockdev][load]") {
    BlockDevice dev;
    bool loaded = dev.load(nullptr, 0, "empty.hdv");
    CHECK(loaded == false);
    CHECK(dev.isLoaded() == false);
}

TEST_CASE("BlockDevice load with non-block-aligned size", "[blockdev][load]") {
    BlockDevice dev;
    // 1000 bytes is not a multiple of 512
    std::vector<uint8_t> data(1000, 0x00);
    bool loaded = dev.load(data.data(), data.size(), "weird.hdv");
    // Implementation may reject or truncate - just verify no crash
    // If it loads, totalBlocks should reflect the truncated count
    if (loaded) {
        CHECK(dev.getTotalBlocks() == 1); // 1000/512 = 1 full block
    }
}

// ============================================================================
// readBlock
// ============================================================================

TEST_CASE("BlockDevice readBlock reads correct data", "[blockdev][readBlock]") {
    BlockDevice dev;

    const size_t numBlocks = 8;
    std::vector<uint8_t> data(numBlocks * BlockDevice::BLOCK_SIZE, 0x00);

    // Put distinctive pattern in block 3
    for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
        data[3 * BlockDevice::BLOCK_SIZE + i] = static_cast<uint8_t>(0xAA);
    }

    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    uint8_t buffer[BlockDevice::BLOCK_SIZE];

    // Read block 3
    bool ok = dev.readBlock(3, buffer);
    REQUIRE(ok);
    for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
        CHECK(buffer[i] == 0xAA);
    }

    // Read block 0 (should be all zeros)
    ok = dev.readBlock(0, buffer);
    REQUIRE(ok);
    for (size_t i = 0; i < BlockDevice::BLOCK_SIZE; i++) {
        CHECK(buffer[i] == 0x00);
    }
}

TEST_CASE("BlockDevice readBlock out of range fails", "[blockdev][readBlock]") {
    BlockDevice dev;

    std::vector<uint8_t> data(4 * BlockDevice::BLOCK_SIZE, 0x00);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    uint8_t buffer[BlockDevice::BLOCK_SIZE];
    bool ok = dev.readBlock(100, buffer); // Way out of range
    CHECK(ok == false);
}

// ============================================================================
// writeBlock
// ============================================================================

TEST_CASE("BlockDevice writeBlock writes and reads back", "[blockdev][writeBlock]") {
    BlockDevice dev;

    std::vector<uint8_t> data(8 * BlockDevice::BLOCK_SIZE, 0x00);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    uint8_t writeData[BlockDevice::BLOCK_SIZE];
    memset(writeData, 0x55, sizeof(writeData));

    bool ok = dev.writeBlock(2, writeData);
    REQUIRE(ok);

    // Read it back
    uint8_t readBuf[BlockDevice::BLOCK_SIZE];
    ok = dev.readBlock(2, readBuf);
    REQUIRE(ok);

    CHECK(memcmp(writeData, readBuf, BlockDevice::BLOCK_SIZE) == 0);
}

TEST_CASE("BlockDevice writeBlock out of range fails", "[blockdev][writeBlock]") {
    BlockDevice dev;

    std::vector<uint8_t> data(4 * BlockDevice::BLOCK_SIZE, 0x00);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    uint8_t writeData[BlockDevice::BLOCK_SIZE];
    memset(writeData, 0xFF, sizeof(writeData));

    bool ok = dev.writeBlock(100, writeData);
    CHECK(ok == false);
}

// ============================================================================
// isModified after writeBlock
// ============================================================================

TEST_CASE("BlockDevice isModified is true after writeBlock", "[blockdev][modified]") {
    BlockDevice dev;

    std::vector<uint8_t> data(4 * BlockDevice::BLOCK_SIZE, 0x00);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    CHECK(dev.isModified() == false);

    uint8_t writeData[BlockDevice::BLOCK_SIZE];
    memset(writeData, 0x42, sizeof(writeData));
    dev.writeBlock(0, writeData);

    CHECK(dev.isModified() == true);
}

// ============================================================================
// eject
// ============================================================================

TEST_CASE("BlockDevice eject clears state", "[blockdev][eject]") {
    BlockDevice dev;

    std::vector<uint8_t> data(8 * BlockDevice::BLOCK_SIZE, 0x00);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    CHECK(dev.isLoaded() == true);

    dev.eject();

    CHECK(dev.isLoaded() == false);
    CHECK(dev.getTotalBlocks() == 0);
    CHECK(dev.isModified() == false);
    CHECK(dev.getFilename().empty());
}

// ============================================================================
// exportData
// ============================================================================

TEST_CASE("BlockDevice exportData returns valid pointer and size", "[blockdev][export]") {
    BlockDevice dev;

    const size_t numBlocks = 4;
    std::vector<uint8_t> data(numBlocks * BlockDevice::BLOCK_SIZE, 0xAB);
    REQUIRE(dev.load(data.data(), data.size(), "test.hdv"));

    size_t exportSize = 0;
    const uint8_t* exported = dev.exportData(&exportSize);

    REQUIRE(exported != nullptr);
    REQUIRE(exportSize > 0);
}

TEST_CASE("BlockDevice exportData on unloaded device returns null", "[blockdev][export]") {
    BlockDevice dev;
    size_t exportSize = 0;
    const uint8_t* exported = dev.exportData(&exportSize);
    CHECK(exported == nullptr);
    CHECK(exportSize == 0);
}

// ============================================================================
// getFilename after load
// ============================================================================

TEST_CASE("BlockDevice getFilename returns loaded filename", "[blockdev][filename]") {
    BlockDevice dev;

    std::vector<uint8_t> data(2 * BlockDevice::BLOCK_SIZE, 0x00);

    dev.load(data.data(), data.size(), "mydisk.po");
    CHECK(dev.getFilename() == "mydisk.po");

    dev.load(data.data(), data.size(), "another.2mg");
    CHECK(dev.getFilename() == "another.2mg");
}

TEST_CASE("BlockDevice getFilename empty when not loaded", "[blockdev][filename]") {
    BlockDevice dev;
    CHECK(dev.getFilename().empty());
}

// ============================================================================
// Multiple load/eject cycles
// ============================================================================

TEST_CASE("BlockDevice can be reloaded after eject", "[blockdev][lifecycle]") {
    BlockDevice dev;

    std::vector<uint8_t> data1(4 * BlockDevice::BLOCK_SIZE, 0x11);
    REQUIRE(dev.load(data1.data(), data1.size(), "first.hdv"));
    CHECK(dev.getTotalBlocks() == 4);

    dev.eject();
    CHECK(dev.isLoaded() == false);

    std::vector<uint8_t> data2(8 * BlockDevice::BLOCK_SIZE, 0x22);
    REQUIRE(dev.load(data2.data(), data2.size(), "second.hdv"));
    CHECK(dev.getTotalBlocks() == 8);
    CHECK(dev.getFilename() == "second.hdv");
}

TEST_CASE("A DOS-ordered 2MG is read and written through the interleave", "[blockdev][2mg]") {
  // A 140K disk in DOS 3.3 sector order: each sector filled with its track
  // and DOS sector number. ProDOS block 0 is ProDOS sectors 0 and 1 of track
  // 0, which DOS order keeps in its sectors 0 and 14; read as ProDOS order,
  // the block came back as DOS sectors 0 and 1, scrambled.
  constexpr size_t SECTOR = 256, TRACK = 16 * SECTOR, TRACKS = 35;
  std::vector<uint8_t> file(64 + TRACKS * TRACK, 0);
  std::memcpy(file.data(), "2IMG", 4);
  file[8] = 64;                                  // header size
  file[12] = 0;                                  // DOS order
  const uint32_t length = TRACKS * TRACK;
  file[24] = 64;                                 // data offset
  for (int i = 0; i < 4; i++) file[28 + i] = static_cast<uint8_t>(length >> (8 * i));
  for (size_t t = 0; t < TRACKS; t++) {
    for (size_t s = 0; s < 16; s++) {
      std::memset(&file[64 + t * TRACK + s * SECTOR], static_cast<int>(t * 16 + s), SECTOR);
    }
  }
  BlockDevice dev;
  REQUIRE(dev.load(file.data(), file.size(), "dos.2mg"));
  REQUIRE(dev.getTotalBlocks() == 280);
  uint8_t block[512];
  REQUIRE(dev.readBlock(0, block));
  REQUIRE(block[0] == 0);         // track 0, DOS sector 0
  REQUIRE(block[256] == 14);      // track 0, DOS sector 14
  REQUIRE(dev.readBlock(9, block)); // track 1, ProDOS sectors 2 and 3
  REQUIRE(block[0] == 16 + 13);
  REQUIRE(block[256] == 16 + 12);

  // A write lands where the read came from, and the file keeps its order.
  std::memset(block, 0xAB, 256);
  std::memset(block + 256, 0xCD, 256);
  REQUIRE(dev.writeBlock(9, block));
  size_t size = 0;
  const uint8_t *out = dev.exportData(&size);
  REQUIRE(size == file.size());
  REQUIRE(out[64 + TRACK + 13 * SECTOR] == 0xAB);
  REQUIRE(out[64 + TRACK + 12 * SECTOR] == 0xCD);
  REQUIRE(out[64 + TRACK + 2 * SECTOR] == 16 + 2); // untouched
}
