/*
 * prodos.hpp - ProDOS filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../disk-image/gcr_encoding.hpp"
#include "fs_write_status.hpp"

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
   * Read a single directory's entries (no recursion into subdirectories).
   * startBlock=2 for root directory, or a subdirectory's keyPointer.
   * Returns number of entries written.
   */
  static int readDirectory(const uint8_t* data, size_t size,
                           int startBlock, const char* pathPrefix,
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

  // ===== Writing =====

  /**
   * Write a file into the volume directory, replacing any existing file of the
   * same name. Blocks come from the volume bitmap; a replaced file's blocks are
   * returned to it first.
   *
   * Only the volume directory is written to, and only seedling and sapling
   * files are produced (up to 128KB) — enough for anything an assembler emits,
   * and the volume directory is fixed-size so it cannot be grown.
   *
   * @param data      Mutable disk image, in either sector order
   * @param size      Image size in bytes
   * @param filename  Name to write (case is folded up)
   * @param fileType  ProDOS file type (0x06 = BIN)
   * @param auxType   Aux type (load address for BIN)
   * @param fileData  File contents
   * @param fileLen   Length of fileData in bytes
   */
  static FsWriteStatus writeFile(uint8_t* data, size_t size, const char* filename,
                                 uint8_t fileType, uint16_t auxType,
                                 const uint8_t* fileData, uint32_t fileLen);

private:
  static void readBlock(const uint8_t* data, size_t size, int blockNum,
                        bool dosOrder, uint8_t* out);
  static void writeBlock(uint8_t* data, size_t size, int blockNum,
                         bool dosOrder, const uint8_t* in);
  static bool isValidVolumeHeader(const uint8_t* block);
  static bool detectSectorOrder(const uint8_t* data, size_t size, bool* dosOrder);
  static void parseFilename(const uint8_t* bytes, int nameLen, char* out, int maxLen);
  static const char* getFileTypeName(uint8_t fileType);


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

  // ===== Write helpers =====

  static constexpr int ENTRY_LENGTH = 0x27;
  static constexpr int ENTRIES_PER_BLOCK = 0x0D;
  static constexpr int VOLUME_DIR_BLOCK = 2;
  static constexpr int MAX_PRODOS_NAME = 15;

  // Volume bitmap: one bit per block, 1 = free, MSB of each byte is the lowest
  // numbered block. The bitmap starts at the block named in the volume header
  // and runs on into following blocks for volumes over 4096 blocks.
  static bool isBlockFree(const uint8_t* data, size_t size, bool dosOrder,
                          int bitmapBlock, int blockNum);
  static void setBlockAllocated(uint8_t* data, size_t size, bool dosOrder,
                                int bitmapBlock, int blockNum, bool allocated);
  static bool allocateBlock(uint8_t* data, size_t size, bool dosOrder,
                            int bitmapBlock, int totalBlocks, int* blockNum);

  // Fold a host filename into ProDOS's uppercase letters/digits/period form
  static bool normaliseFilename(const char* filename, char* out, int maxLen);

  // Release every block held by the file described by a directory entry
  static void freeFileBlocks(uint8_t* data, size_t size, bool dosOrder,
                             int bitmapBlock, uint8_t storageType,
                             int keyBlock, uint32_t eof);
};

} // namespace a2e
