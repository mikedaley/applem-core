/*
 * dos33.cpp - DOS 3.3 filesystem reader for disk image browsing
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "dos33.hpp"
#include <cstring>
#include <vector>

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

// ============================================================================
// Writing
// ============================================================================

uint8_t* DOS33::writeableSector(uint8_t* data, size_t size, int track, int sector) {
  if (track < 0 || sector < 0 || sector >= SECTORS_PER_TRACK) return nullptr;
  int offset = getSectorOffset(track, sector);
  if (offset < 0 || offset + BYTES_PER_SECTOR > static_cast<int>(size)) return nullptr;
  return data + offset;
}

bool DOS33::isSectorFree(const uint8_t* vtoc, int track, int sector) {
  int byteIndex = (sector < 8) ? 1 : 0;
  int bit = sector & 7;
  return (vtoc[0x38 + track * 4 + byteIndex] >> bit) & 1;
}

void DOS33::markSectorUsed(uint8_t* vtoc, int track, int sector) {
  int byteIndex = (sector < 8) ? 1 : 0;
  int bit = sector & 7;
  vtoc[0x38 + track * 4 + byteIndex] &= static_cast<uint8_t>(~(1 << bit));
}

void DOS33::markSectorFree(uint8_t* vtoc, int track, int sector) {
  int byteIndex = (sector < 8) ? 1 : 0;
  int bit = sector & 7;
  vtoc[0x38 + track * 4 + byteIndex] |= static_cast<uint8_t>(1 << bit);
}

bool DOS33::allocateSector(uint8_t* vtoc, int trackCount, int* track, int* sector) {
  // DOS itself sweeps outward from the catalog track; the bitmap is the only
  // thing that has to be right, so a plain low-to-high sweep is enough. Tracks
  // holding DOS and the catalog are already marked in use on a formatted disk.
  for (int t = 1; t < trackCount; t++) {
    for (int s = SECTORS_PER_TRACK - 1; s >= 0; s--) {
      if (isSectorFree(vtoc, t, s)) {
        markSectorUsed(vtoc, t, s);
        *track = t;
        *sector = s;
        vtoc[0x30] = static_cast<uint8_t>(t); // Last track allocated
        vtoc[0x31] = 1;                       // Allocation direction
        return true;
      }
    }
  }
  return false;
}

bool DOS33::normaliseFilename(const char* filename, uint8_t* out30) {
  if (!filename) return false;

  // DOS pads with high-bit spaces
  for (int i = 0; i < MAX_FILENAME; i++) out30[i] = ' ' | 0x80;

  int len = 0;
  for (const char* p = filename; *p; p++) {
    char c = *p;
    if (len >= MAX_FILENAME) return false;
    // A comma ends the filename as far as DOS's command parser is concerned,
    // and control characters cannot be typed back in, so neither can be part
    // of a name we create.
    if (c == ',' || static_cast<unsigned char>(c) < 0x20 ||
        static_cast<unsigned char>(c) > 0x7E) {
      return false;
    }
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    out30[len++] = static_cast<uint8_t>(c) | 0x80;
  }

  // A name that is empty or all spaces would be unreachable from DOS
  if (len == 0) return false;
  for (int i = 0; i < len; i++) {
    if ((out30[i] & 0x7F) != ' ') return true;
  }
  return false;
}

void DOS33::freeFileChain(uint8_t* data, size_t size, uint8_t* vtoc,
                          int track, int sector) {
  bool visited[TRACKS * SECTORS_PER_TRACK] = {};

  while (track != 0) {
    int key = track * SECTORS_PER_TRACK + sector;
    if (key < 0 || key >= TRACKS * SECTORS_PER_TRACK || visited[key]) break;
    visited[key] = true;

    const uint8_t* tsList = readSector(data, size, track, sector);
    if (!tsList) break;

    for (int i = 0x0C; i < BYTES_PER_SECTOR; i += 2) {
      int t = tsList[i];
      int s = tsList[i + 1];
      if (t == 0 && s == 0) continue;
      if (t < TRACKS && s < SECTORS_PER_TRACK) markSectorFree(vtoc, t, s);
    }

    int nextTrack = tsList[0x01];
    int nextSector = tsList[0x02];
    markSectorFree(vtoc, track, sector);
    track = nextTrack;
    sector = nextSector;
  }
}

FsWriteStatus DOS33::writeFile(uint8_t* data, size_t size, const char* filename,
                               uint8_t fileType, const uint8_t* fileData,
                               size_t fileLen) {
  if (size < static_cast<size_t>(DISK_SIZE)) return FsWriteStatus::ImageTooSmall;
  if (!isDOS33(data, size)) return FsWriteStatus::NotFormatted;

  uint8_t name[MAX_FILENAME];
  if (!normaliseFilename(filename, name)) return FsWriteStatus::InvalidName;

  uint8_t* vtoc = writeableSector(data, size, 17, 0);
  if (!vtoc) return FsWriteStatus::NotFormatted;

  int trackCount = vtoc[0x34];
  if (trackCount <= 0 || trackCount > TRACKS) trackCount = TRACKS;

  // Sectors needed: one per 256 bytes of data, plus a T/S list sector for each
  // 122 data sectors. A file of zero length still occupies one T/S list sector.
  size_t dataSectors = (fileLen + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
  size_t listSectors = (dataSectors + TS_PAIRS_PER_LIST - 1) / TS_PAIRS_PER_LIST;
  if (listSectors == 0) listSectors = 1;
  size_t totalSectors = dataSectors + listSectors;
  if (totalSectors > 0xFFFF) return FsWriteStatus::FileTooLarge;

  // ------------------------------------------------------------------
  // Locate the catalog entry to use: an existing file of the same name is
  // replaced in place, otherwise the first free or deleted slot is claimed.
  // ------------------------------------------------------------------
  uint8_t* entry = nullptr;
  uint8_t* freeEntry = nullptr;
  int existingTrack = 0, existingSector = 0;

  {
    int track = vtoc[0x01];
    int sector = vtoc[0x02];
    bool visited[TRACKS * SECTORS_PER_TRACK] = {};

    while (track != 0) {
      int key = track * SECTORS_PER_TRACK + sector;
      if (key < 0 || key >= TRACKS * SECTORS_PER_TRACK || visited[key]) break;
      visited[key] = true;

      uint8_t* catSector = writeableSector(data, size, track, sector);
      if (!catSector) break;

      for (int i = 0; i < CATALOG_ENTRIES_PER_SECTOR; i++) {
        uint8_t* e = catSector + 0x0B + i * CATALOG_ENTRY_SIZE;
        uint8_t firstTrack = e[0x00];

        if (firstTrack == 0x00 || firstTrack == 0xFF) {
          if (!freeEntry) freeEntry = e;
          continue;
        }

        if (memcmp(e + 0x03, name, MAX_FILENAME) == 0) {
          if (e[0x02] & 0x80) return FsWriteStatus::FileLocked;
          entry = e;
          existingTrack = firstTrack;
          existingSector = e[0x01];
          break;
        }
      }
      if (entry) break;

      track = catSector[0x01];
      sector = catSector[0x02];
    }
  }

  if (!entry) entry = freeEntry;
  if (!entry) return FsWriteStatus::DirectoryFull;

  // Reclaim the replaced file's sectors before allocating, so a file that is
  // rewritten at a similar size always fits.
  if (existingTrack != 0) {
    freeFileChain(data, size, vtoc, existingTrack, existingSector);
  }

  // ------------------------------------------------------------------
  // Allocate every sector up front. Nothing is written to the catalog until
  // the whole file has somewhere to live, so a full disk leaves the image
  // exactly as it was apart from the freed sectors of the replaced file.
  // ------------------------------------------------------------------
  uint8_t vtocBackup[BYTES_PER_SECTOR];
  memcpy(vtocBackup, vtoc, BYTES_PER_SECTOR);

  struct SectorRef { int track; int sector; };
  std::vector<SectorRef> listRefs;
  std::vector<SectorRef> dataRefs;
  listRefs.reserve(listSectors);
  dataRefs.reserve(dataSectors);

  bool full = false;
  for (size_t i = 0; i < listSectors && !full; i++) {
    SectorRef ref{};
    if (allocateSector(vtoc, trackCount, &ref.track, &ref.sector)) {
      listRefs.push_back(ref);
    } else {
      full = true;
    }
  }
  for (size_t i = 0; i < dataSectors && !full; i++) {
    SectorRef ref{};
    if (allocateSector(vtoc, trackCount, &ref.track, &ref.sector)) {
      dataRefs.push_back(ref);
    } else {
      full = true;
    }
  }

  if (full) {
    memcpy(vtoc, vtocBackup, BYTES_PER_SECTOR);
    return FsWriteStatus::DiskFull;
  }

  // ------------------------------------------------------------------
  // Emit data sectors, then the T/S lists that describe them
  // ------------------------------------------------------------------
  for (size_t i = 0; i < dataRefs.size(); i++) {
    uint8_t* sec = writeableSector(data, size, dataRefs[i].track, dataRefs[i].sector);
    if (!sec) return FsWriteStatus::DiskFull;
    size_t offset = i * BYTES_PER_SECTOR;
    size_t chunk = fileLen - offset;
    if (chunk > BYTES_PER_SECTOR) chunk = BYTES_PER_SECTOR;
    memcpy(sec, fileData + offset, chunk);
    if (chunk < BYTES_PER_SECTOR) {
      memset(sec + chunk, 0, BYTES_PER_SECTOR - chunk);
    }
  }

  for (size_t i = 0; i < listRefs.size(); i++) {
    uint8_t* list = writeableSector(data, size, listRefs[i].track, listRefs[i].sector);
    if (!list) return FsWriteStatus::DiskFull;
    memset(list, 0, BYTES_PER_SECTOR);

    if (i + 1 < listRefs.size()) {
      list[0x01] = static_cast<uint8_t>(listRefs[i + 1].track);
      list[0x02] = static_cast<uint8_t>(listRefs[i + 1].sector);
    }

    // Sector offset in the file of the first sector this list describes
    uint16_t sectorOffset = static_cast<uint16_t>(i * TS_PAIRS_PER_LIST);
    list[0x05] = static_cast<uint8_t>(sectorOffset & 0xFF);
    list[0x06] = static_cast<uint8_t>(sectorOffset >> 8);

    for (int p = 0; p < TS_PAIRS_PER_LIST; p++) {
      size_t index = i * TS_PAIRS_PER_LIST + p;
      if (index >= dataRefs.size()) break;
      list[0x0C + p * 2] = static_cast<uint8_t>(dataRefs[index].track);
      list[0x0C + p * 2 + 1] = static_cast<uint8_t>(dataRefs[index].sector);
    }
  }

  // ------------------------------------------------------------------
  // Catalog entry
  // ------------------------------------------------------------------
  entry[0x00] = static_cast<uint8_t>(listRefs[0].track);
  entry[0x01] = static_cast<uint8_t>(listRefs[0].sector);
  entry[0x02] = static_cast<uint8_t>(fileType & 0x7F);
  memcpy(entry + 0x03, name, MAX_FILENAME);
  entry[0x21] = static_cast<uint8_t>(totalSectors & 0xFF);
  entry[0x22] = static_cast<uint8_t>((totalSectors >> 8) & 0xFF);

  return FsWriteStatus::OK;
}

FsWriteStatus DOS33::writeBinaryFile(uint8_t* data, size_t size,
                                     const char* filename, uint16_t loadAddress,
                                     const uint8_t* payload, size_t payloadLen) {
  if (payloadLen > 0xFFFF) return FsWriteStatus::FileTooLarge;

  std::vector<uint8_t> file;
  file.reserve(payloadLen + 4);
  file.push_back(static_cast<uint8_t>(loadAddress & 0xFF));
  file.push_back(static_cast<uint8_t>(loadAddress >> 8));
  file.push_back(static_cast<uint8_t>(payloadLen & 0xFF));
  file.push_back(static_cast<uint8_t>((payloadLen >> 8) & 0xFF));
  file.insert(file.end(), payload, payload + payloadLen);

  return writeFile(data, size, filename, 0x04, file.data(), file.size());
}

} // namespace a2e
