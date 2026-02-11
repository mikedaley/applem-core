/*
 * pascal.hpp - Apple Pascal filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <cstddef>

namespace a2e {

struct PascalCatalogEntry {
  char filename[16];        // Max 15 chars + null
  uint8_t fileType;         // 0-8
  char fileTypeName[5];     // "CODE", "TEXT", "DATA", etc.
  uint16_t startBlock;      // First block of file
  uint16_t nextBlock;       // First block past end
  uint16_t bytesInLastBlock;// Bytes used in final block
  uint32_t fileSize;        // Computed: (nextBlock - startBlock - 1) * 512 + bytesInLastBlock
  uint16_t modDate;         // Raw Pascal date
};

struct PascalVolumeInfo {
  char volumeName[8];       // Max 7 chars + null
  uint16_t totalBlocks;
  uint16_t fileCount;
  bool useDOSSectorOrder;   // DOS order (.DO/.DSK) vs ProDOS order (.PO)
};

class Pascal {
public:
  static constexpr int BLOCK_SIZE = 512;
  static constexpr int DISK_140K_SIZE = 143360;
  static constexpr int DIR_START_BLOCK = 2;
  static constexpr int DIR_BLOCKS = 4;        // Blocks 2-5
  static constexpr int DIR_ENTRY_SIZE = 26;
  static constexpr int MAX_DIR_ENTRIES = 77;   // (2048 / 26) - 1

  /**
   * Check if disk data is Apple Pascal format.
   * Should be called after isProDOS() fails to avoid false positives.
   */
  static bool isPascal(const uint8_t* data, size_t size);

  /**
   * Parse volume information
   */
  static bool parseVolumeInfo(const uint8_t* data, size_t size, PascalVolumeInfo* info);

  /**
   * Read the catalog (flat directory).
   * Returns number of entries written.
   */
  static int readCatalog(const uint8_t* data, size_t size,
                         PascalCatalogEntry* entries, int maxEntries);

  /**
   * Read a file's contents based on its catalog entry.
   * Returns bytes written.
   */
  static int readFile(const uint8_t* data, size_t size,
                      const PascalCatalogEntry* entry,
                      uint8_t* outBuf, int outMax);

  /**
   * Map Pascal file type to DOS 3.3 type for viewer.
   * Returns -1 if no mapping (use hex dump).
   */
  static int mapFileTypeForViewer(uint8_t pascalType);

private:
  static void readBlock(const uint8_t* data, size_t size, int blockNum,
                        bool dosOrder, uint8_t* out);
  static bool detectSectorOrder(const uint8_t* data, size_t size, bool* dosOrder);
  static bool isValidVolumeHeader(const uint8_t* dirData);
  static const char* getFileTypeName(uint8_t fileType);

  // ProDOS-to-DOS sector conversion table (same interleave as ProDOS)
  static constexpr uint8_t PRODOS_TO_DOS_SECTOR[16] = {
    0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15
  };
};

} // namespace a2e
