#pragma once

#include <cstdint>
#include <cstddef>

namespace a2e {

struct ProDOSCatalogEntry {
  char filename[16];       // Null-terminated (max 15 chars)
  char path[128];          // Full path including subdirectory
  uint8_t fileType;        // ProDOS file type
  char fileTypeName[4];    // Short type name
  uint8_t storageType;     // 1=seedling, 2=sapling, 3=tree, 0xD=subdir
  uint16_t keyPointer;     // Key block
  uint16_t blocksUsed;
  uint32_t eof;            // File size (24-bit)
  uint16_t auxType;        // Aux type (load address for BIN)
  uint8_t access;
  bool isLocked;
  bool isDirectory;
};

struct ProDOSVolumeInfo {
  char volumeName[16];     // Null-terminated
  uint16_t totalBlocks;
  uint16_t fileCount;
  bool useDOSSectorOrder;
};

class ProDOS {
public:
  static constexpr int BLOCK_SIZE = 512;
  static constexpr int DISK_140K_SIZE = 143360;

  /**
   * Check if disk data is ProDOS format
   */
  static bool isProDOS(const uint8_t* data, size_t size);

  /**
   * Parse volume information
   */
  static bool parseVolumeInfo(const uint8_t* data, size_t size, ProDOSVolumeInfo* info);

  /**
   * Read the full catalog (including subdirectories).
   * Returns number of entries written.
   */
  static int readCatalog(const uint8_t* data, size_t size,
                         ProDOSCatalogEntry* entries, int maxEntries);

  /**
   * Read a file's contents based on its catalog entry.
   * outBuf must be large enough (at least entry.eof bytes).
   * Returns bytes written.
   */
  static int readFile(const uint8_t* data, size_t size,
                      const ProDOSCatalogEntry* entry,
                      uint8_t* outBuf, int outMax);

  /**
   * Map ProDOS file type to DOS 3.3 type for viewer.
   * Returns -1 if no mapping (use hex dump).
   */
  static int mapFileTypeForViewer(uint8_t prodosType);

private:
  static void readBlock(const uint8_t* data, size_t size, int blockNum,
                        bool dosOrder, uint8_t* out);
  static bool isValidVolumeHeader(const uint8_t* block);
  static bool detectSectorOrder(const uint8_t* data, size_t size, bool* dosOrder);
  static void parseFilename(const uint8_t* bytes, int nameLen, char* out, int maxLen);
  static const char* getFileTypeName(uint8_t fileType);

  // ProDOS-to-DOS sector conversion table
  static constexpr uint8_t PRODOS_TO_DOS_SECTOR[16] = {
    0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15
  };

  // Storage types
  static constexpr uint8_t STORAGE_DELETED = 0x0;
  static constexpr uint8_t STORAGE_SEEDLING = 0x1;
  static constexpr uint8_t STORAGE_SAPLING = 0x2;
  static constexpr uint8_t STORAGE_TREE = 0x3;
  static constexpr uint8_t STORAGE_SUBDIR = 0xD;
  static constexpr uint8_t STORAGE_SUBDIR_HEADER = 0xE;
  static constexpr uint8_t STORAGE_VOLUME_HEADER = 0xF;

  static int readSeedlingFile(const uint8_t* data, size_t size, int blockNum,
                              int eof, bool dosOrder, uint8_t* out, int outMax);
  static int readSaplingFile(const uint8_t* data, size_t size, int indexBlock,
                             int eof, bool dosOrder, uint8_t* out, int outMax);
  static int readTreeFile(const uint8_t* data, size_t size, int masterBlock,
                          int eof, bool dosOrder, uint8_t* out, int outMax);

  static int readDirectoryEntries(const uint8_t* data, size_t size,
                                  int startBlock, const char* pathPrefix,
                                  bool dosOrder,
                                  ProDOSCatalogEntry* entries, int maxEntries,
                                  int currentCount);
};

} // namespace a2e
