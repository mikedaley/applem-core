/*
 * disk_image_builder.cpp - Programmatic disk image builders
 */

#include "disk_image_builder.hpp"
#include <algorithm>
#include <cstring>

namespace test {

// ==================== DOS33DiskBuilder ====================

DOS33DiskBuilder::DOS33DiskBuilder() : data_(DISK_SIZE, 0) {
    initVTOC();
    initCatalog();
}

int DOS33DiskBuilder::getSectorOffset(int track, int sector) const {
    return (track * SECTORS + sector) * SECTOR_SIZE;
}

void DOS33DiskBuilder::initVTOC() {
    // VTOC is at track 17, sector 0
    int offset = getSectorOffset(17, 0);

    data_[offset + 0x01] = 17;  // Catalog track
    data_[offset + 0x02] = 15;  // Catalog sector
    data_[offset + 0x03] = 3;   // DOS version (3.3)
    data_[offset + 0x06] = 254; // Volume number
    data_[offset + 0x27] = 122; // Max track/sector pairs per TS list
    data_[offset + 0x30] = 17;  // Last track allocated
    data_[offset + 0x31] = 1;   // Direction of allocation (+1)
    data_[offset + 0x34] = 35;  // Number of tracks
    data_[offset + 0x35] = 16;  // Sectors per track
    data_[offset + 0x36] = 0;   // Bytes per sector low
    data_[offset + 0x37] = 1;   // Bytes per sector high (256)

    // Free sector bitmap - mark all sectors as free
    // Each track gets 4 bytes starting at offset 0x38
    for (int t = 0; t < 35; t++) {
        int bitmapOffset = offset + 0x38 + t * 4;
        data_[bitmapOffset + 0] = 0xFF;  // Sectors 0-7 free
        data_[bitmapOffset + 1] = 0xFF;  // Sectors 8-15 free
        data_[bitmapOffset + 2] = 0x00;
        data_[bitmapOffset + 3] = 0x00;
    }

    // Mark VTOC track (17) sectors as used
    int vtocBitmap = offset + 0x38 + 17 * 4;
    data_[vtocBitmap + 0] = 0x00;
    data_[vtocBitmap + 1] = 0x00;
}

void DOS33DiskBuilder::initCatalog() {
    // First catalog sector: track 17, sector 15
    // Just leave it zeroed - that marks it as having no entries
    // The next catalog link is already 0,0 which means end of catalog
}

bool DOS33DiskBuilder::allocateSector(int& track, int& sector) {
    if (nextAllocTrack_ >= TRACKS) return false;
    track = nextAllocTrack_;
    sector = nextAllocSector_;
    nextAllocSector_++;
    if (nextAllocSector_ >= SECTORS) {
        nextAllocSector_ = 0;
        nextAllocTrack_++;
        if (nextAllocTrack_ == 17) nextAllocTrack_ = 18; // Skip catalog track
    }
    return true;
}

void DOS33DiskBuilder::writeSector(int track, int sector, const uint8_t* sdata, int len) {
    int offset = getSectorOffset(track, sector);
    int copyLen = std::min(len, SECTOR_SIZE);
    std::memcpy(&data_[offset], sdata, copyLen);
}

bool DOS33DiskBuilder::addFile(const std::string& name, uint8_t fileType,
                                const uint8_t* fileData, int dataLen, bool locked) {
    // Allocate a track/sector list sector
    int tsTrack, tsSector;
    if (!allocateSector(tsTrack, tsSector)) return false;

    int tsOffset = getSectorOffset(tsTrack, tsSector);
    // TS list: bytes 0x01-0x02 = next TS list (0,0 = none)
    // Pairs start at byte 0x0C

    // Write data sectors
    int remaining = dataLen;
    const uint8_t* ptr = fileData;
    int pairIdx = 0;
    int sectorCount = 0;

    while (remaining > 0 && pairIdx < 122) {
        int dataTrack, dataSector;
        if (!allocateSector(dataTrack, dataSector)) return false;

        int writeLen = std::min(remaining, SECTOR_SIZE);
        writeSector(dataTrack, dataSector, ptr, writeLen);

        // Record in TS list
        data_[tsOffset + 0x0C + pairIdx * 2] = dataTrack;
        data_[tsOffset + 0x0C + pairIdx * 2 + 1] = dataSector;

        ptr += writeLen;
        remaining -= writeLen;
        pairIdx++;
        sectorCount++;
    }

    // Add catalog entry
    int catOffset = getSectorOffset(catalogTrack_, catalogSector_);
    int entryOffset = catOffset + 0x0B + catalogEntryIndex_ * 0x23;

    // First TS list track/sector
    data_[entryOffset + 0x00] = tsTrack;
    data_[entryOffset + 0x01] = tsSector;

    // File type with locked flag
    data_[entryOffset + 0x02] = (locked ? 0x80 : 0x00) | fileType;

    // Filename (30 bytes, space-padded, high bit set)
    for (int i = 0; i < 30; i++) {
        if (i < static_cast<int>(name.size())) {
            data_[entryOffset + 0x03 + i] = name[i] | 0x80;
        } else {
            data_[entryOffset + 0x03 + i] = ' ' | 0x80;
        }
    }

    // Sector count
    data_[entryOffset + 0x21] = sectorCount & 0xFF;
    data_[entryOffset + 0x22] = (sectorCount >> 8) & 0xFF;

    catalogEntryIndex_++;
    if (catalogEntryIndex_ >= 7) {
        // Would need another catalog sector - for testing, 7 entries is enough
        catalogEntryIndex_ = 0;
        catalogSector_--;
    }

    return true;
}

// ==================== ProDOSDiskBuilder ====================

ProDOSDiskBuilder::ProDOSDiskBuilder(const std::string& volumeName)
    : data_(DISK_SIZE, 0), volumeName_(volumeName) {
    initVolumeDirectory();
    initBitmap();
}

void ProDOSDiskBuilder::readBlock(int blockNum, uint8_t* out) const {
    // ProDOS block to physical sector mapping for 140K disks
    // Block N maps to track N/8, sectors interleaved
    static const uint8_t INTERLEAVE[16] = {
        0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15
    };

    int track = blockNum / 8;
    int blockInTrack = blockNum % 8;
    int sector1 = INTERLEAVE[blockInTrack * 2];
    int sector2 = INTERLEAVE[blockInTrack * 2 + 1];

    int off1 = (track * 16 + sector1) * 256;
    int off2 = (track * 16 + sector2) * 256;

    std::memcpy(out, &data_[off1], 256);
    std::memcpy(out + 256, &data_[off2], 256);
}

void ProDOSDiskBuilder::writeBlock(int blockNum, const uint8_t* blockData, int len) {
    // For simplicity, store blocks in ProDOS order (linear)
    // Since we're building images for testing the ProDOS parser,
    // we write in ProDOS-order format (blockNum * 512)
    int offset = blockNum * BLOCK_SIZE;
    if (offset + len > DISK_SIZE) return;
    int copyLen = std::min(len, BLOCK_SIZE);
    std::memcpy(&data_[offset], blockData, copyLen);
}

void ProDOSDiskBuilder::initVolumeDirectory() {
    // Block 2 = volume directory header
    uint8_t block[BLOCK_SIZE] = {};

    // Prev/next directory block pointers
    block[0] = 0;  // Previous block (none for volume dir)
    block[1] = 0;
    block[2] = 3;  // Next block
    block[3] = 0;

    // Volume directory header entry at offset 4
    int nameLen = std::min(static_cast<int>(volumeName_.size()), 15);
    block[4] = 0xF0 | nameLen;  // Storage type F = volume header, name length

    for (int i = 0; i < nameLen; i++) {
        block[5 + i] = volumeName_[i] & 0x7F;
    }

    // Creation date/time (leave zeroed)
    // Access byte
    block[0x22] = 0xC3;  // Full access

    // Entry length
    block[0x23] = 0x27;  // 39 bytes per entry

    // Entries per block
    block[0x24] = 0x0D;  // 13 entries per block

    // File count
    block[0x25] = 0;
    block[0x26] = 0;

    // Bitmap pointer
    block[0x27] = 6;  // Block 6
    block[0x28] = 0;

    // Total blocks
    block[0x29] = 0x18;  // 280 blocks (0x118)
    block[0x2A] = 0x01;

    writeBlock(2, block, BLOCK_SIZE);

    // Block 3 - continuation of volume directory (empty)
    uint8_t block3[BLOCK_SIZE] = {};
    block3[0] = 2; block3[1] = 0;  // Previous = block 2
    block3[2] = 4; block3[3] = 0;  // Next = block 4
    writeBlock(3, block3, BLOCK_SIZE);

    // Block 4 - continuation (empty)
    uint8_t block4[BLOCK_SIZE] = {};
    block4[0] = 3; block4[1] = 0;  // Previous = block 3
    block4[2] = 5; block4[3] = 0;  // Next = block 5
    writeBlock(4, block4, BLOCK_SIZE);

    // Block 5 - last dir block
    uint8_t block5[BLOCK_SIZE] = {};
    block5[0] = 4; block5[1] = 0;  // Previous = block 4
    block5[2] = 0; block5[3] = 0;  // Next = none
    writeBlock(5, block5, BLOCK_SIZE);
}

void ProDOSDiskBuilder::initBitmap() {
    // Volume bitmap at block 6
    // 280 blocks = 35 bytes, each bit = 1 block (1=free, 0=used)
    uint8_t bitmap[BLOCK_SIZE] = {};

    // Mark all blocks as free initially
    for (int i = 0; i < 35; i++) {
        bitmap[i] = 0xFF;
    }

    // Mark blocks 0-9 as used (boot, volume dir, bitmap)
    bitmap[0] = 0x00;  // Blocks 0-7 used
    bitmap[1] = 0x3F;  // Blocks 8-9 used, 10-15 free

    writeBlock(6, bitmap, BLOCK_SIZE);
}

bool ProDOSDiskBuilder::allocateBlock(int& blockNum) {
    if (nextAllocBlock_ >= 280) return false;
    blockNum = nextAllocBlock_++;
    return true;
}

bool ProDOSDiskBuilder::addFile(const std::string& name, uint8_t fileType,
                                 uint16_t auxType, const uint8_t* fileData, int dataLen) {
    // Allocate block for file data (seedling - single block for small files)
    int dataBlock;
    if (!allocateBlock(dataBlock)) return false;

    // Write file data
    uint8_t blockBuf[BLOCK_SIZE] = {};
    int writeLen = std::min(dataLen, BLOCK_SIZE);
    if (fileData && writeLen > 0) {
        std::memcpy(blockBuf, fileData, writeLen);
    }
    writeBlock(dataBlock, blockBuf, BLOCK_SIZE);

    // Add directory entry
    // Directory entries start at offset 4 + entry * 0x27 within a block
    // First entry in block 2 is the volume header, so file entries start at index 1
    int entryBlock = 2 + (dirEntryCount_ + 1) / 13;
    int entryInBlock = (dirEntryCount_ + 1) % 13;
    if (entryInBlock == 0 && entryBlock > 2) {
        entryInBlock = 0;
    }

    // Read the directory block
    int blockOffset = entryBlock * BLOCK_SIZE;
    int entryOffset = blockOffset + 4 + entryInBlock * 0x27;

    int nameLen = std::min(static_cast<int>(name.size()), 15);
    data_[entryOffset] = 0x10 | nameLen;  // Storage type 1 = seedling

    for (int i = 0; i < nameLen; i++) {
        data_[entryOffset + 1 + i] = name[i] & 0x7F;
    }

    // File type
    data_[entryOffset + 0x10] = fileType;

    // Key pointer (data block)
    data_[entryOffset + 0x11] = dataBlock & 0xFF;
    data_[entryOffset + 0x12] = (dataBlock >> 8) & 0xFF;

    // Blocks used
    data_[entryOffset + 0x13] = 1;
    data_[entryOffset + 0x14] = 0;

    // EOF
    data_[entryOffset + 0x15] = dataLen & 0xFF;
    data_[entryOffset + 0x16] = (dataLen >> 8) & 0xFF;
    data_[entryOffset + 0x17] = (dataLen >> 16) & 0xFF;

    // Aux type
    data_[entryOffset + 0x1F] = auxType & 0xFF;
    data_[entryOffset + 0x20] = (auxType >> 8) & 0xFF;

    // Access
    data_[entryOffset + 0x1E] = 0xC3;

    // Update file count in volume header
    int volOffset = 2 * BLOCK_SIZE + 4;
    dirEntryCount_++;
    data_[volOffset + 0x21] = dirEntryCount_ & 0xFF;
    data_[volOffset + 0x22] = (dirEntryCount_ >> 8) & 0xFF;

    return true;
}

// ==================== PascalDiskBuilder ====================

PascalDiskBuilder::PascalDiskBuilder(const std::string& volumeName)
    : data_(DISK_SIZE, 0), volumeName_(volumeName) {
    initVolumeHeader();
}

void PascalDiskBuilder::initVolumeHeader() {
    // Pascal directory is blocks 2-5 (2048 bytes)
    // Volume header is the first 26-byte entry in block 2
    // For testing, we write in ProDOS block order

    int offset = 2 * BLOCK_SIZE;  // Block 2

    // Entry 0: Volume header
    data_[offset + 0] = 0;     // firstBlock = 0
    data_[offset + 1] = 0;
    data_[offset + 2] = 6;     // nextBlock (first free block)
    data_[offset + 3] = 0;
    data_[offset + 4] = 0;     // fileType = 0 (volume)
    data_[offset + 5] = 0;

    // Volume name (Pascal format: length byte + chars)
    int nameLen = std::min(static_cast<int>(volumeName_.size()), 7);
    data_[offset + 6] = nameLen;
    for (int i = 0; i < nameLen; i++) {
        data_[offset + 7 + i] = volumeName_[i];
    }

    // Total blocks
    data_[offset + 14] = 0x18;  // 280 blocks (low)
    data_[offset + 15] = 0x01;  // (high)

    // File count (will be updated as files are added)
    data_[offset + 16] = 0;
    data_[offset + 17] = 0;

    // Last access date (leave 0)
    // Most recent date set
    data_[offset + 20] = 0;
    data_[offset + 21] = 0;
}

bool PascalDiskBuilder::addFile(const std::string& name, uint8_t fileType,
                                 const uint8_t* fileData, int dataLen) {
    int blocksNeeded = (dataLen + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (blocksNeeded == 0) blocksNeeded = 1;
    if (nextBlock_ + blocksNeeded > 280) return false;

    int startBlock = nextBlock_;

    // Write file data
    for (int i = 0; i < blocksNeeded; i++) {
        int blockOffset = (nextBlock_ + i) * BLOCK_SIZE;
        int remaining = dataLen - i * BLOCK_SIZE;
        int writeLen = std::min(remaining, BLOCK_SIZE);
        if (writeLen > 0 && fileData) {
            std::memcpy(&data_[blockOffset], fileData + i * BLOCK_SIZE, writeLen);
        }
    }

    nextBlock_ += blocksNeeded;

    // Add directory entry (26 bytes each, starting at entry 1 in block 2)
    dirEntryCount_++;
    int entryOffset = 2 * BLOCK_SIZE + dirEntryCount_ * 26;

    data_[entryOffset + 0] = startBlock & 0xFF;
    data_[entryOffset + 1] = (startBlock >> 8) & 0xFF;
    data_[entryOffset + 2] = nextBlock_ & 0xFF;
    data_[entryOffset + 3] = (nextBlock_ >> 8) & 0xFF;
    data_[entryOffset + 4] = fileType;
    data_[entryOffset + 5] = 0;

    int nameLen = std::min(static_cast<int>(name.size()), 15);
    data_[entryOffset + 6] = nameLen;
    for (int i = 0; i < nameLen; i++) {
        data_[entryOffset + 7 + i] = name[i];
    }

    // Bytes in last block
    int bytesInLast = dataLen % BLOCK_SIZE;
    if (bytesInLast == 0 && dataLen > 0) bytesInLast = BLOCK_SIZE;
    data_[entryOffset + 22] = bytesInLast & 0xFF;
    data_[entryOffset + 23] = (bytesInLast >> 8) & 0xFF;

    // Update volume header file count
    int volOffset = 2 * BLOCK_SIZE;
    data_[volOffset + 16] = dirEntryCount_ & 0xFF;
    data_[volOffset + 17] = (dirEntryCount_ >> 8) & 0xFF;

    // Note: Do NOT update the nextBlock field in the volume header.
    // That field must remain 6 (first block after the 4-block directory area)
    // as required by the Pascal filesystem validator.

    return true;
}

// ==================== BlockImageBuilder ====================

BlockImageBuilder::BlockImageBuilder(int totalBlocks)
    : data_(totalBlocks * BLOCK_SIZE, 0) {
}

void BlockImageBuilder::writeBlock(int blockNum, const uint8_t* blockData, int len) {
    int offset = blockNum * BLOCK_SIZE;
    if (offset + len > static_cast<int>(data_.size())) return;
    int copyLen = std::min(len, BLOCK_SIZE);
    std::memcpy(&data_[offset], blockData, copyLen);
}

} // namespace test
