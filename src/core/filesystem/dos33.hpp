/*
 * dos33.hpp - DOS 3.3 filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "fs_write_status.hpp"

#include <cstdint>
#include <cstddef>

namespace a2e {

struct DOS33CatalogEntry {
  char filename[31];       // Null-terminated filename (max 30 chars)
  uint8_t fileType;        // Raw file type byte (0x00=T, 0x01=I, 0x02=A, 0x04=B, etc.)
  char fileTypeName[4];    // Short type name ("T", "I", "A", "B", etc.)
  bool isLocked;
  uint16_t sectorCount;
  uint8_t firstTrack;
  uint8_t firstSector;
};

class DOS33 {
public:
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int DISK_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR; // 143360

  /**
   * Check if disk data is DOS 3.3 format
   */
  static bool isDOS33(const uint8_t* data, size_t size);

  /**
   * Read the catalog from a DOS 3.3 disk image.
   * Returns number of entries written to entries array.
   */
  static int readCatalog(const uint8_t* data, size_t size,
                         DOS33CatalogEntry* entries, int maxEntries);

  /**
   * Read a file's raw data (all sectors concatenated).
   * outBuf must be large enough. Returns number of bytes written.
   */
  static int readFile(const uint8_t* data, size_t size,
                      uint8_t firstTrack, uint8_t firstSector,
                      uint8_t* outBuf, int outMax);

  /**
   * Get binary file header info (load address and length).
   * Returns false if fileData is too small.
   */
  static bool getBinaryFileInfo(const uint8_t* fileData, size_t size,
                                uint16_t* address, uint16_t* length);

  // ===== Writing =====

  /**
   * Write a file to a DOS 3.3 image, replacing any existing file of the same
   * name. Sectors are taken from the VTOC free bitmap; a replaced file's
   * sectors are returned to it first, so overwriting in place does not leak
   * space.
   *
   * @param data      Mutable disk image (DOS sector order)
   * @param size      Image size; must be at least DISK_SIZE
   * @param filename  Name to write (case is folded up, trailing spaces added)
   * @param fileType  Raw type byte (0x00=T, 0x02=A, 0x04=B, ...)
   * @param fileData  File contents exactly as they should appear on disk
   * @param fileLen   Length of fileData in bytes
   */
  static FsWriteStatus writeFile(uint8_t* data, size_t size,
                                 const char* filename, uint8_t fileType,
                                 const uint8_t* fileData, size_t fileLen);

  /**
   * Write a binary (type B) file. DOS binaries begin with a 4-byte
   * address/length header, which this synthesises — pass the payload alone.
   */
  static FsWriteStatus writeBinaryFile(uint8_t* data, size_t size,
                                       const char* filename,
                                       uint16_t loadAddress,
                                       const uint8_t* payload,
                                       size_t payloadLen);

private:
  static int getSectorOffset(int track, int sector);
  static const uint8_t* readSector(const uint8_t* data, size_t size, int track, int sector);
  static uint8_t* writeableSector(uint8_t* data, size_t size, int track, int sector);
  static void parseFilename(const uint8_t* bytes, char* out, int maxLen);
  static const char* getFileTypeName(uint8_t fileType);

  // ===== Write helpers =====

  // Maximum track/sector pairs a single T/S list sector can describe
  static constexpr int TS_PAIRS_PER_LIST = 122;
  static constexpr int CATALOG_ENTRY_SIZE = 35;
  static constexpr int CATALOG_ENTRIES_PER_SECTOR = 7;
  static constexpr int MAX_FILENAME = 30;

  // VTOC free-sector bitmap: 4 bytes per track at 0x38. The first byte holds
  // sectors 15..8 (bit 7 = sector 15), the second sectors 7..0.
  static bool isSectorFree(const uint8_t* vtoc, int track, int sector);
  static void markSectorUsed(uint8_t* vtoc, int track, int sector);
  static void markSectorFree(uint8_t* vtoc, int track, int sector);
  static bool allocateSector(uint8_t* vtoc, int trackCount, int* track, int* sector);

  // Fold a host filename into the 30-character, high-bit, space-padded form
  // DOS stores. Returns false if the name cannot be represented.
  static bool normaliseFilename(const char* filename, uint8_t* out30);

  // Release every sector held by the file whose T/S list starts at track/sector
  static void freeFileChain(uint8_t* data, size_t size, uint8_t* vtoc,
                            int track, int sector);
};

} // namespace a2e
