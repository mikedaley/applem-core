#pragma once

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

private:
  static int getSectorOffset(int track, int sector);
  static const uint8_t* readSector(const uint8_t* data, size_t size, int track, int sector);
  static void parseFilename(const uint8_t* bytes, char* out, int maxLen);
  static const char* getFileTypeName(uint8_t fileType);
};

} // namespace a2e
