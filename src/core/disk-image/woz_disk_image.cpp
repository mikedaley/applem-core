/*
 * woz_disk_image.cpp - WOZ 1.0/2.0 bit-accurate disk image implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "woz_disk_image.hpp"
#include "gcr_encoding.hpp"
#include <cstring>

namespace a2e {

// 6-and-2 decoding table (reverse of GCR::ENCODE_6_AND_2)
static constexpr std::array<int8_t, 256> DECODE_6_AND_2 = []() {
  std::array<int8_t, 256> table{};
  for (int i = 0; i < 256; i++) {
    table[i] = -1; // Invalid nibble
  }
  // Build reverse lookup from encode table
  for (int i = 0; i < 64; i++) {
    table[GCR::ENCODE_6_AND_2[i]] = static_cast<int8_t>(i);
  }
  return table;
}();

// DOS 3.3 physical to logical sector mapping
// Physical sector order on disk -> logical sector in file
static constexpr std::array<int, 16> DOS_PHYSICAL_TO_LOGICAL = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};

// ProDOS physical to logical sector mapping
static constexpr std::array<int, 16> PRODOS_PHYSICAL_TO_LOGICAL = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15};

WozDiskImage::WozDiskImage() { reset(); }

void WozDiskImage::reset() {
  format_ = Format::Unknown;
  loaded_ = false;
  modified_ = false;
  std::memset(&info_, 0, sizeof(info_));
  tmap_.fill(NO_TRACK);
  tracks_.clear();

  // Reset head positioning state
  phase_states_ = 0;
  quarter_track_ = 0;
  current_phase_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;

  // Clear decoded sector cache
  decoded_sectors_.clear();
  sectors_decoded_ = false;
}

bool WozDiskImage::load(const uint8_t *data, size_t size,
                        const std::string &filename) {
  reset();
  filename_ = filename;

  if (size < sizeof(WozHeader)) {
    return false;
  }

  // Validate header
  const auto *header = reinterpret_cast<const WozHeader *>(data);
  if (header->signature == WOZ1_SIGNATURE) {
    format_ = Format::WOZ1;
  } else if (header->signature == WOZ2_SIGNATURE) {
    format_ = Format::WOZ2;
  } else {
    return false;
  }

  // Validate magic bytes
  if (header->high_bits != 0xFF || header->lfcrlf[0] != 0x0A ||
      header->lfcrlf[1] != 0x0D || header->lfcrlf[2] != 0x0A) {
    return false;
  }

  // Parse chunks
  size_t offset = sizeof(WozHeader);
  bool has_info = false;
  bool has_tmap = false;
  bool has_trks = false;

  // Store TRKS chunk info for later processing (needs TMAP first)
  const uint8_t *trks_data = nullptr;
  uint32_t trks_size = 0;

  while (offset + sizeof(ChunkHeader) <= size) {
    const auto *chunk =
        reinterpret_cast<const ChunkHeader *>(data + offset);
    const uint8_t *chunk_data = data + offset + sizeof(ChunkHeader);

    if (offset + sizeof(ChunkHeader) + chunk->size > size) {
      break; // Chunk extends past end of file
    }

    switch (chunk->chunk_id) {
    case INFO_CHUNK_ID:
      if (!parseInfoChunk(chunk_data, chunk->size)) {
        return false;
      }
      has_info = true;
      break;

    case TMAP_CHUNK_ID:
      if (!parseTmapChunk(chunk_data, chunk->size)) {
        return false;
      }
      has_tmap = true;
      break;

    case TRKS_CHUNK_ID:
      // Save for later - need TMAP first
      trks_data = chunk_data;
      trks_size = chunk->size;
      has_trks = true;
      break;

    default:
      // Skip unknown chunks (META, WRIT, etc.)
      break;
    }

    offset += sizeof(ChunkHeader) + chunk->size;
  }

  // Validate required chunks
  if (!has_info || !has_tmap || !has_trks) {
    return false;
  }

  // Parse TRKS chunk (depends on format and TMAP)
  bool trks_ok = false;
  if (format_ == Format::WOZ1) {
    trks_ok = parseTrksChunkWoz1(trks_data, trks_size);
  } else {
    trks_ok = parseTrksChunkWoz2(data, size, trks_data, trks_size);
  }

  if (!trks_ok) {
    return false;
  }

  loaded_ = true;
  return true;
}

bool WozDiskImage::parseInfoChunk(const uint8_t *data, uint32_t size) {
  // Minimum size is 60 bytes for WOZ2, but accept smaller for WOZ1
  if (size < 37) {
    return false;
  }

  // Copy the info structure (handle size differences)
  std::memset(&info_, 0, sizeof(info_));
  std::memcpy(&info_, data,
              std::min(size, static_cast<uint32_t>(sizeof(info_))));

  // Validate disk type
  if (info_.disk_type != 1 && info_.disk_type != 2) {
    return false;
  }

  return true;
}

bool WozDiskImage::parseTmapChunk(const uint8_t *data, uint32_t size) {
  if (size < QUARTER_TRACK_COUNT) {
    return false;
  }

  std::memcpy(tmap_.data(), data, QUARTER_TRACK_COUNT);
  return true;
}

bool WozDiskImage::parseTrksChunkWoz1(const uint8_t *data, uint32_t size) {
  // WOZ1 TRKS: each track entry is 6656 bytes total
  // - Bytes 0-6645: Bitstream data (up to 6646 bytes)
  // - Bytes 6646-6647: bytes_used (uint16 LE)
  // - Bytes 6648-6649: bit_count (uint16 LE)
  // - Bytes 6650-6651: splice_point (uint16 LE)
  // - Byte 6652: splice_nibble
  // - Byte 6653: splice_bit_count
  // - Bytes 6654-6655: reserved
  static constexpr size_t WOZ1_ENTRY_SIZE = 6656;
  static constexpr size_t WOZ1_BYTES_USED_OFFSET = 6646;
  static constexpr size_t WOZ1_BIT_COUNT_OFFSET = 6648;

  // Count how many tracks we have
  size_t track_count = size / WOZ1_ENTRY_SIZE;

  tracks_.resize(track_count);

  for (size_t i = 0; i < track_count; i++) {
    const uint8_t *entry = data + i * WOZ1_ENTRY_SIZE;
    uint16_t bytes_used =
        entry[WOZ1_BYTES_USED_OFFSET] | (entry[WOZ1_BYTES_USED_OFFSET + 1] << 8);
    uint16_t bit_count =
        entry[WOZ1_BIT_COUNT_OFFSET] | (entry[WOZ1_BIT_COUNT_OFFSET + 1] << 8);

    if (bytes_used > 0 && bytes_used <= 6646) {
      tracks_[i].bit_count = bit_count;
      tracks_[i].bits.assign(entry, entry + bytes_used);
      tracks_[i].valid = true;
    }
  }

  return true;
}

bool WozDiskImage::parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                                       const uint8_t *trks_data,
                                       uint32_t trks_size) {
  // WOZ2 TRKS: 160 track entries (8 bytes each) = 1280 bytes
  // Track data follows at block offsets specified in entries
  static constexpr size_t WOZ2_TRK_TABLE_SIZE = 160 * sizeof(Woz2TrackEntry);

  if (trks_size < WOZ2_TRK_TABLE_SIZE) {
    return false;
  }

  const auto *entries = reinterpret_cast<const Woz2TrackEntry *>(trks_data);

  // Find max track index referenced by TMAP
  int max_track_index = -1;
  for (int i = 0; i < QUARTER_TRACK_COUNT; i++) {
    if (tmap_[i] != NO_TRACK && tmap_[i] > max_track_index) {
      max_track_index = tmap_[i];
    }
  }

  if (max_track_index < 0) {
    return true; // No tracks - empty disk
  }

  tracks_.resize(max_track_index + 1);

  // Load each track referenced by TMAP
  for (int i = 0; i <= max_track_index; i++) {
    const auto &entry = entries[i];

    if (entry.starting_block == 0 || entry.block_count == 0) {
      continue; // Empty track
    }

    // Calculate offset in file (blocks start at offset 0 relative to start of
    // file)
    size_t track_offset =
        static_cast<size_t>(entry.starting_block) * WOZ2_TRACK_BLOCK_SIZE;
    size_t track_size =
        static_cast<size_t>(entry.block_count) * WOZ2_TRACK_BLOCK_SIZE;

    if (track_offset + track_size > file_size) {
      continue; // Track extends past end of file
    }

    tracks_[i].bit_count = entry.bit_count;
    tracks_[i].bits.assign(file_data + track_offset,
                           file_data + track_offset + track_size);
    tracks_[i].valid = true;
  }

  return true;
}

void WozDiskImage::createBlank() {
  reset();

  // Set up as WOZ2 format
  format_ = Format::WOZ2;

  // Initialize INFO chunk for a 5.25" disk
  info_.version = 2;
  info_.disk_type = 1;           // 5.25"
  info_.write_protected = 0;     // Not write protected
  info_.synchronized = 0;
  info_.cleaned = 1;
  std::strncpy(info_.creator, "A2E Emulator", sizeof(info_.creator));
  info_.disk_sides = 1;
  info_.boot_sector_format = 0;  // Unknown
  info_.optimal_bit_timing = 32; // 4 microseconds
  info_.compatible_hardware = 0;
  info_.required_ram = 0;
  info_.largest_track = 13;      // 13 blocks per track

  // Standard 5.25" disk parameters
  static constexpr int NUM_TRACKS = 35;
  static constexpr uint32_t BITS_PER_TRACK = 51200;  // Standard track length in bits
  static constexpr size_t BYTES_PER_TRACK = (BITS_PER_TRACK + 7) / 8;  // 6400 bytes

  // Initialize TMAP - map quarter-tracks to whole tracks
  for (int qt = 0; qt < QUARTER_TRACK_COUNT; qt++) {
    int track = qt / 4;
    if (track < NUM_TRACKS) {
      tmap_[qt] = static_cast<uint8_t>(track);
    } else {
      tmap_[qt] = NO_TRACK;
    }
  }

  // Initialize tracks with sync bytes (0xFF)
  tracks_.resize(NUM_TRACKS);
  for (int t = 0; t < NUM_TRACKS; t++) {
    tracks_[t].bit_count = BITS_PER_TRACK;
    tracks_[t].bits.resize(BYTES_PER_TRACK, 0xFF);  // Fill with sync bytes
    tracks_[t].valid = true;
  }

  loaded_ = true;
  modified_ = true;  // Mark as modified so it will be saved
}

bool WozDiskImage::isLoaded() const { return loaded_; }

DiskImage::Format WozDiskImage::getFormat() const { return format_; }

int WozDiskImage::getTrackCount() const {
  // Standard 5.25" disk has 35 tracks
  return 35;
}

// ===== Head Positioning =====

void WozDiskImage::setPhase(int phase, bool on) {
  if (phase < 0 || phase > 3) {
    return;
  }

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

void WozDiskImage::updateHeadPosition() {
  // The Disk II stepper motor only moves when:
  // 1. The current phase (where head is settled) is turned OFF
  // 2. An adjacent phase is ON
  //
  // This matches apple2ts behavior and real hardware.

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
    if (quarter_track_ < 158) {
      quarter_track_ += 2;
    } else if (quarter_track_ < 159) {
      quarter_track_ = 159;
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

int WozDiskImage::getQuarterTrack() const { return quarter_track_; }

int WozDiskImage::getTrack() const { return quarter_track_ / 4; }

void WozDiskImage::setQuarterTrack(int quarter_track) {
  quarter_track_ = std::max(0, std::min(quarter_track, QUARTER_TRACK_COUNT - 1));
}

bool WozDiskImage::hasData() const {
  if (quarter_track_ < 0 || quarter_track_ >= QUARTER_TRACK_COUNT) {
    return false;
  }
  return tmap_[quarter_track_] != NO_TRACK;
}

const WozDiskImage::TrackData *WozDiskImage::getCurrentTrackData() const {
  if (quarter_track_ < 0 || quarter_track_ >= QUARTER_TRACK_COUNT) {
    return nullptr;
  }

  uint8_t track_index = tmap_[quarter_track_];
  if (track_index == NO_TRACK) {
    return nullptr;
  }
  if (track_index >= static_cast<uint8_t>(tracks_.size())) {
    return nullptr;
  }

  const TrackData &track = tracks_[track_index];
  if (!track.valid) {
    return nullptr;
  }
  return &track;
}

// Timing constants for WOZ format
// Disk spins at ~300 RPM = 5 revolutions/second
// Each bit takes approximately 4 microseconds = 4 cycles at 1.023 MHz
static constexpr uint64_t CYCLES_PER_BIT = 4;

void WozDiskImage::advanceBitPosition(uint64_t elapsed_cycles) {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) {
    return;
  }

  // Calculate how many bits have passed
  uint32_t bits_elapsed = static_cast<uint32_t>(elapsed_cycles / CYCLES_PER_BIT);

  // Advance bit position (wrapping around track)
  bit_position_ = (bit_position_ + bits_elapsed) % track->bit_count;
}

uint8_t WozDiskImage::readBitInternal() const {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) {
    return 0;
  }

  // Wrap bit position to track length
  uint32_t pos = bit_position_ % track->bit_count;

  // Calculate byte and bit offsets
  uint32_t byte_offset = pos / 8;
  uint8_t bit_offset = 7 - (pos % 8); // MSB first

  if (byte_offset >= track->bits.size()) {
    return 0;
  }

  return (track->bits[byte_offset] >> bit_offset) & 1;
}

uint8_t WozDiskImage::readNibble() {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) {
    return 0;
  }

  // Read bits until we get a nibble (byte with high bit set)
  // The Disk II hardware shifts bits into a register until bit 7 is set
  uint8_t value = 0;
  int bits_read = 0;
  static constexpr int MAX_BITS = 64; // Safety limit

  while (bits_read < MAX_BITS) {
    uint8_t bit = readBitInternal();
    bit_position_ = (bit_position_ + 1) % track->bit_count;
    bits_read++;

    if (bit) {
      // Got a 1 bit - start/continue building nibble
      value = (value << 1) | 1;
    } else if (value != 0) {
      // Got a 0 bit after a 1 - continue building nibble
      value = value << 1;
    }
    // If value is 0 and bit is 0, we're still in sync bits - skip

    // Check if we have a complete nibble (bit 7 set)
    if (value & 0x80) {
      return value;
    }
  }

  // Timeout - return whatever we have
  return value;
}

bool WozDiskImage::isWriteProtected() const {
  return info_.write_protected != 0;
}

std::string WozDiskImage::getFormatName() const {
  switch (format_) {
  case Format::WOZ1:
    return "WOZ 1.0";
  case Format::WOZ2:
    return "WOZ 2.0";
  default:
    return "Unknown";
  }
}

uint8_t WozDiskImage::getDiskType() const { return info_.disk_type; }

uint8_t WozDiskImage::getOptimalBitTiming() const {
  // Default to 32 (4 microseconds) if not specified
  return info_.optimal_bit_timing ? info_.optimal_bit_timing : 32;
}

std::string WozDiskImage::getDiskTypeString() const {
  switch (info_.disk_type) {
  case 1:
    return "5.25\"";
  case 2:
    return "3.5\"";
  default:
    return "Unknown";
  }
}

// ===== Write Operations =====

WozDiskImage::TrackData *WozDiskImage::getMutableCurrentTrackData() {
  if (quarter_track_ < 0 || quarter_track_ >= QUARTER_TRACK_COUNT) {
    return nullptr;
  }

  uint8_t track_index = tmap_[quarter_track_];
  if (track_index == NO_TRACK ||
      track_index >= static_cast<uint8_t>(tracks_.size())) {
    return nullptr;
  }

  TrackData &track = tracks_[track_index];
  return track.valid ? &track : nullptr;
}

void WozDiskImage::writeBitInternal(uint8_t bit) {
  TrackData *track = getMutableCurrentTrackData();
  if (!track || track->bit_count == 0) {
    return;
  }

  // Wrap bit position to track length
  uint32_t pos = bit_position_ % track->bit_count;

  // Calculate byte and bit offsets
  uint32_t byte_offset = pos / 8;
  uint8_t bit_offset = 7 - (pos % 8); // MSB first

  if (byte_offset >= track->bits.size()) {
    return;
  }

  // Clear the bit first, then set if needed
  track->bits[byte_offset] &= ~(1 << bit_offset);
  if (bit) {
    track->bits[byte_offset] |= (1 << bit_offset);
  }
}

void WozDiskImage::writeNibble(uint8_t nibble) {
  if (!loaded_ || isWriteProtected()) {
    return;
  }

  TrackData *track = getMutableCurrentTrackData();
  if (!track || track->bit_count == 0) {
    return;
  }

  // Write 8 bits, MSB first
  for (int i = 7; i >= 0; i--) {
    writeBitInternal((nibble >> i) & 1);
    bit_position_ = (bit_position_ + 1) % track->bit_count;
  }

  modified_ = true;
}

const uint8_t *WozDiskImage::getSectorData(size_t *size) const {
  if (!loaded_) {
    *size = 0;
    return nullptr;
  }

  // Try to decode sectors if not already done
  if (!sectors_decoded_) {
    if (!decodeSectors()) {
      *size = 0;
      return nullptr;
    }
  }

  *size = decoded_sectors_.size();
  return decoded_sectors_.data();
}

uint8_t WozDiskImage::getNibbleAt(int track, int position) const {
  // WOZ stores bit-level data, not nibbles directly
  // This would require significant work to implement
  (void)track;
  (void)position;
  return 0;
}

int WozDiskImage::getTrackNibbleCount(int track) const {
  // WOZ stores bit-level data
  (void)track;
  return 0;
}

const uint8_t *WozDiskImage::exportData(size_t *size) {
  if (!loaded_) {
    *size = 0;
    return nullptr;
  }

  // Calculate required size for WOZ2 format
  // Header: 12 bytes
  // INFO chunk: 8 (header) + 60 (data) = 68 bytes
  // TMAP chunk: 8 (header) + 160 (data) = 168 bytes
  // TRKS chunk: 8 (header) + 1280 (track table) = 1288 bytes
  // Total header area: pad to block 3 (1536 bytes)
  static constexpr size_t HEADER_SIZE = 12;
  static constexpr size_t INFO_CHUNK_SIZE = 68;
  static constexpr size_t TMAP_CHUNK_SIZE = 168;
  static constexpr size_t TRKS_HEADER_SIZE = 8;
  static constexpr size_t TRKS_TABLE_SIZE = 160 * 8;  // 1280 bytes
  static constexpr size_t TRACK_DATA_START_BLOCK = 3;
  static constexpr size_t BLOCK_SIZE = 512;

  // Count tracks and calculate total track data size
  size_t total_track_blocks = 0;
  for (size_t i = 0; i < tracks_.size(); i++) {
    if (tracks_[i].valid && tracks_[i].bit_count > 0) {
      size_t track_bytes = (tracks_[i].bit_count + 7) / 8;
      size_t track_blocks = (track_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
      total_track_blocks += track_blocks;
    }
  }

  size_t total_size = TRACK_DATA_START_BLOCK * BLOCK_SIZE + total_track_blocks * BLOCK_SIZE;
  export_buffer_.resize(total_size);
  std::fill(export_buffer_.begin(), export_buffer_.end(), 0);

  size_t offset = 0;

  // === WOZ2 Header (12 bytes) ===
  export_buffer_[offset++] = 0x57;  // 'W'
  export_buffer_[offset++] = 0x4F;  // 'O'
  export_buffer_[offset++] = 0x5A;  // 'Z'
  export_buffer_[offset++] = 0x32;  // '2'
  export_buffer_[offset++] = 0xFF;  // High bit
  export_buffer_[offset++] = 0x0A;  // LF
  export_buffer_[offset++] = 0x0D;  // CR
  export_buffer_[offset++] = 0x0A;  // LF
  // CRC32 placeholder (not validated by loader)
  export_buffer_[offset++] = 0x00;
  export_buffer_[offset++] = 0x00;
  export_buffer_[offset++] = 0x00;
  export_buffer_[offset++] = 0x00;

  // === INFO Chunk ===
  export_buffer_[offset++] = 0x49;  // 'I'
  export_buffer_[offset++] = 0x4E;  // 'N'
  export_buffer_[offset++] = 0x46;  // 'F'
  export_buffer_[offset++] = 0x4F;  // 'O'
  // Chunk size: 60 bytes
  export_buffer_[offset++] = 60;
  export_buffer_[offset++] = 0;
  export_buffer_[offset++] = 0;
  export_buffer_[offset++] = 0;
  // Copy INFO data
  std::memcpy(&export_buffer_[offset], &info_, sizeof(info_));
  offset += 60;

  // === TMAP Chunk ===
  export_buffer_[offset++] = 0x54;  // 'T'
  export_buffer_[offset++] = 0x4D;  // 'M'
  export_buffer_[offset++] = 0x41;  // 'A'
  export_buffer_[offset++] = 0x50;  // 'P'
  // Chunk size: 160 bytes
  export_buffer_[offset++] = 160;
  export_buffer_[offset++] = 0;
  export_buffer_[offset++] = 0;
  export_buffer_[offset++] = 0;
  // Copy TMAP data
  std::memcpy(&export_buffer_[offset], tmap_.data(), QUARTER_TRACK_COUNT);
  offset += QUARTER_TRACK_COUNT;

  // === TRKS Chunk ===
  export_buffer_[offset++] = 0x54;  // 'T'
  export_buffer_[offset++] = 0x52;  // 'R'
  export_buffer_[offset++] = 0x4B;  // 'K'
  export_buffer_[offset++] = 0x53;  // 'S'
  // Chunk size: track table only (1280 bytes)
  export_buffer_[offset++] = TRKS_TABLE_SIZE & 0xFF;
  export_buffer_[offset++] = (TRKS_TABLE_SIZE >> 8) & 0xFF;
  export_buffer_[offset++] = (TRKS_TABLE_SIZE >> 16) & 0xFF;
  export_buffer_[offset++] = (TRKS_TABLE_SIZE >> 24) & 0xFF;

  // Build track entries and copy track data
  size_t current_block = TRACK_DATA_START_BLOCK;
  size_t track_table_offset = offset;

  for (size_t i = 0; i < 160; i++) {
    if (i < tracks_.size() && tracks_[i].valid && tracks_[i].bit_count > 0) {
      const TrackData &track = tracks_[i];
      size_t track_bytes = (track.bit_count + 7) / 8;
      size_t block_count = (track_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

      // Write track entry
      export_buffer_[track_table_offset + i * 8 + 0] = current_block & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 1] = (current_block >> 8) & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 2] = block_count & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 3] = (block_count >> 8) & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 4] = track.bit_count & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 5] = (track.bit_count >> 8) & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 6] = (track.bit_count >> 16) & 0xFF;
      export_buffer_[track_table_offset + i * 8 + 7] = (track.bit_count >> 24) & 0xFF;

      // Copy track data
      size_t data_offset = current_block * BLOCK_SIZE;
      size_t copy_size = std::min(track.bits.size(), block_count * BLOCK_SIZE);
      std::memcpy(&export_buffer_[data_offset], track.bits.data(), copy_size);

      current_block += block_count;
    } else {
      // Empty track entry
      std::memset(&export_buffer_[track_table_offset + i * 8], 0, 8);
    }
  }

  *size = export_buffer_.size();
  return export_buffer_.data();
}

// ===== Sector Decoding Implementation =====

uint8_t WozDiskImage::decode4and4(uint8_t odd, uint8_t even) {
  // 4-and-4 encoding: odd bits in first nibble, even bits in second
  // Decode by combining the low bits of each
  return ((odd << 1) | 0x01) & even;
}

bool WozDiskImage::decode6and2(const uint8_t *nibbles, uint8_t *output) {
  // 6-and-2 decoding: 343 nibbles -> 256 bytes
  // This reverses the encoding done in gcr_encoding.cpp:
  //
  // Encoding creates a 342-byte buffer:
  //   buffer[0..85]: auxiliary buffer - 2 bits from each of 3 data bytes
  //   buffer[86..341]: primary buffer - 6 high bits from each of 256 data bytes
  // Then XOR encodes: each byte XOR'd with previous byte's value
  // Final nibble (343rd) is the checksum (last pre-XOR value)

  std::array<uint8_t, 342> buffer;

  // Step 1: Decode nibbles and reverse XOR encoding
  // The encoder does: nibble[i] = encode(buffer[i] ^ prev), prev = buffer[i]
  // To decode: buffer[i] = decode(nibble[i]) ^ prev_decoded
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++) {
    int8_t val = DECODE_6_AND_2[nibbles[i]];
    if (val < 0) {
      return false; // Invalid nibble
    }
    buffer[i] = static_cast<uint8_t>(val) ^ prev;
    prev = buffer[i];
  }

  // Step 2: Verify checksum
  // The checksum nibble encodes the last buffer value (before XOR was applied)
  int8_t chk_val = DECODE_6_AND_2[nibbles[342]];
  if (chk_val < 0 || prev != static_cast<uint8_t>(chk_val)) {
    return false; // Checksum mismatch
  }

  // Step 3: Reassemble 256 bytes from the buffer
  // Primary buffer (86-341) contains bits 2-7 of each byte
  // Auxiliary buffer (0-85) contains bits 0-1 from groups of 3 bytes:
  //   buffer[i] bits 0,1 -> data[i] bits 1,0 (swapped)
  //   buffer[i] bits 2,3 -> data[i+86] bits 1,0 (swapped)
  //   buffer[i] bits 4,5 -> data[i+172] bits 1,0 (swapped)

  for (int i = 0; i < 256; i++) {
    // Get the 6 high bits from primary buffer
    uint8_t high = buffer[86 + i] << 2;

    // Get the 2 low bits from auxiliary buffer
    // The aux index for byte i depends on which group of 86 it's in
    int aux_idx = i % 86;
    int group = i / 86; // 0, 1, or 2

    uint8_t aux = buffer[aux_idx];
    uint8_t low_bits;

    // Extract the 2 bits for this group and unswap them
    // Encoding did: ((data[i] & 0x01) << 1) | ((data[i] & 0x02) >> 1)
    // So bit 0 of aux = bit 1 of data, bit 1 of aux = bit 0 of data
    switch (group) {
      case 0:
        // Bits 0,1 of aux -> bits 0,1 of data (with swap)
        low_bits = ((aux & 0x02) >> 1) | ((aux & 0x01) << 1);
        break;
      case 1:
        // Bits 2,3 of aux -> bits 0,1 of data (with swap)
        low_bits = ((aux & 0x08) >> 3) | ((aux & 0x04) >> 1);
        break;
      case 2:
      default:
        // Bits 4,5 of aux -> bits 0,1 of data (with swap)
        low_bits = ((aux & 0x20) >> 5) | ((aux & 0x10) >> 3);
        break;
    }

    output[i] = high | low_bits;
  }

  return true;
}

std::vector<uint8_t> WozDiskImage::readTrackNibbles(size_t track_index) const {
  std::vector<uint8_t> nibbles;

  if (track_index >= tracks_.size() || !tracks_[track_index].valid) {
    return nibbles;
  }

  const TrackData &track = tracks_[track_index];
  if (track.bit_count == 0) {
    return nibbles;
  }

  nibbles.reserve(track.bit_count / 8); // Approximate

  // Read nibbles by scanning through bit stream
  uint32_t bit_pos = 0;
  uint8_t value = 0;
  int bits_read = 0;

  // Read through entire track twice to ensure we get all sectors
  // (sectors may wrap around the track boundary)
  uint32_t total_bits = track.bit_count * 2;

  while (bit_pos < total_bits) {
    // Read a bit
    uint32_t actual_pos = bit_pos % track.bit_count;
    uint32_t byte_offset = actual_pos / 8;
    uint8_t bit_offset = 7 - (actual_pos % 8);

    if (byte_offset >= track.bits.size()) {
      break;
    }

    uint8_t bit = (track.bits[byte_offset] >> bit_offset) & 1;
    bit_pos++;

    if (bit) {
      // Got a 1 bit - start/continue building nibble
      value = (value << 1) | 1;
      bits_read++;
    } else if (value != 0) {
      // Got a 0 bit after a 1 - continue building nibble
      value = value << 1;
      bits_read++;
    }
    // If value is 0 and bit is 0, we're in sync - skip

    // Check if we have a complete nibble (bit 7 set)
    if (value & 0x80) {
      nibbles.push_back(value);
      value = 0;
      bits_read = 0;

      // Limit total nibbles to prevent runaway
      if (nibbles.size() > 8192) {
        break;
      }
    }

    // Timeout - reset if too many bits without a valid nibble
    if (bits_read > 16) {
      value = 0;
      bits_read = 0;
    }
  }

  return nibbles;
}

int WozDiskImage::decodeSectorsFromNibbles(
    const std::vector<uint8_t> &nibbles, int expected_track,
    std::array<std::array<uint8_t, 256>, 16> &sectors) const {

  // Track which sectors we've successfully decoded
  std::array<bool, 16> sector_found{};
  int sectors_decoded = 0;

  // Search for address field prologues: D5 AA 96
  for (size_t i = 0; i + 350 < nibbles.size(); i++) {
    // Look for address field prologue
    if (nibbles[i] != 0xD5 || nibbles[i + 1] != 0xAA || nibbles[i + 2] != 0x96) {
      continue;
    }

    // Decode address field (4-and-4 encoded)
    uint8_t volume = decode4and4(nibbles[i + 3], nibbles[i + 4]);
    uint8_t track = decode4and4(nibbles[i + 5], nibbles[i + 6]);
    uint8_t sector = decode4and4(nibbles[i + 7], nibbles[i + 8]);
    uint8_t checksum = decode4and4(nibbles[i + 9], nibbles[i + 10]);

    // Verify address checksum
    if ((volume ^ track ^ sector) != checksum) {
      continue;
    }

    // Verify track number matches (allow some tolerance for copy protection)
    if (track != expected_track && track != expected_track + 1 &&
        track != expected_track - 1) {
      continue;
    }

    // Verify sector number is valid
    if (sector >= 16) {
      continue;
    }

    // Skip if we already have this sector
    if (sector_found[sector]) {
      continue;
    }

    // Search for data field prologue: D5 AA AD
    // It should be within ~50 nibbles after address field
    size_t data_start = 0;
    for (size_t j = i + 11; j < i + 60 && j + 2 < nibbles.size(); j++) {
      if (nibbles[j] == 0xD5 && nibbles[j + 1] == 0xAA && nibbles[j + 2] == 0xAD) {
        data_start = j + 3;
        break;
      }
    }

    if (data_start == 0 || data_start + 343 > nibbles.size()) {
      continue;
    }

    // Decode 6-and-2 data (343 nibbles: 342 data + 1 checksum)
    if (decode6and2(&nibbles[data_start], sectors[sector].data())) {
      sector_found[sector] = true;
      sectors_decoded++;

      // If we have all 16 sectors, we're done
      if (sectors_decoded == 16) {
        break;
      }
    }
  }

  return sectors_decoded;
}

bool WozDiskImage::decodeSectors() const {
  if (!loaded_) {
    return false;
  }

  // Standard DOS 3.3 disk: 35 tracks, 16 sectors, 256 bytes = 143,360 bytes
  static constexpr size_t DISK_SIZE = 35 * 16 * 256;
  decoded_sectors_.resize(DISK_SIZE);
  std::fill(decoded_sectors_.begin(), decoded_sectors_.end(), 0);

  int total_sectors_decoded = 0;

  // Decode each track
  for (int track = 0; track < 35; track++) {
    // Find the track index from TMAP (use whole track position)
    int quarter_track = track * 4;
    if (quarter_track >= QUARTER_TRACK_COUNT) {
      continue;
    }

    uint8_t track_index = tmap_[quarter_track];
    if (track_index == NO_TRACK || track_index >= tracks_.size()) {
      continue;
    }

    // Read nibbles from track
    std::vector<uint8_t> nibbles = readTrackNibbles(track_index);
    if (nibbles.empty()) {
      continue;
    }

    // Decode sectors from nibbles
    std::array<std::array<uint8_t, 256>, 16> track_sectors{};
    int decoded = decodeSectorsFromNibbles(nibbles, track, track_sectors);
    total_sectors_decoded += decoded;

    // Copy decoded sectors to output buffer
    // Use DOS 3.3 physical-to-logical mapping (most common for WOZ files)
    for (int phys_sector = 0; phys_sector < 16; phys_sector++) {
      int logical_sector = DOS_PHYSICAL_TO_LOGICAL[phys_sector];
      size_t offset = (track * 16 + logical_sector) * 256;
      std::memcpy(&decoded_sectors_[offset], track_sectors[phys_sector].data(), 256);
    }
  }

  // Consider decoding successful if we got at least 50% of sectors
  // This allows for some bad sectors while still enabling catalog reading
  sectors_decoded_ = (total_sectors_decoded >= 35 * 8);

  return sectors_decoded_;
}

} // namespace a2e
