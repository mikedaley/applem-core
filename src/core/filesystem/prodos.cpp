#include "prodos.hpp"
#include <cstring>
#include <cstdio>

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

  // Visited set for cycle detection
  bool visited[1024] = {};

  while (blockNum != 0 && count < maxEntries) {
    if (blockNum >= 1024 || visited[blockNum]) break;
    visited[blockNum] = true;

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

} // namespace a2e
