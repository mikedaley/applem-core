/*
 * dsk_disk_image.cpp - DSK/DO/PO disk image format implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "dsk_disk_image.hpp"
#include "gcr_encoding.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

DskDiskImage::DskDiskImage() { sector_data_.fill(0); }

void DskDiskImage::resetState() {
  quarter_track_ = 0;
  phase_states_ = 0;
  nibble_position_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;
}

bool DskDiskImage::load(const uint8_t *data, size_t size,
                        const std::string &filename) {
  // Check file size
  if (size != DISK_SIZE) {
    return false;
  }

  // Copy sector data
  std::memcpy(sector_data_.data(), data, DISK_SIZE);

  loaded_ = true;
  modified_ = false;
  filename_ = filename;

  // Detect format from disk content (not file extension)
  format_ = detectFormat(filename);

  // Invalidate all nibble and bit tracks (will be regenerated on demand)
  for (auto &track : nibble_tracks_) {
    track.valid = false;
    track.dirty = false;
    track.nibbles.clear();
  }
  for (auto &track : bit_tracks_) {
    track.valid = false;
    track.dirty = false;
    track.bits.clear();
    track.bit_count = 0;
  }

  // Reset head position
  quarter_track_ = 0;
  phase_states_ = 0;
  nibble_position_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;

  return true;
}

bool DskDiskImage::loadAs(const uint8_t *data, size_t size,
                          const std::string &filename, Format format) {
  if (format != Format::DSK && format != Format::DO && format != Format::PO) {
    return false;
  }
  if (!load(data, size, filename)) {
    return false;
  }
  // load() detected an order from the content; the caller knows better
  if (format_ != format) {
    format_ = format;
    for (auto &track : nibble_tracks_) {
      track.valid = false;
      track.nibbles.clear();
      track.is_sync.clear();
    }
    for (auto &track : bit_tracks_) {
      track.valid = false;
      track.bits.clear();
      track.bit_count = 0;
    }
  }
  return true;
}

bool DskDiskImage::getTrackBits(int track, std::vector<uint8_t> &bits,
                                uint32_t &bitCount) {
  if (!loaded_ || track < 0 || track >= TRACKS) {
    return false;
  }

  if (!nibble_tracks_[track].valid) {
    nibblizeTrack(track);
  }
  bitifyTrack(track);

  const auto &bt = bit_tracks_[track];
  if (!bt.valid || bt.bit_count == 0) {
    return false;
  }

  bits = bt.bits;
  bitCount = bt.bit_count;
  return true;
}

int DskDiskImage::countCatalogChain(int track, int sector,
                                    bool prodosOrder) const {
  int links = 0;
  bool visited[TRACKS * SECTORS_PER_TRACK] = {};

  while (track > 0 && track < TRACKS && sector >= 0 &&
         sector < SECTORS_PER_TRACK) {
    int key = track * SECTORS_PER_TRACK + sector;
    if (visited[key]) break;
    visited[key] = true;

    // GCR::PRODOS_TO_DOS_SECTOR reads a track's sectors out of an image
    // laid out in the other order
    int stored = prodosOrder ? GCR::PRODOS_TO_DOS_SECTOR[sector] : sector;
    size_t offset =
        static_cast<size_t>(track * SECTORS_PER_TRACK + stored) * BYTES_PER_SECTOR;
    if (offset + BYTES_PER_SECTOR > sector_data_.size()) break;

    const uint8_t *catalog = sector_data_.data() + offset;
    int nextTrack = catalog[0x01];
    int nextSector = catalog[0x02];

    // The end of the chain is a zero link, which is as valid as any other
    if (nextTrack == 0 && nextSector == 0) {
      links++;
      break;
    }
    // A catalog sector always links to another sector on the catalog track
    if (nextTrack != track || nextSector >= SECTORS_PER_TRACK) break;

    links++;
    sector = nextSector;
  }

  return links;
}

DiskImage::Format DskDiskImage::detectFormat(const std::string &filename) const {
  // Content-based format detection
  // We check for filesystem signatures to determine if data is in DOS or ProDOS
  // order

  // Check for ProDOS volume header assuming ProDOS sector order
  // Block 2 in ProDOS = offset 1024
  constexpr int PRODOS_BLOCK2_OFFSET = 1024;
  if (sector_data_.size() > PRODOS_BLOCK2_OFFSET + 5) {
    uint8_t storage_type = sector_data_[PRODOS_BLOCK2_OFFSET + 4];
    // High nibble 0xF = volume directory header, low nibble = name length
    if ((storage_type & 0xF0) == 0xF0) {
      int name_len = storage_type & 0x0F;
      if (name_len > 0 && name_len <= 15) {
        // Verify the name contains valid ProDOS characters (letters, digits,
        // periods). ProDOS stores characters with high bit set (0x80 OR'd),
        // so we mask it off before checking.
        bool valid_name = true;
        for (int i = 0; i < name_len && valid_name; i++) {
          uint8_t c = sector_data_[PRODOS_BLOCK2_OFFSET + 5 + i];
          uint8_t ch = c & 0x7F;  // Strip high bit
          // ProDOS names: A-Z, 0-9, period
          bool is_letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
          bool is_digit = (ch >= '0' && ch <= '9');
          bool is_period = (ch == '.');
          if (!is_letter && !is_digit && !is_period) {
            valid_name = false;
          }
        }
        if (valid_name) {
          return Format::PO;
        }
      }
    }
  }

  // Check for DOS 3.3 VTOC assuming DOS sector order
  // Track 17, Sector 0 = offset 69632
  constexpr int DOS_VTOC_OFFSET = 17 * 16 * 256;
  if (sector_data_.size() > DOS_VTOC_OFFSET + 4) {
    uint8_t catalog_track = sector_data_[DOS_VTOC_OFFSET + 1];
    uint8_t catalog_sector = sector_data_[DOS_VTOC_OFFSET + 2];
    uint8_t dos_version = sector_data_[DOS_VTOC_OFFSET + 3];

    // Valid DOS 3.3 VTOC:
    // - catalog track is typically 17 (0x11) or nearby
    // - catalog sector is typically 15 (0x0F)
    // - DOS version is 3 (0x03)
    bool valid_catalog_track = (catalog_track >= 0x11 && catalog_track <= 0x14);
    bool valid_catalog_sector = (catalog_sector <= 0x0F);
    bool valid_dos_version = (dos_version == 0x03);

    if (valid_catalog_track && valid_catalog_sector && valid_dos_version) {
      // The VTOC alone cannot tell the two orders apart: sector 0 of a track
      // holds the same file offset either way, so a DOS 3.3 volume stored in
      // ProDOS order still has its VTOC exactly here. The catalog chain does
      // distinguish them — the sectors it links to move — so follow it under
      // each order and believe whichever holds together.
      int dosLinks = countCatalogChain(catalog_track, catalog_sector, false);
      int prodosLinks = countCatalogChain(catalog_track, catalog_sector, true);
      return (prodosLinks > dosLinks) ? Format::PO : Format::DSK;
    }
  }

  // Fall back to extension-based detection
  std::string ext = filename;
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext.find(".po") != std::string::npos) {
    return Format::PO;
  }

  // Default to DOS order for .dsk and .do files
  return Format::DSK;
}

std::string DskDiskImage::getFormatName() const {
  switch (format_) {
  case Format::DSK:
    return "DSK (DOS order)";
  case Format::DO:
    return "DO (DOS order)";
  case Format::PO:
    return "PO (ProDOS order)";
  default:
    return "Unknown";
  }
}

int DskDiskImage::getLogicalSector(int physical_sector) const {
  if (physical_sector < 0 || physical_sector >= SECTORS_PER_TRACK)
    return 0;

  if (format_ == Format::PO) {
    return GCR::PRODOS_PHYSICAL_TO_LOGICAL[physical_sector];
  } else {
    return GCR::DOS_PHYSICAL_TO_LOGICAL[physical_sector];
  }
}

void DskDiskImage::nibblizeTrack(int track) {
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  nt.nibbles.clear();
  nt.is_sync.clear();
  nt.nibbles.reserve(NIBBLES_PER_TRACK);
  nt.is_sync.reserve(NIBBLES_PER_TRACK);

  // Helper: push a data byte (8-bit in bit stream)
  auto pushData = [&](uint8_t val) {
    nt.nibbles.push_back(val);
    nt.is_sync.push_back(false);
  };

  // Helper: push a sync byte (10-bit self-sync FF in bit stream)
  auto pushSync = [&]() {
    nt.nibbles.push_back(0xFF);
    nt.is_sync.push_back(true);
  };

  // Build each sector using the exact structure from the working version
  for (int physical_sector = 0; physical_sector < SECTORS_PER_TRACK;
       physical_sector++) {
    // Map physical sector to DOS logical sector
    int dos_sector = getLogicalSector(physical_sector);

    // Get sector data
    int offset = (track * SECTORS_PER_TRACK + dos_sector) * BYTES_PER_SECTOR;
    const uint8_t *data = &sector_data_[offset];

    // Gap 1 (first sector) or Gap 3 (between sectors)
    int gap;
    if (physical_sector == 0) {
      gap = 0x80; // Gap 1: 128 bytes
    } else {
      gap = (track == 0) ? 0x28 : 0x26; // Gap 3: 40 or 38 bytes
    }
    for (int i = 0; i < gap; ++i) {
      pushSync();
    }

    // === Address Field ===
    // Prologue
    pushData(0xD5);
    pushData(0xAA);
    pushData(0x96);

    // 4-and-4 encoded values
    auto encode44 = [&](uint8_t val) {
      pushData((val >> 1) | 0xAA);
      pushData(val | 0xAA);
    };

    uint8_t checksum = volume_number_ ^ track ^ physical_sector;
    encode44(volume_number_);
    encode44(track);
    encode44(physical_sector);
    encode44(checksum);

    // Epilogue
    pushData(0xDE);
    pushData(0xAA);
    pushData(0xEB);

    // Gap 2: 5 bytes
    for (int i = 0; i < 5; ++i) {
      pushSync();
    }

    // === Data Field ===
    // Prologue
    pushData(0xD5);
    pushData(0xAA);
    pushData(0xAD);

    // 6-and-2 encode the sector data
    auto encoded = GCR::encode6and2(data);
    for (auto byte : encoded) {
      pushData(byte);
    }

    // Epilogue
    pushData(0xDE);
    pushData(0xAA);
    pushData(0xEB);

    // Gap 3 end: 1 byte
    pushSync();
  }

  // Pad or truncate to standard track size
  while (nt.nibbles.size() < NIBBLES_PER_TRACK) {
    pushSync();
  }
  if (nt.nibbles.size() > NIBBLES_PER_TRACK) {
    nt.nibbles.resize(NIBBLES_PER_TRACK);
    nt.is_sync.resize(NIBBLES_PER_TRACK);
  }

  nt.valid = true;
  nt.dirty = false;

  // Invalidate corresponding bit track (will be regenerated on demand)
  bit_tracks_[track].valid = false;
  bit_tracks_[track].dirty = false;
  bit_tracks_[track].bits.clear();
  bit_tracks_[track].bit_count = 0;
}

void DskDiskImage::scanAndDecodeSectors(int track, const uint8_t *nibbles,
                                        size_t search_count,
                                        size_t total_count) {
  // Scan a recovered nibble stream for address/data field pairs and decode each
  // sector into sector_data_. `search_count` bounds where a new sector may
  // START (one disk revolution); `total_count` bounds how far a field body may
  // be READ (revolution + wrap-around tail). Shared by the nibble-grid path
  // (denibblizeTrack) and the self-syncing bit path (denibblizeBitTrack).
  size_t pos = 0;
  while (pos < search_count) {
    // Look for address field prologue: D5 AA 96
    bool found_addr = false;
    while (pos + 3 <= total_count && pos < search_count) {
      if (nibbles[pos] == 0xD5 && nibbles[pos + 1] == 0xAA &&
          nibbles[pos + 2] == 0x96) {
        found_addr = true;
        pos += 3;
        break;
      }
      pos++;
    }

    if (!found_addr)
      break;

    // Read address field (4-and-4 encoded: volume, track, sector, checksum)
    if (pos + 8 > total_count)
      break;

    uint8_t volume = GCR::decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t addr_track = GCR::decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t sector = GCR::decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t checksum = GCR::decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;

    // Verify address checksum using the volume read from the disk,
    // not volume_number_ which may be different
    if ((volume ^ addr_track ^ sector) != checksum)
      continue; // Invalid address field

    // Verify track number matches
    if (addr_track != track)
      continue; // Wrong track

    // Guard against a corrupt sector index so we never clobber sector 0
    if (sector >= SECTORS_PER_TRACK)
      continue;

    // Skip address epilogue and look for data prologue: D5 AA AD
    bool found_data = false;
    size_t search_limit = pos + 50; // Don't search too far
    while (pos + 3 <= total_count && pos < search_limit) {
      if (nibbles[pos] == 0xD5 && nibbles[pos + 1] == 0xAA &&
          nibbles[pos + 2] == 0xAD) {
        found_data = true;
        pos += 3;
        break;
      }
      pos++;
    }

    if (!found_data)
      continue;

    // Read and decode 343 nibbles of data field
    if (pos + 343 > total_count)
      break;

    uint8_t decoded[256];
    if (GCR::decode6and2(nibbles + pos, decoded, false)) {
      int log_sector = getLogicalSector(sector);
      int offset = (track * SECTORS_PER_TRACK + log_sector) * BYTES_PER_SECTOR;
      std::memcpy(&sector_data_[offset], decoded, BYTES_PER_SECTOR);
    }

    pos += 343;
  }
}

void DskDiskImage::denibblizeTrack(int track) {
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  if (!nt.valid || !nt.dirty)
    return;

  // Extend the nibble buffer with a copy of the first ~500 nibbles so a sector
  // that wraps past the track index is still decoded whole.
  std::vector<uint8_t> nibbles = nt.nibbles;
  const size_t original_size = nibbles.size();
  const size_t wrap_extension = 500;
  if (original_size > wrap_extension) {
    nibbles.insert(nibbles.end(), nt.nibbles.begin(),
                   nt.nibbles.begin() + wrap_extension);
  }

  scanAndDecodeSectors(track, nibbles.data(), original_size, nibbles.size());

  nt.dirty = false;
  modified_ = true;
}

void DskDiskImage::setPhase(int phase, bool on) {
  if (phase < 0 || phase > 3)
    return;

  uint8_t phase_bit = 1 << phase;

  if (on) {
    phase_states_ |= phase_bit;
  } else {
    phase_states_ &= ~phase_bit;
  }

  // Re-evaluate head position on every magnet change (both ON and OFF). The
  // head is pulled toward a newly energised magnet, so we must step on ON, not
  // only on OFF.
  updateHeadPosition();
}

void DskDiskImage::updateHeadPosition() {
  // Canonical 4-magnet stepper model (as used by AppleWin / OpenEmulator).
  //
  // The head *position* is the state; the magnet currently aligned with the
  // head is derived from the position (each magnet spans 2 quarter-tracks, the
  // four magnets repeating every 8 quarter-tracks). On any magnet-state change
  // we sum the pull from the two neighbouring magnets and, if there is a net
  // pull, move one half-track (2 quarter-tracks) toward the energised
  // neighbour. Deriving the aligned magnet from the position (rather than
  // tracking a separate "current phase") keeps the head in sync through
  // recalibration and non-overlapping step patterns.
  constexpr int MAX_QUARTER_TRACK = (TRACKS * 4) - 1;

  int aligned = (quarter_track_ >> 1) & 3;
  int direction = 0;
  if (phase_states_ & (1 << ((aligned + 1) & 3))) {
    direction += 1; // pull inward (toward higher track numbers)
  }
  if (phase_states_ & (1 << ((aligned + 3) & 3))) {
    direction -= 1; // pull outward (toward track 0)
  }

  if (direction != 0) {
    quarter_track_ = std::max(
        0, std::min(MAX_QUARTER_TRACK, quarter_track_ + 2 * direction));
  }
  // If both or neither neighbouring magnets are energised, the head is settled.
}

bool DskDiskImage::hasData() const {
  int track = quarter_track_ / 4;
  return track >= 0 && track < TRACKS;
}

void DskDiskImage::ensureTrackNibblized() {
  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS) {
    return;
  }

  if (!nibble_tracks_[track].valid) {
    nibblizeTrack(track);
  }
}

void DskDiskImage::advanceBitPosition(uint64_t current_cycles) {
  if (!loaded_)
    return;

  // Calculate elapsed cycles since last update
  if (current_cycles <= last_cycle_count_) {
    last_cycle_count_ = current_cycles;
    return;
  }

  uint64_t elapsed = current_cycles - last_cycle_count_;
  last_cycle_count_ = current_cycles;

  // Disk spins at ~300 RPM = 5 revolutions/second
  // At 1.023 MHz, one revolution = ~204,600 cycles
  // With 6656 nibbles per track, each nibble = ~30.7 cycles
  // Using 31 gives ~297 RPM which is within spec tolerance
  constexpr uint64_t CYCLES_PER_NIBBLE = 31;

  ensureTrackNibblized();

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
    return;

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty())
    return;

  // Advance position based on elapsed time
  uint64_t nibbles_elapsed = elapsed / CYCLES_PER_NIBBLE;
  nibble_position_ = (nibble_position_ + nibbles_elapsed) % nt.nibbles.size();
}

uint8_t DskDiskImage::readNibble() {
  if (!loaded_)
    return 0xFF; // Return sync byte pattern when not loaded

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS)
    return 0xFF; // Return sync byte pattern for invalid track

  ensureTrackNibblized();

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty())
    return 0xFF; // Return sync byte pattern if track not ready

  // Read nibble at current position
  uint8_t nibble = nt.nibbles[nibble_position_];

  // Advance to next nibble
  nibble_position_ = (nibble_position_ + 1) % nt.nibbles.size();

  // All valid disk nibbles must have bit 7 set
  // This is guaranteed by GCR encoding, but verify for safety
  return nibble | 0x80;
}

void DskDiskImage::writeNibble(uint8_t nibble) {
  if (!loaded_ || write_protected_)
    return;

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS) {
    return;
  }

  ensureTrackNibblized();

  auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty()) {
    return;
  }

  // Write nibble at current position
  nt.nibbles[nibble_position_] = nibble;
  nt.dirty = true;
  modified_ = true;

  // Advance to next nibble
  nibble_position_ = (nibble_position_ + 1) % nt.nibbles.size();
}

// ===== Bit-Level Access (for LSS) =====

void DskDiskImage::ensureTrackBitified() {
  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS) return;
  if (bit_tracks_[track].valid) return;

  // Ensure nibble track is ready first
  ensureTrackNibblized();
  bitifyTrack(track);
}

void DskDiskImage::bitifyTrack(int track) {
  if (track < 0 || track >= TRACKS) return;

  auto &bt = bit_tracks_[track];
  if (bt.valid) return;

  const auto &nt = nibble_tracks_[track];
  if (!nt.valid || nt.nibbles.empty()) return;

  // Calculate total bit count: sync bytes = 10 bits, data bytes = 8 bits
  uint32_t total_bits = 0;
  bool have_sync_flags = (nt.is_sync.size() == nt.nibbles.size());
  for (size_t i = 0; i < nt.nibbles.size(); i++) {
    total_bits += (have_sync_flags && nt.is_sync[i]) ? 10 : 8;
  }

  bt.bit_count = total_bits;
  size_t byte_count = (total_bits + 7) / 8;
  bt.bits.assign(byte_count, 0);

  // Helper: set a bit in the packed array
  auto setBit = [&](uint32_t bit_idx) {
    uint32_t byte_off = bit_idx / 8;
    uint8_t bit_off = 7 - (bit_idx % 8);
    bt.bits[byte_off] |= (1 << bit_off);
  };

  // Convert nibbles to packed bit stream
  uint32_t bit_pos = 0;
  for (size_t i = 0; i < nt.nibbles.size(); i++) {
    uint8_t nibble = nt.nibbles[i];
    // Write 8 data bits MSB first
    for (int b = 7; b >= 0; b--) {
      if (nibble & (1 << b)) {
        setBit(bit_pos);
      }
      bit_pos++;
    }
    // Sync bytes get 2 extra zero bits (already 0 in the cleared array)
    if (have_sync_flags && nt.is_sync[i]) {
      bit_pos += 2;
    }
  }

  bt.dirty = false;
  bt.valid = true;
}

void DskDiskImage::denibblizeBitTrack(int track) {
  if (track < 0 || track >= TRACKS)
    return;

  auto &bt = bit_tracks_[track];
  if (!bt.valid || bt.bit_count == 0)
    return;

  auto getBit = [&](uint32_t idx) -> uint8_t {
    uint32_t byte_off = idx / 8;
    uint8_t bit_off = 7 - (idx % 8);
    return (byte_off < bt.bits.size() && (bt.bits[byte_off] & (1 << bit_off)))
               ? 1
               : 0;
  };

  // Recover the nibble stream straight from the raw bits using a
  // self-synchronizing shift register: every valid GCR nibble has bit 7 set,
  // and 10-bit self-sync bytes realign the register. This mirrors the real
  // Disk II LSS, so writes that landed off the original nibble grid (which the
  // old fixed-frame bitTrackToNibbleTrack misread) are recovered correctly.
  std::vector<uint8_t> nibbles;
  nibbles.reserve(bt.bit_count / 8 + 16);

  // Scan one revolution plus a wrap tail (~500 nibbles) so a sector straddling
  // the index mark is still recovered whole.
  const uint32_t wrap_bits = std::min<uint32_t>(bt.bit_count, 4000);
  uint8_t shift = 0;
  size_t rev_nibbles = 0;
  for (uint32_t i = 0; i < bt.bit_count + wrap_bits; i++) {
    if (i == bt.bit_count)
      rev_nibbles = nibbles.size(); // sectors may only START in one revolution
    shift = static_cast<uint8_t>((shift << 1) | getBit(i % bt.bit_count));
    if (shift & 0x80) {
      nibbles.push_back(shift);
      shift = 0;
    }
  }
  if (rev_nibbles == 0)
    rev_nibbles = nibbles.size();

  scanAndDecodeSectors(track, nibbles.data(), rev_nibbles, nibbles.size());

  bt.dirty = false;
  // The bit stream is the source of truth after a bit-level write; drop any
  // stale nibble-track dirty flag so getSectorData's nibble pass skips it.
  nibble_tracks_[track].dirty = false;
  modified_ = true;
}

uint8_t DskDiskImage::readBit() {
  if (!loaded_) return 0;

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS) return 0;

  ensureTrackBitified();

  const auto &bt = bit_tracks_[track];
  if (!bt.valid || bt.bit_count == 0) return 0;

  uint32_t pos = bit_position_ % bt.bit_count;
  uint32_t byte_off = pos / 8;
  uint8_t bit_off = 7 - (pos % 8);

  uint8_t bit = 0;
  if (byte_off < bt.bits.size()) {
    bit = (bt.bits[byte_off] >> bit_off) & 1;
  }

  bit_position_ = (bit_position_ + 1) % bt.bit_count;
  return bit;
}

void DskDiskImage::writeBit(uint8_t bit) {
  if (!loaded_ || write_protected_) return;

  int track = quarter_track_ / 4;
  if (track < 0 || track >= TRACKS) return;

  ensureTrackBitified();

  auto &bt = bit_tracks_[track];
  if (!bt.valid || bt.bit_count == 0) return;

  uint32_t pos = bit_position_ % bt.bit_count;
  uint32_t byte_off = pos / 8;
  uint8_t bit_off = 7 - (pos % 8);

  if (byte_off < bt.bits.size()) {
    bt.bits[byte_off] &= ~(1 << bit_off);
    if (bit) {
      bt.bits[byte_off] |= (1 << bit_off);
    }
  }

  bt.dirty = true;
  modified_ = true;
  bit_position_ = (bit_position_ + 1) % bt.bit_count;
}

const uint8_t *DskDiskImage::getSectorData(size_t *size) const {
  if (!loaded_) {
    *size = 0;
    return nullptr;
  }

  // Decode any dirty bit tracks directly from their bit stream. A self-syncing
  // scan (bit7-latch) recovers nibbles regardless of where the writes landed,
  // matching the real Disk II LSS read path.
  for (int t = 0; t < TRACKS; t++) {
    if (bit_tracks_[t].dirty) {
      const_cast<DskDiskImage *>(this)->denibblizeBitTrack(t);
    }
  }

  // Then denibblize any tracks dirtied via the nibble-level write path.
  for (int t = 0; t < TRACKS; t++) {
    if (nibble_tracks_[t].dirty) {
      const_cast<DskDiskImage *>(this)->denibblizeTrack(t);
    }
  }

  *size = DISK_SIZE;
  return sector_data_.data();
}

bool DskDiskImage::writeSectorData(size_t offset, const uint8_t *data,
                                   size_t len) {
  if (!loaded_ || write_protected_ || !data) {
    return false;
  }
  if (offset > static_cast<size_t>(DISK_SIZE) ||
      len > static_cast<size_t>(DISK_SIZE) - offset) {
    return false;
  }
  if (len == 0) {
    return true;
  }

  // Fold any pending head writes into sector_data_ first, or they would be
  // decoded on top of the new bytes the next time the image is serialised.
  size_t flushed = 0;
  getSectorData(&flushed);

  std::memcpy(sector_data_.data() + offset, data, len);

  // Drop cached encodings of every track the write touched so the drive reads
  // the new contents back rather than the nibbles made from the old ones.
  int firstTrack = static_cast<int>(offset / TRACK_SIZE);
  int lastTrack = static_cast<int>((offset + len - 1) / TRACK_SIZE);
  bool currentTrackAffected = false;
  int currentTrack = getTrack();

  for (int t = firstTrack; t <= lastTrack && t < TRACKS; t++) {
    nibble_tracks_[t].valid = false;
    nibble_tracks_[t].dirty = false;
    nibble_tracks_[t].nibbles.clear();
    nibble_tracks_[t].is_sync.clear();
    bit_tracks_[t].valid = false;
    bit_tracks_[t].dirty = false;
    bit_tracks_[t].bits.clear();
    bit_tracks_[t].bit_count = 0;
    if (t == currentTrack) currentTrackAffected = true;
  }

  // The head's position within a re-encoded track is meaningless, and the new
  // track may be shorter than the old position.
  if (currentTrackAffected) {
    nibble_position_ = 0;
    bit_position_ = 0;
  }

  modified_ = true;
  return true;
}

const uint8_t *DskDiskImage::exportData(size_t *size) {
  // DSK format is already in its native format, just return sector data
  return getSectorData(size);
}

uint8_t DskDiskImage::getNibbleAt(int track, int position) const {
  if (track < 0 || track >= TRACKS) {
    return 0;
  }

  // Ensure track is nibblized
  if (!nibble_tracks_[track].valid) {
    const_cast<DskDiskImage *>(this)->nibblizeTrack(track);
  }

  const auto &nt = nibble_tracks_[track];
  if (nt.nibbles.empty() || position < 0 ||
      position >= static_cast<int>(nt.nibbles.size())) {
    return 0;
  }

  return nt.nibbles[position];
}

int DskDiskImage::getTrackNibbleCount(int track) const {
  if (track < 0 || track >= TRACKS) {
    return 0;
  }

  // Ensure track is nibblized
  if (!nibble_tracks_[track].valid) {
    const_cast<DskDiskImage *>(this)->nibblizeTrack(track);
  }

  return static_cast<int>(nibble_tracks_[track].nibbles.size());
}

} // namespace a2e
