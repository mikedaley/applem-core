#include "dos33.hpp"
#include <cstring>

namespace a2e {

int DOS33::getSectorOffset(int track, int sector) {
  return (track * SECTORS_PER_TRACK + sector) * BYTES_PER_SECTOR;
}

const uint8_t* DOS33::readSector(const uint8_t* data, size_t size, int track, int sector) {
  int offset = getSectorOffset(track, sector);
  if (offset + BYTES_PER_SECTOR > static_cast<int>(size)) return nullptr;
  return data + offset;
}

void DOS33::parseFilename(const uint8_t* bytes, char* out, int maxLen) {
  // DOS 3.3 filenames are 30 bytes, high bit set, space-padded
  int len = 0;
  int lastNonSpace = -1;
  for (int i = 0; i < 30 && len < maxLen - 1; i++) {
    char c = bytes[i] & 0x7F; // Strip high bit
    out[len] = c;
    if (c != ' ') lastNonSpace = len;
    len++;
  }
  // Trim trailing spaces
  out[lastNonSpace + 1] = '\0';
}

const char* DOS33::getFileTypeName(uint8_t fileType) {
  switch (fileType) {
    case 0x00: return "T";
    case 0x01: return "I";
    case 0x02: return "A";
    case 0x04: return "B";
    case 0x08: return "S";
    case 0x10: return "R";
    case 0x20: return "a";
    case 0x40: return "b";
    default:   return "?";
  }
}

bool DOS33::isDOS33(const uint8_t* data, size_t size) {
  if (size < static_cast<size_t>(DISK_SIZE)) return false;

  const uint8_t* vtoc = readSector(data, size, 17, 0);
  if (!vtoc) return false;

  uint8_t catalogTrack = vtoc[0x01];
  uint8_t dosVersion = vtoc[0x03];

  return (catalogTrack == 0x11 && dosVersion == 0x03);
}

int DOS33::readCatalog(const uint8_t* data, size_t size,
                       DOS33CatalogEntry* entries, int maxEntries) {
  if (size < static_cast<size_t>(DISK_SIZE)) return 0;

  const uint8_t* vtoc = readSector(data, size, 17, 0);
  if (!vtoc) return 0;

  uint8_t catalogTrack = vtoc[0x01];
  uint8_t dosVersion = vtoc[0x03];
  if (catalogTrack != 0x11 || dosVersion != 0x03) return 0;

  int count = 0;
  int track = vtoc[0x01];
  int sector = vtoc[0x02];

  // Visited set for cycle detection (max 560 sectors on a 35-track disk)
  bool visited[35 * 16] = {};

  while (track != 0 && sector != 0 && count < maxEntries) {
    int key = track * 16 + sector;
    if (key < 0 || key >= 35 * 16 || visited[key]) break;
    visited[key] = true;

    const uint8_t* catSector = readSector(data, size, track, sector);
    if (!catSector) break;

    // Parse entries (7 entries per sector, starting at offset 0x0B)
    for (int i = 0; i < 7 && count < maxEntries; i++) {
      int entryOff = 0x0B + (i * 35);
      uint8_t firstTrack = catSector[entryOff + 0x00];
      uint8_t firstSector = catSector[entryOff + 0x01];

      if (firstTrack == 0xFF || firstTrack == 0x00) continue;

      uint8_t typeAndFlags = catSector[entryOff + 0x02];
      uint8_t fileType = typeAndFlags & 0x7F;
      bool isLocked = (typeAndFlags & 0x80) != 0;

      DOS33CatalogEntry& entry = entries[count];
      parseFilename(catSector + entryOff + 0x03, entry.filename, sizeof(entry.filename));
      entry.fileType = fileType;
      const char* typeName = getFileTypeName(fileType);
      strncpy(entry.fileTypeName, typeName, sizeof(entry.fileTypeName) - 1);
      entry.fileTypeName[sizeof(entry.fileTypeName) - 1] = '\0';
      entry.isLocked = isLocked;
      entry.sectorCount = catSector[entryOff + 0x21] | (catSector[entryOff + 0x22] << 8);
      entry.firstTrack = firstTrack;
      entry.firstSector = firstSector;

      count++;
    }

    // Next catalog sector
    track = catSector[0x01];
    sector = catSector[0x02];
  }

  return count;
}

int DOS33::readFile(const uint8_t* data, size_t size,
                    uint8_t firstTrack, uint8_t firstSector,
                    uint8_t* outBuf, int outMax) {
  if (size < static_cast<size_t>(DISK_SIZE)) return 0;

  int bytesWritten = 0;
  int track = firstTrack;
  int sector = firstSector;

  bool visited[35 * 16] = {};

  // Follow T/S list chain
  while (track != 0) {
    int key = track * 16 + sector;
    if (key < 0 || key >= 35 * 16 || visited[key]) break;
    visited[key] = true;

    const uint8_t* tsList = readSector(data, size, track, sector);
    if (!tsList) break;

    // Read sector pairs from T/S list (starting at offset 0x0C)
    for (int i = 0x0C; i < 0x100; i += 2) {
      int t = tsList[i];
      int s = tsList[i + 1];
      if (t == 0 && s == 0) break;

      const uint8_t* sectorData = readSector(data, size, t, s);
      if (!sectorData) continue;

      int bytesToCopy = BYTES_PER_SECTOR;
      if (bytesWritten + bytesToCopy > outMax) {
        bytesToCopy = outMax - bytesWritten;
      }
      if (bytesToCopy > 0) {
        memcpy(outBuf + bytesWritten, sectorData, bytesToCopy);
        bytesWritten += bytesToCopy;
      }
    }

    // Next T/S list sector
    track = tsList[0x01];
    sector = tsList[0x02];
  }

  return bytesWritten;
}

bool DOS33::getBinaryFileInfo(const uint8_t* fileData, size_t size,
                              uint16_t* address, uint16_t* length) {
  if (size < 4) return false;
  *address = fileData[0] | (fileData[1] << 8);
  *length = fileData[2] | (fileData[3] << 8);
  return true;
}

} // namespace a2e
