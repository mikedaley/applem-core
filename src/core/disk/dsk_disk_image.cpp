#include "dsk_disk_image.hpp"
#include "gcr_encoding.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

// DOS 3.3 logical to physical sector mapping
// When reading a DSK file, logical sector N is at file offset N * 256
// But on disk, sectors are interleaved for performance
static constexpr std::array<int, 16> DOS_LOGICAL_TO_PHYSICAL = {
    0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};

// Reverse mapping: physical to logical
static constexpr std::array<int, 16> DOS_PHYSICAL_TO_LOGICAL = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};

// ProDOS logical to physical sector mapping
static constexpr std::array<int, 16> PRODOS_LOGICAL_TO_PHYSICAL = {
    0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};

// ProDOS reverse mapping
static constexpr std::array<int, 16> PRODOS_PHYSICAL_TO_LOGICAL = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15};

// 6-and-2 decoding table (reverse of ENCODE_6_AND_2)
static constexpr std::array<int8_t, 256> DECODE_6_AND_2 = []() {
  std::array<int8_t, 256> table{};
  for (int i = 0; i < 256; i++) {
    table[i] = -1; // Invalid by default
  }
  // Fill in valid mappings from the encode table
  for (int i = 0; i < 64; i++) {
    table[GCR::ENCODE_6_AND_2[i]] = static_cast<int8_t>(i);
  }
  return table;
}();

DskDiskImage::DskDiskImage() { sector_data_.fill(0); }

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

  // Detect format from disk content (not file extension)
  format_ = detectFormat(filename);

  // Invalidate all nibble tracks (will be regenerated on demand)
  for (auto &track : nibble_tracks_) {
    track.valid = false;
    track.dirty = false;
    track.nibbles.clear();
  }

  // Reset head position
  quarter_track_ = 0;
  phase_states_ = 0;
  current_phase_ = 0; // Reset to phase 0 for correct stepper tracking
  nibble_position_ = 0;
  last_cycle_count_ = 0;

  return true;
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
        // periods)
        bool valid_name = true;
        for (int i = 0; i < name_len && valid_name; i++) {
          uint8_t c = sector_data_[PRODOS_BLOCK2_OFFSET + 5 + i];
          // ProDOS names: A-Z (0x41-0x5A or 0xC1-0xDA), 0-9, period
          bool is_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
          bool is_digit = (c >= '0' && c <= '9');
          bool is_period = (c == '.');
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
      return Format::DSK;
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
    return PRODOS_PHYSICAL_TO_LOGICAL[physical_sector];
  } else {
    return DOS_PHYSICAL_TO_LOGICAL[physical_sector];
  }
}

void DskDiskImage::nibblizeTrack(int track) {
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  nt.nibbles.clear();
  nt.nibbles.reserve(NIBBLES_PER_TRACK);

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
      nt.nibbles.push_back(0xFF);
    }

    // === Address Field ===
    // Prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0x96);

    // 4-and-4 encoded values
    auto encode44 = [&](uint8_t val) {
      nt.nibbles.push_back((val >> 1) | 0xAA);
      nt.nibbles.push_back(val | 0xAA);
    };

    uint8_t checksum = volume_number_ ^ track ^ physical_sector;
    encode44(volume_number_);
    encode44(track);
    encode44(physical_sector);
    encode44(checksum);

    // Epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 2: 5 bytes
    for (int i = 0; i < 5; ++i) {
      nt.nibbles.push_back(0xFF);
    }

    // === Data Field ===
    // Prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xAD);

    // 6-and-2 encode the sector data
    auto encoded = GCR::encode6and2(data);
    nt.nibbles.insert(nt.nibbles.end(), encoded.begin(), encoded.end());

    // Epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 3 end: 1 byte
    nt.nibbles.push_back(0xFF);
  }

  // Pad or truncate to standard track size
  while (nt.nibbles.size() < NIBBLES_PER_TRACK) {
    nt.nibbles.push_back(0xFF);
  }
  if (nt.nibbles.size() > NIBBLES_PER_TRACK) {
    nt.nibbles.resize(NIBBLES_PER_TRACK);
  }

  nt.valid = true;
  nt.dirty = false;
}

uint8_t DskDiskImage::decode4and4(uint8_t odd, uint8_t even) {
  // Reverse of encode4and4:
  // odd has bits 7,5,3,1 of original in positions 6,4,2,0 (masked with 0x55,
  // OR'd with 0xAA) even has bits 6,4,2,0 of original in positions 6,4,2,0
  uint8_t result = ((odd << 1) & 0xAA) | (even & 0x55);
  return result;
}

bool DskDiskImage::decode6and2(const uint8_t *encoded, uint8_t *output) {
  // Decode 343 nibbles back to 256 bytes
  // First, convert disk nibbles to 6-bit values
  uint8_t buffer[342];

  // XOR decode (reverse of encode)
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++) {
    int8_t decoded = DECODE_6_AND_2[encoded[i]];
    if (decoded < 0) {
      return false; // Invalid nibble
    }
    buffer[i] = decoded ^ prev;
    prev = buffer[i];
  }

  // Verify checksum
  int8_t checksum_decoded = DECODE_6_AND_2[encoded[342]];
  if (checksum_decoded < 0 || (prev & 0x3F) != (checksum_decoded & 0x3F)) {
    // Checksum mismatch - still try to decode
    // Some disk images have minor errors
  }

  // Reconstruct 256 bytes from auxiliary (86) and primary (256) buffers
  for (int i = 0; i < 256; i++) {
    // High 6 bits from primary buffer
    uint8_t high = buffer[86 + i] << 2;

    // Low 2 bits from auxiliary buffer
    uint8_t aux_byte = buffer[i % 86];
    int shift = (i / 86) * 2;
    uint8_t low = (aux_byte >> shift) & 0x03;

    output[i] = high | low;
  }

  return true;
}

void DskDiskImage::denibblizeTrack(int track) {
  if (track < 0 || track >= TRACKS)
    return;

  auto &nt = nibble_tracks_[track];
  if (!nt.valid || !nt.dirty)
    return;

  const auto &nibbles = nt.nibbles;
  size_t pos = 0;
  size_t size = nibbles.size();

  // Find and decode each sector
  while (pos < size) {
    // Look for address field prologue: D5 AA 96
    bool found_addr = false;
    while (pos + 3 < size) {
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
    if (pos + 8 > size)
      break;

    uint8_t volume = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t addr_track = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t sector = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;
    uint8_t checksum = decode4and4(nibbles[pos], nibbles[pos + 1]);
    pos += 2;

    (void)volume; // Unused

    // Verify address checksum
    if ((volume_number_ ^ addr_track ^ sector) != checksum) {
      continue; // Invalid address field
    }

    // Verify track number matches
    if (addr_track != track) {
      continue; // Wrong track
    }

    // Skip address epilogue and look for data prologue: D5 AA AD
    bool found_data = false;
    size_t search_limit = pos + 50; // Don't search too far
    while (pos + 3 < size && pos < search_limit) {
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

    // Read 343 nibbles of data field
    if (pos + 343 > size)
      break;

    // Decode the sector data
    uint8_t decoded[256];
    if (decode6and2(&nibbles[pos], decoded)) {
      // Write to sector data array
      int log_sector = getLogicalSector(sector);
      int offset = (track * SECTORS_PER_TRACK + log_sector) * BYTES_PER_SECTOR;
      std::memcpy(&sector_data_[offset], decoded, BYTES_PER_SECTOR);
    }

    pos += 343;
  }

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
    // Stepping happens when the current phase is turned OFF
    // and an adjacent phase is ON (like apple2ts)
    updateHeadPosition();
  }
}

void DskDiskImage::updateHeadPosition() {
  // The Disk II stepper motor only moves when:
  // 1. The current phase (where head is settled) is turned OFF
  // 2. An adjacent phase is ON
  //
  // This matches apple2ts behavior and real hardware.

  constexpr int MAX_QUARTER_TRACK = (TRACKS * 4) - 1;

  // Check if current phase is now off
  uint8_t current_phase_bit = 1 << current_phase_;
  if (phase_states_ & current_phase_bit) {
    // Current phase is still on, don't step
    return;
  }

  // Current phase is off - check adjacent phases
  int next_phase = (current_phase_ + 1) % 4;
  int prev_phase = (current_phase_ + 3) % 4;

  bool next_on = (phase_states_ & (1 << next_phase)) != 0;
  bool prev_on = (phase_states_ & (1 << prev_phase)) != 0;

  if (next_on && !prev_on) {
    // Step inward (toward higher track numbers)
    current_phase_ = next_phase;
    if (quarter_track_ < MAX_QUARTER_TRACK - 1) {
      quarter_track_ += 2;
    } else if (quarter_track_ < MAX_QUARTER_TRACK) {
      quarter_track_ = MAX_QUARTER_TRACK;
    }
  } else if (prev_on && !next_on) {
    // Step outward (toward track 0)
    current_phase_ = prev_phase;
    if (quarter_track_ > 1) {
      quarter_track_ -= 2;
    } else if (quarter_track_ > 0) {
      quarter_track_ = 0;
    }
  }
  // If both or neither adjacent phases are on, don't step
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

const uint8_t *DskDiskImage::getSectorData(size_t *size) const {
  if (!loaded_) {
    *size = 0;
    return nullptr;
  }

  // First denibblize any dirty tracks
  for (int t = 0; t < TRACKS; t++) {
    if (nibble_tracks_[t].dirty) {
      const_cast<DskDiskImage *>(this)->denibblizeTrack(t);
    }
  }

  *size = DISK_SIZE;
  return sector_data_.data();
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
