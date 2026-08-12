/*
 * prodos.cpp - ProDOS filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "prodos.hpp"
#include <cstring>
#include <cstdio>
#include <unordered_set>
#include <vector>

namespace a2e {

void ProDOS::readBlock(const uint8_t* data, size_t size, int blockNum,
                       bool dosOrder, uint8_t* out) {
  memset(out, 0, BLOCK_SIZE);

  if (size <= static_cast<size_t>(DISK_140K_SIZE)) {
    // 140K disk - block N is at track N/8, sectors (N%8)*2 and (N%8)*2+1
    int track = blockNum / 8;
    int blockInTrack = blockNum % 8;
    int prodosSector1 = blockInTrack * 2;
    int prodosSector2 = blockInTrack * 2 + 1;

    int sector1 = dosOrder ? GCR::PRODOS_TO_DOS_SECTOR[prodosSector1] : prodosSector1;
    int sector2 = dosOrder ? GCR::PRODOS_TO_DOS_SECTOR[prodosSector2] : prodosSector2;

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

bool ProDOS::isValidVolumeHeader(const uint8_t* block) {
  uint8_t storageTypeAndNameLen = block[0x04];
  uint8_t storageType = (storageTypeAndNameLen >> 4) & 0x0F;
  uint8_t nameLen = storageTypeAndNameLen & 0x0F;

  if (storageType != STORAGE_VOLUME_HEADER) return false;
  if (nameLen == 0 || nameLen > 15) return false;

  uint8_t entryLength = block[0x23];
  uint8_t entriesPerBlock = block[0x24];
  if (entryLength != 0x27 || entriesPerBlock != 0x0D) return false;

  return true;
}

bool ProDOS::detectSectorOrder(const uint8_t* data, size_t size, bool* dosOrder) {
  uint8_t block[BLOCK_SIZE];

  // Try ProDOS order first
  readBlock(data, size, 2, false, block);
  if (isValidVolumeHeader(block)) {
    *dosOrder = false;
    return true;
  }

  // Try DOS order
  readBlock(data, size, 2, true, block);
  if (isValidVolumeHeader(block)) {
    *dosOrder = true;
    return true;
  }

  return false;
}

void ProDOS::parseFilename(const uint8_t* bytes, int nameLen, char* out, int maxLen) {
  int len = nameLen;
  if (len > maxLen - 1) len = maxLen - 1;
  for (int i = 0; i < len; i++) {
    out[i] = bytes[i] & 0x7F;
  }
  out[len] = '\0';
}

const char* ProDOS::getFileTypeName(uint8_t fileType) {
  switch (fileType) {
    case 0x00: return "UNK";
    case 0x01: return "BAD";
    case 0x04: return "TXT";
    case 0x06: return "BIN";
    case 0x0F: return "DIR";
    case 0x19: return "ADB";
    case 0x1A: return "AWP";
    case 0x1B: return "ASP";
    case 0xB0: return "SRC";
    case 0xB3: return "S16";
    case 0xBF: return "DOC";
    case 0xC0: return "PNT";
    case 0xC1: return "PIC";
    case 0xE0: return "SHK";
    case 0xEF: return "PAS";
    case 0xF0: return "CMD";
    case 0xFA: return "INT";
    case 0xFB: return "IVR";
    case 0xFC: return "BAS";
    case 0xFD: return "VAR";
    case 0xFE: return "REL";
    case 0xFF: return "SYS";
    default:   return "???";
  }
}

bool ProDOS::isProDOS(const uint8_t* data, size_t size) {
  if (size < static_cast<size_t>(DISK_140K_SIZE)) return false;
  bool dosOrder;
  return detectSectorOrder(data, size, &dosOrder);
}

bool ProDOS::parseVolumeInfo(const uint8_t* data, size_t size, ProDOSVolumeInfo* info) {
  if (size < static_cast<size_t>(DISK_140K_SIZE)) return false;

  bool dosOrder;
  if (!detectSectorOrder(data, size, &dosOrder)) return false;

  uint8_t block[BLOCK_SIZE];
  readBlock(data, size, 2, dosOrder, block);

  uint8_t nameLen = block[0x04] & 0x0F;
  parseFilename(block + 0x05, nameLen, info->volumeName, sizeof(info->volumeName));

  // Validate volume name
  if (info->volumeName[0] == '\0') return false;

  info->fileCount = block[0x25] | (block[0x26] << 8);
  info->totalBlocks = block[0x29] | (block[0x2A] << 8);
  info->useDOSSectorOrder = dosOrder;

  // Validate total blocks
  int expectedBlocks = (size <= static_cast<size_t>(DISK_140K_SIZE)) ? 280
                       : static_cast<int>(size / BLOCK_SIZE);
  if (info->totalBlocks == 0 || info->totalBlocks > expectedBlocks + 10) return false;

  return true;
}

int ProDOS::readDirectoryEntries(const uint8_t* data, size_t size,
                                 int startBlock, const char* pathPrefix,
                                 bool dosOrder,
                                 ProDOSCatalogEntry* entries, int maxEntries,
                                 int currentCount) {
  int count = currentCount;
  int blockNum = startBlock;
  int maxBlocks = static_cast<int>(size / BLOCK_SIZE);

  // Visited set for cycle detection (supports large HD volumes)
  std::unordered_set<int> visited;

  while (blockNum != 0 && count < maxEntries) {
    if (blockNum >= maxBlocks || visited.count(blockNum)) break;
    visited.insert(blockNum);

    uint8_t block[BLOCK_SIZE];
    readBlock(data, size, blockNum, dosOrder, block);

    int nextBlock = block[0x02] | (block[0x03] << 8);

    // Parse entries (first block has header at entry 0)
    int firstEntry = (blockNum == startBlock) ? 1 : 0;

    for (int i = firstEntry; i < 13 && count < maxEntries; i++) {
      int offset = 0x04 + (i * 39);
      if (offset + 39 > BLOCK_SIZE) break;

      const uint8_t* entry = block + offset;
      uint8_t storageTypeAndNameLen = entry[0x00];
      uint8_t storageType = (storageTypeAndNameLen >> 4) & 0x0F;
      uint8_t nameLen = storageTypeAndNameLen & 0x0F;

      if (storageType == STORAGE_DELETED || nameLen == 0) continue;
      if (storageType == STORAGE_VOLUME_HEADER || storageType == STORAGE_SUBDIR_HEADER) continue;

      ProDOSCatalogEntry& e = entries[count];
      parseFilename(entry + 0x01, nameLen, e.filename, sizeof(e.filename));
      e.fileType = entry[0x10];
      const char* typeName = getFileTypeName(e.fileType);
      strncpy(e.fileTypeName, typeName, sizeof(e.fileTypeName) - 1);
      e.fileTypeName[sizeof(e.fileTypeName) - 1] = '\0';
      e.storageType = storageType;
      e.keyPointer = entry[0x11] | (entry[0x12] << 8);
      e.blocksUsed = entry[0x13] | (entry[0x14] << 8);
      e.eof = entry[0x15] | (entry[0x16] << 8) | (entry[0x17] << 16);
      e.auxType = entry[0x1F] | (entry[0x20] << 8);
      e.access = entry[0x1E];
      e.isLocked = (e.access & 0x02) == 0;
      e.isDirectory = (storageType == STORAGE_SUBDIR);

      // Build full path
      if (pathPrefix[0] != '\0') {
        snprintf(e.path, sizeof(e.path), "%s/%s", pathPrefix, e.filename);
      } else {
        strncpy(e.path, e.filename, sizeof(e.path) - 1);
        e.path[sizeof(e.path) - 1] = '\0';
      }

      count++;
    }

    blockNum = nextBlock;
  }

  return count;
}

int ProDOS::readCatalog(const uint8_t* data, size_t size,
                        ProDOSCatalogEntry* entries, int maxEntries) {
  ProDOSVolumeInfo info;
  if (!parseVolumeInfo(data, size, &info)) return 0;

  bool dosOrder = info.useDOSSectorOrder;
  int count = readDirectoryEntries(data, size, 2, "", dosOrder, entries, maxEntries, 0);

  // Process subdirectories (breadth-first)
  int processedDirs = 0;
  while (processedDirs < count) {
    // Scan for unprocessed subdirectories
    bool found = false;
    for (int i = processedDirs; i < count; i++) {
      if (entries[i].isDirectory) {
        int newCount = readDirectoryEntries(data, size, entries[i].keyPointer,
                                            entries[i].path, dosOrder,
                                            entries, maxEntries, count);
        count = newCount;
        found = true;
      }
      processedDirs = i + 1;
      if (found) break;
    }
    if (!found) break;
  }

  return count;
}

int ProDOS::readSeedlingFile(const uint8_t* data, size_t size, int blockNum,
                              int eof, bool dosOrder, uint8_t* out, int outMax) {
  uint8_t block[BLOCK_SIZE];
  readBlock(data, size, blockNum, dosOrder, block);
  int bytes = eof < BLOCK_SIZE ? eof : BLOCK_SIZE;
  if (bytes > outMax) bytes = outMax;
  memcpy(out, block, bytes);
  return bytes;
}

int ProDOS::readSaplingFile(const uint8_t* data, size_t size, int indexBlock,
                             int eof, bool dosOrder, uint8_t* out, int outMax) {
  uint8_t index[BLOCK_SIZE];
  readBlock(data, size, indexBlock, dosOrder, index);

  int bytesRead = 0;
  for (int i = 0; i < 256 && bytesRead < eof && bytesRead < outMax; i++) {
    int dataBlockNum = index[i] | (index[i + 256] << 8);
    int bytesToCopy = BLOCK_SIZE;
    if (bytesRead + bytesToCopy > eof) bytesToCopy = eof - bytesRead;
    if (bytesRead + bytesToCopy > outMax) bytesToCopy = outMax - bytesRead;

    if (dataBlockNum == 0) {
      // Sparse file - fill with zeros
      memset(out + bytesRead, 0, bytesToCopy);
    } else {
      uint8_t dataBlock[BLOCK_SIZE];
      readBlock(data, size, dataBlockNum, dosOrder, dataBlock);
      memcpy(out + bytesRead, dataBlock, bytesToCopy);
    }
    bytesRead += bytesToCopy;
  }

  return bytesRead;
}

int ProDOS::readTreeFile(const uint8_t* data, size_t size, int masterBlock,
                          int eof, bool dosOrder, uint8_t* out, int outMax) {
  uint8_t master[BLOCK_SIZE];
  readBlock(data, size, masterBlock, dosOrder, master);

  int bytesRead = 0;
  for (int i = 0; i < 128 && bytesRead < eof && bytesRead < outMax; i++) {
    int indexBlockNum = master[i] | (master[i + 256] << 8);
    if (indexBlockNum == 0) {
      // Sparse - skip up to 256 blocks
      int bytesToSkip = 256 * BLOCK_SIZE;
      if (bytesRead + bytesToSkip > eof) bytesToSkip = eof - bytesRead;
      if (bytesRead + bytesToSkip > outMax) bytesToSkip = outMax - bytesRead;
      memset(out + bytesRead, 0, bytesToSkip);
      bytesRead += bytesToSkip;
      continue;
    }

    uint8_t index[BLOCK_SIZE];
    readBlock(data, size, indexBlockNum, dosOrder, index);

    for (int j = 0; j < 256 && bytesRead < eof && bytesRead < outMax; j++) {
      int dataBlockNum = index[j] | (index[j + 256] << 8);
      int bytesToCopy = BLOCK_SIZE;
      if (bytesRead + bytesToCopy > eof) bytesToCopy = eof - bytesRead;
      if (bytesRead + bytesToCopy > outMax) bytesToCopy = outMax - bytesRead;

      if (dataBlockNum == 0) {
        memset(out + bytesRead, 0, bytesToCopy);
      } else {
        uint8_t dataBlock[BLOCK_SIZE];
        readBlock(data, size, dataBlockNum, dosOrder, dataBlock);
        memcpy(out + bytesRead, dataBlock, bytesToCopy);
      }
      bytesRead += bytesToCopy;
    }
  }

  return bytesRead;
}

int ProDOS::readFile(const uint8_t* data, size_t size,
                     const ProDOSCatalogEntry* entry,
                     uint8_t* outBuf, int outMax) {
  if (entry->isDirectory) return 0;

  ProDOSVolumeInfo info;
  if (!parseVolumeInfo(data, size, &info)) return 0;
  bool dosOrder = info.useDOSSectorOrder;

  switch (entry->storageType) {
    case STORAGE_SEEDLING:
      return readSeedlingFile(data, size, entry->keyPointer, entry->eof, dosOrder, outBuf, outMax);
    case STORAGE_SAPLING:
      return readSaplingFile(data, size, entry->keyPointer, entry->eof, dosOrder, outBuf, outMax);
    case STORAGE_TREE:
      return readTreeFile(data, size, entry->keyPointer, entry->eof, dosOrder, outBuf, outMax);
    default:
      return 0;
  }
}

int ProDOS::mapFileTypeForViewer(uint8_t prodosType) {
  switch (prodosType) {
    case 0x04: return 0x00; // TXT -> Text
    case 0xFA: return 0x01; // INT -> Integer BASIC
    case 0xFC: return 0x02; // BAS -> Applesoft BASIC
    case 0x06: return 0x04; // BIN -> Binary
    case 0xFF: return 0x04; // SYS -> Binary
    default:   return -1;
  }
}

// ============================================================================
// Writing
// ============================================================================

void ProDOS::writeBlock(uint8_t* data, size_t size, int blockNum,
                        bool dosOrder, const uint8_t* in) {
  if (blockNum < 0) return;

  if (size <= static_cast<size_t>(DISK_140K_SIZE)) {
    int track = blockNum / 8;
    int blockInTrack = blockNum % 8;
    int prodosSector1 = blockInTrack * 2;
    int prodosSector2 = blockInTrack * 2 + 1;

    int sector1 = dosOrder ? GCR::PRODOS_TO_DOS_SECTOR[prodosSector1] : prodosSector1;
    int sector2 = dosOrder ? GCR::PRODOS_TO_DOS_SECTOR[prodosSector2] : prodosSector2;

    int offset1 = (track * 16 + sector1) * 256;
    int offset2 = (track * 16 + sector2) * 256;

    if (offset1 >= 0 && offset1 + 256 <= static_cast<int>(size)) {
      memcpy(data + offset1, in, 256);
    }
    if (offset2 >= 0 && offset2 + 256 <= static_cast<int>(size)) {
      memcpy(data + offset2, in + 256, 256);
    }
  } else {
    int offset = blockNum * BLOCK_SIZE;
    if (offset >= 0 && offset + BLOCK_SIZE <= static_cast<int>(size)) {
      memcpy(data + offset, in, BLOCK_SIZE);
    }
  }
}

bool ProDOS::isBlockFree(const uint8_t* data, size_t size, bool dosOrder,
                         int bitmapBlock, int blockNum) {
  int blocksPerBitmapBlock = BLOCK_SIZE * 8;
  int whichBlock = bitmapBlock + blockNum / blocksPerBitmapBlock;
  int indexInBlock = blockNum % blocksPerBitmapBlock;

  uint8_t bitmap[BLOCK_SIZE];
  readBlock(data, size, whichBlock, dosOrder, bitmap);
  return (bitmap[indexInBlock / 8] >> (7 - (indexInBlock % 8))) & 1;
}

void ProDOS::setBlockAllocated(uint8_t* data, size_t size, bool dosOrder,
                               int bitmapBlock, int blockNum, bool allocated) {
  int blocksPerBitmapBlock = BLOCK_SIZE * 8;
  int whichBlock = bitmapBlock + blockNum / blocksPerBitmapBlock;
  int indexInBlock = blockNum % blocksPerBitmapBlock;

  uint8_t bitmap[BLOCK_SIZE];
  readBlock(data, size, whichBlock, dosOrder, bitmap);
  uint8_t mask = static_cast<uint8_t>(1 << (7 - (indexInBlock % 8)));
  if (allocated) {
    bitmap[indexInBlock / 8] &= static_cast<uint8_t>(~mask);
  } else {
    bitmap[indexInBlock / 8] |= mask;
  }
  writeBlock(data, size, whichBlock, dosOrder, bitmap);
}

bool ProDOS::allocateBlock(uint8_t* data, size_t size, bool dosOrder,
                           int bitmapBlock, int totalBlocks, int* blockNum) {
  for (int b = 0; b < totalBlocks; b++) {
    if (isBlockFree(data, size, dosOrder, bitmapBlock, b)) {
      setBlockAllocated(data, size, dosOrder, bitmapBlock, b, true);
      *blockNum = b;
      return true;
    }
  }
  return false;
}

bool ProDOS::normaliseFilename(const char* filename, char* out, int maxLen) {
  if (!filename || maxLen < MAX_PRODOS_NAME + 1) return false;

  int len = 0;
  for (const char* p = filename; *p; p++) {
    char c = *p;
    if (len >= MAX_PRODOS_NAME) return false;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');

    bool isLetter = (c >= 'A' && c <= 'Z');
    bool isDigit = (c >= '0' && c <= '9');
    bool isPeriod = (c == '.');
    // ProDOS names are letters, digits and periods, and must start with a
    // letter — a name outside that set cannot be typed at a ProDOS prompt.
    if (!isLetter && !isDigit && !isPeriod) return false;
    if (len == 0 && !isLetter) return false;

    out[len++] = c;
  }

  if (len == 0) return false;
  out[len] = '\0';
  return true;
}

void ProDOS::freeFileBlocks(uint8_t* data, size_t size, bool dosOrder,
                            int bitmapBlock, uint8_t storageType,
                            int keyBlock, uint32_t eof) {
  auto freeIndexed = [&](int indexBlock, int maxEntries) {
    uint8_t index[BLOCK_SIZE];
    readBlock(data, size, indexBlock, dosOrder, index);
    for (int i = 0; i < maxEntries; i++) {
      int block = index[i] | (index[i + 256] << 8);
      if (block != 0) setBlockAllocated(data, size, dosOrder, bitmapBlock, block, false);
    }
    setBlockAllocated(data, size, dosOrder, bitmapBlock, indexBlock, false);
  };

  switch (storageType) {
    case STORAGE_SEEDLING:
      setBlockAllocated(data, size, dosOrder, bitmapBlock, keyBlock, false);
      break;
    case STORAGE_SAPLING:
      freeIndexed(keyBlock, 256);
      break;
    case STORAGE_TREE: {
      uint8_t master[BLOCK_SIZE];
      readBlock(data, size, keyBlock, dosOrder, master);
      for (int i = 0; i < 128; i++) {
        int indexBlock = master[i] | (master[i + 256] << 8);
        if (indexBlock != 0) freeIndexed(indexBlock, 256);
      }
      setBlockAllocated(data, size, dosOrder, bitmapBlock, keyBlock, false);
      break;
    }
    default:
      break;
  }
  (void)eof;
}

FsWriteStatus ProDOS::writeFile(uint8_t* data, size_t size, const char* filename,
                                uint8_t fileType, uint16_t auxType,
                                const uint8_t* fileData, uint32_t fileLen) {
  if (size < static_cast<size_t>(DISK_140K_SIZE)) return FsWriteStatus::ImageTooSmall;

  bool dosOrder = false;
  if (!detectSectorOrder(data, size, &dosOrder)) return FsWriteStatus::NotFormatted;

  char name[MAX_PRODOS_NAME + 1];
  if (!normaliseFilename(filename, name, sizeof(name))) return FsWriteStatus::InvalidName;
  int nameLen = static_cast<int>(strlen(name));

  uint8_t header[BLOCK_SIZE];
  readBlock(data, size, VOLUME_DIR_BLOCK, dosOrder, header);
  int bitmapBlock = header[0x27] | (header[0x28] << 8);
  int totalBlocks = header[0x29] | (header[0x2A] << 8);
  int imageBlocks = static_cast<int>(size / BLOCK_SIZE);
  if (totalBlocks <= 0 || totalBlocks > imageBlocks) totalBlocks = imageBlocks;
  if (bitmapBlock <= 0 || bitmapBlock >= totalBlocks) return FsWriteStatus::NotFormatted;

  // Seedling holds one block; sapling adds an index block addressing up to 256
  // data blocks. Beyond that a tree file would be needed, which nothing this
  // writer is used for produces.
  const uint32_t MAX_SAPLING_BYTES = 256u * BLOCK_SIZE;
  if (fileLen > MAX_SAPLING_BYTES) return FsWriteStatus::FileTooLarge;

  uint32_t dataBlockCount = (fileLen + BLOCK_SIZE - 1) / BLOCK_SIZE;
  if (dataBlockCount == 0) dataBlockCount = 1;
  bool sapling = dataBlockCount > 1;

  // ------------------------------------------------------------------
  // Find the entry to use: same name replaces in place, otherwise the first
  // free slot in the (fixed-size) volume directory.
  // ------------------------------------------------------------------
  int entryBlock = 0, entryOffset = 0;
  int freeBlockNum = 0, freeOffset = 0;
  bool replacing = false;

  {
    int blockNum = VOLUME_DIR_BLOCK;
    int guard = 0;
    while (blockNum != 0 && guard++ < 64) {
      uint8_t block[BLOCK_SIZE];
      readBlock(data, size, blockNum, dosOrder, block);
      int nextBlock = block[0x02] | (block[0x03] << 8);

      int firstEntry = (blockNum == VOLUME_DIR_BLOCK) ? 1 : 0;
      for (int i = firstEntry; i < ENTRIES_PER_BLOCK; i++) {
        int offset = 0x04 + i * ENTRY_LENGTH;
        const uint8_t* entry = block + offset;
        uint8_t storageType = (entry[0x00] >> 4) & 0x0F;
        int entryNameLen = entry[0x00] & 0x0F;

        if (storageType == STORAGE_DELETED || entryNameLen == 0) {
          if (freeBlockNum == 0) {
            freeBlockNum = blockNum;
            freeOffset = offset;
          }
          continue;
        }

        if (entryNameLen == nameLen &&
            memcmp(entry + 0x01, name, static_cast<size_t>(nameLen)) == 0) {
          if (storageType == STORAGE_SUBDIR) return FsWriteStatus::FileLocked;
          if ((entry[0x1E] & 0x02) == 0) return FsWriteStatus::FileLocked;
          entryBlock = blockNum;
          entryOffset = offset;
          replacing = true;
          break;
        }
      }
      if (replacing) break;
      blockNum = nextBlock;
    }
  }

  if (!replacing) {
    if (freeBlockNum == 0) return FsWriteStatus::DirectoryFull;
    entryBlock = freeBlockNum;
    entryOffset = freeOffset;
  }

  // Only the bitmap changes before the point of no return, so backing it up is
  // enough to undo a half-finished allocation on a full disk.
  int bitmapBlockCount = (totalBlocks + BLOCK_SIZE * 8 - 1) / (BLOCK_SIZE * 8);
  std::vector<uint8_t> bitmapBackup(static_cast<size_t>(bitmapBlockCount) * BLOCK_SIZE);
  for (int i = 0; i < bitmapBlockCount; i++) {
    readBlock(data, size, bitmapBlock + i, dosOrder,
              bitmapBackup.data() + static_cast<size_t>(i) * BLOCK_SIZE);
  }

  auto restoreBitmap = [&]() {
    for (int i = 0; i < bitmapBlockCount; i++) {
      writeBlock(data, size, bitmapBlock + i, dosOrder,
                 bitmapBackup.data() + static_cast<size_t>(i) * BLOCK_SIZE);
    }
  };

  // Reclaim the replaced file's blocks first, so a rewrite at a similar size
  // always fits and nothing is leaked.

  if (replacing) {
    uint8_t block[BLOCK_SIZE];
    readBlock(data, size, entryBlock, dosOrder, block);
    const uint8_t* entry = block + entryOffset;
    freeFileBlocks(data, size, dosOrder, bitmapBlock,
                   static_cast<uint8_t>((entry[0x00] >> 4) & 0x0F),
                   entry[0x11] | (entry[0x12] << 8),
                   entry[0x15] | (entry[0x16] << 8) | (entry[0x17] << 16));
  }

  // ------------------------------------------------------------------
  // Allocate every block before writing anything the reader can see
  // ------------------------------------------------------------------
  std::vector<int> dataBlocks;
  dataBlocks.reserve(dataBlockCount);
  int indexBlock = 0;
  bool full = false;

  if (sapling && !allocateBlock(data, size, dosOrder, bitmapBlock, totalBlocks, &indexBlock)) {
    full = true;
  }
  for (uint32_t i = 0; i < dataBlockCount && !full; i++) {
    int block = 0;
    if (allocateBlock(data, size, dosOrder, bitmapBlock, totalBlocks, &block)) {
      dataBlocks.push_back(block);
    } else {
      full = true;
    }
  }

  if (full) {
    restoreBitmap();
    return FsWriteStatus::DiskFull;
  }

  // ------------------------------------------------------------------
  // Data blocks, then the index block that addresses them
  // ------------------------------------------------------------------
  for (size_t i = 0; i < dataBlocks.size(); i++) {
    uint8_t block[BLOCK_SIZE];
    memset(block, 0, BLOCK_SIZE);
    uint32_t offset = static_cast<uint32_t>(i) * BLOCK_SIZE;
    if (offset < fileLen) {
      uint32_t chunk = fileLen - offset;
      if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
      memcpy(block, fileData + offset, chunk);
    }
    writeBlock(data, size, dataBlocks[i], dosOrder, block);
  }

  if (sapling) {
    uint8_t index[BLOCK_SIZE];
    memset(index, 0, BLOCK_SIZE);
    for (size_t i = 0; i < dataBlocks.size(); i++) {
      index[i] = static_cast<uint8_t>(dataBlocks[i] & 0xFF);
      index[i + 256] = static_cast<uint8_t>((dataBlocks[i] >> 8) & 0xFF);
    }
    writeBlock(data, size, indexBlock, dosOrder, index);
  }

  // ------------------------------------------------------------------
  // Directory entry
  // ------------------------------------------------------------------
  uint8_t dirBlock[BLOCK_SIZE];
  readBlock(data, size, entryBlock, dosOrder, dirBlock);
  uint8_t* entry = dirBlock + entryOffset;
  memset(entry, 0, ENTRY_LENGTH);

  uint8_t storageType = sapling ? STORAGE_SAPLING : STORAGE_SEEDLING;
  int keyBlock = sapling ? indexBlock : dataBlocks[0];
  int blocksUsed = static_cast<int>(dataBlocks.size()) + (sapling ? 1 : 0);

  entry[0x00] = static_cast<uint8_t>((storageType << 4) | nameLen);
  memcpy(entry + 0x01, name, static_cast<size_t>(nameLen));
  entry[0x10] = fileType;
  entry[0x11] = static_cast<uint8_t>(keyBlock & 0xFF);
  entry[0x12] = static_cast<uint8_t>((keyBlock >> 8) & 0xFF);
  entry[0x13] = static_cast<uint8_t>(blocksUsed & 0xFF);
  entry[0x14] = static_cast<uint8_t>((blocksUsed >> 8) & 0xFF);
  entry[0x15] = static_cast<uint8_t>(fileLen & 0xFF);
  entry[0x16] = static_cast<uint8_t>((fileLen >> 8) & 0xFF);
  entry[0x17] = static_cast<uint8_t>((fileLen >> 16) & 0xFF);
  // Creation and modification stamps stay zero: ProDOS shows <NO DATE> rather
  // than a wrong date, and the core has no clock of its own to ask.
  entry[0x1E] = 0xC3; // Destroy, rename, write and read enabled
  entry[0x1F] = static_cast<uint8_t>(auxType & 0xFF);
  entry[0x20] = static_cast<uint8_t>((auxType >> 8) & 0xFF);
  entry[0x25] = static_cast<uint8_t>(VOLUME_DIR_BLOCK & 0xFF);
  entry[0x26] = static_cast<uint8_t>((VOLUME_DIR_BLOCK >> 8) & 0xFF);

  writeBlock(data, size, entryBlock, dosOrder, dirBlock);

  if (!replacing) {
    readBlock(data, size, VOLUME_DIR_BLOCK, dosOrder, header);
    int fileCount = header[0x25] | (header[0x26] << 8);
    fileCount++;
    header[0x25] = static_cast<uint8_t>(fileCount & 0xFF);
    header[0x26] = static_cast<uint8_t>((fileCount >> 8) & 0xFF);
    writeBlock(data, size, VOLUME_DIR_BLOCK, dosOrder, header);
  }

  return FsWriteStatus::OK;
}

int ProDOS::readDirectory(const uint8_t* data, size_t size,
                          int startBlock, const char* pathPrefix,
                          ProDOSCatalogEntry* entries, int maxEntries) {
  ProDOSVolumeInfo info;
  if (!parseVolumeInfo(data, size, &info)) return 0;

  return readDirectoryEntries(data, size, startBlock, pathPrefix,
                              info.useDOSSectorOrder, entries, maxEntries, 0);
}

} // namespace a2e
