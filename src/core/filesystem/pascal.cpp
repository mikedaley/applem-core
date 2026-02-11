/*
 * pascal.cpp - Apple Pascal filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "pascal.hpp"
#include <cstring>

namespace a2e {

void Pascal::readBlock(const uint8_t* data, size_t size, int blockNum,
                       bool dosOrder, uint8_t* out) {
  memset(out, 0, BLOCK_SIZE);

  if (size <= static_cast<size_t>(DISK_140K_SIZE)) {
    // 140K disk - block N is at track N/8, sectors (N%8)*2 and (N%8)*2+1
    int track = blockNum / 8;
    int blockInTrack = blockNum % 8;
    int prodosSector1 = blockInTrack * 2;
    int prodosSector2 = blockInTrack * 2 + 1;

    int sector1 = dosOrder ? PRODOS_TO_DOS_SECTOR[prodosSector1] : prodosSector1;
    int sector2 = dosOrder ? PRODOS_TO_DOS_SECTOR[prodosSector2] : prodosSector2;

    int offset1 = (track * 16 + sector1) * 256;
    int offset2 = (track * 16 + sector2) * 256;

    if (offset1 + 256 <= static_cast<int>(size)) {
      memcpy(out, data + offset1, 256);
    }
    if (offset2 + 256 <= static_cast<int>(size)) {
      memcpy(out + 256, data + offset2, 256);
    }
  } else {
    // Larger disk - blocks are sequential
    int offset = blockNum * BLOCK_SIZE;
    if (offset + BLOCK_SIZE <= static_cast<int>(size)) {
      memcpy(out, data + offset, BLOCK_SIZE);
    }
  }
}

bool Pascal::isValidVolumeHeader(const uint8_t* dirData) {
  // Volume header is the first 26-byte entry in the directory (blocks 2-5)
  // Bytes 0-1: first block of volume (must be 0)
  // Bytes 2-3: next block after directory (must be 6)
  // Byte 4: file type (must be 0 for volume header)
  // Byte 6: volume name length (1-7)
  // Bytes 7-13: volume name characters
  // Bytes 14-15: total blocks on volume
  // Bytes 16-17: number of files

  uint16_t firstBlock = dirData[0] | (dirData[1] << 8);
  uint16_t nextBlock = dirData[2] | (dirData[3] << 8);
  uint8_t fileType = dirData[4];
  uint8_t nameLen = dirData[6];
  uint16_t totalBlocks = dirData[14] | (dirData[15] << 8);
  uint16_t fileCount = dirData[16] | (dirData[17] << 8);

  // Validate volume header fields
  if (firstBlock != 0) return false;
  if (nextBlock != 6) return false;
  if (fileType != 0) return false;
  if (nameLen < 1 || nameLen > 7) return false;
  if (fileCount > MAX_DIR_ENTRIES) return false;

  // Validate volume name characters (should be printable ASCII)
  for (int i = 0; i < nameLen; i++) {
    uint8_t ch = dirData[7 + i];
    if (ch < 0x20 || ch > 0x7E) return false;
  }

  // Total blocks should be reasonable (280 for 140K, could be larger for other media)
  if (totalBlocks == 0 || totalBlocks > 65535) return false;

  return true;
}

bool Pascal::detectSectorOrder(const uint8_t* data, size_t size, bool* dosOrder) {
  uint8_t dirData[DIR_BLOCKS * BLOCK_SIZE];

  // Try ProDOS order first
  for (int i = 0; i < DIR_BLOCKS; i++) {
    readBlock(data, size, DIR_START_BLOCK + i, false, dirData + i * BLOCK_SIZE);
  }
  if (isValidVolumeHeader(dirData)) {
    *dosOrder = false;
    return true;
  }

  // Try DOS order
  for (int i = 0; i < DIR_BLOCKS; i++) {
    readBlock(data, size, DIR_START_BLOCK + i, true, dirData + i * BLOCK_SIZE);
  }
  if (isValidVolumeHeader(dirData)) {
    *dosOrder = true;
    return true;
  }

  return false;
}

const char* Pascal::getFileTypeName(uint8_t fileType) {
  switch (fileType) {
    case 0: return "VOL";
    case 1: return "BAD";
    case 2: return "CODE";
    case 3: return "TEXT";
    case 4: return "INFO";
    case 5: return "DATA";
    case 6: return "GRAF";
    case 7: return "FOTO";
    case 8: return "SDIR";
    default: return "???";
  }
}

bool Pascal::isPascal(const uint8_t* data, size_t size) {
  if (size < static_cast<size_t>(DISK_140K_SIZE)) return false;
  bool dosOrder;
  return detectSectorOrder(data, size, &dosOrder);
}

bool Pascal::parseVolumeInfo(const uint8_t* data, size_t size, PascalVolumeInfo* info) {
  if (size < static_cast<size_t>(DISK_140K_SIZE)) return false;

  bool dosOrder;
  if (!detectSectorOrder(data, size, &dosOrder)) return false;

  // Read the full directory area (blocks 2-5)
  uint8_t dirData[DIR_BLOCKS * BLOCK_SIZE];
  for (int i = 0; i < DIR_BLOCKS; i++) {
    readBlock(data, size, DIR_START_BLOCK + i, dosOrder, dirData + i * BLOCK_SIZE);
  }

  // Parse volume header (first 26-byte entry)
  uint8_t nameLen = dirData[6];
  if (nameLen > 7) nameLen = 7;
  for (int i = 0; i < nameLen; i++) {
    info->volumeName[i] = dirData[7 + i] & 0x7F;
  }
  info->volumeName[nameLen] = '\0';

  info->totalBlocks = dirData[14] | (dirData[15] << 8);
  info->fileCount = dirData[16] | (dirData[17] << 8);
  info->useDOSSectorOrder = dosOrder;

  return true;
}

int Pascal::readCatalog(const uint8_t* data, size_t size,
                        PascalCatalogEntry* entries, int maxEntries) {
  PascalVolumeInfo info;
  if (!parseVolumeInfo(data, size, &info)) return 0;

  bool dosOrder = info.useDOSSectorOrder;

  // Read the full directory area (blocks 2-5)
  uint8_t dirData[DIR_BLOCKS * BLOCK_SIZE];
  for (int i = 0; i < DIR_BLOCKS; i++) {
    readBlock(data, size, DIR_START_BLOCK + i, dosOrder, dirData + i * BLOCK_SIZE);
  }

  int count = 0;
  int numFiles = info.fileCount;
  if (numFiles > MAX_DIR_ENTRIES) numFiles = MAX_DIR_ENTRIES;

  // File entries start at entry 1 (entry 0 is the volume header)
  for (int i = 1; i <= numFiles && count < maxEntries; i++) {
    int offset = i * DIR_ENTRY_SIZE;
    if (offset + DIR_ENTRY_SIZE > DIR_BLOCKS * BLOCK_SIZE) break;

    const uint8_t* entry = dirData + offset;

    PascalCatalogEntry& e = entries[count];

    e.startBlock = entry[0] | (entry[1] << 8);
    e.nextBlock = entry[2] | (entry[3] << 8);
    e.fileType = entry[4];

    // Skip deleted entries (startBlock == 0 or nextBlock <= startBlock)
    if (e.startBlock == 0 || e.nextBlock <= e.startBlock) continue;

    // File name: length-prefixed at byte 6
    uint8_t nameLen = entry[6];
    if (nameLen > 15) nameLen = 15;
    for (int j = 0; j < nameLen; j++) {
      e.filename[j] = entry[7 + j] & 0x7F;
    }
    e.filename[nameLen] = '\0';

    // Skip entries with empty names
    if (nameLen == 0) continue;

    const char* typeName = getFileTypeName(e.fileType);
    strncpy(e.fileTypeName, typeName, sizeof(e.fileTypeName) - 1);
    e.fileTypeName[sizeof(e.fileTypeName) - 1] = '\0';

    e.bytesInLastBlock = entry[22] | (entry[23] << 8);
    e.modDate = entry[24] | (entry[25] << 8);

    // Compute file size
    int numBlocks = e.nextBlock - e.startBlock;
    if (numBlocks > 1) {
      e.fileSize = (numBlocks - 1) * BLOCK_SIZE + e.bytesInLastBlock;
    } else if (numBlocks == 1) {
      e.fileSize = e.bytesInLastBlock;
    } else {
      e.fileSize = 0;
    }

    count++;
  }

  return count;
}

int Pascal::readFile(const uint8_t* data, size_t size,
                     const PascalCatalogEntry* entry,
                     uint8_t* outBuf, int outMax) {
  PascalVolumeInfo info;
  if (!parseVolumeInfo(data, size, &info)) return 0;
  bool dosOrder = info.useDOSSectorOrder;

  int numBlocks = entry->nextBlock - entry->startBlock;
  if (numBlocks <= 0) return 0;

  int bytesRead = 0;
  for (int i = 0; i < numBlocks && bytesRead < outMax; i++) {
    uint8_t block[BLOCK_SIZE];
    readBlock(data, size, entry->startBlock + i, dosOrder, block);

    int bytesToCopy;
    if (i == numBlocks - 1) {
      // Last block - use bytesInLastBlock
      bytesToCopy = entry->bytesInLastBlock;
      if (bytesToCopy == 0) bytesToCopy = BLOCK_SIZE;
    } else {
      bytesToCopy = BLOCK_SIZE;
    }

    if (bytesRead + bytesToCopy > outMax) {
      bytesToCopy = outMax - bytesRead;
    }

    memcpy(outBuf + bytesRead, block, bytesToCopy);
    bytesRead += bytesToCopy;
  }

  return bytesRead;
}

int Pascal::mapFileTypeForViewer(uint8_t pascalType) {
  switch (pascalType) {
    case 3: return 0x00; // TEXT -> Text viewer
    default: return -1;  // Everything else -> hex dump
  }
}

} // namespace a2e
