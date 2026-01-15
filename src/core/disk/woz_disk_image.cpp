#include "woz_disk_image.hpp"
#include <cstring>

namespace a2e {

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
  last_phase_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;
}

bool WozDiskImage::load(const uint8_t *data, size_t size,
                        const std::string &filename) {
  (void)filename; // Not used for WOZ format detection

  reset();

  printf("WOZ load: size=%zu\n", size);

  if (size < sizeof(WozHeader)) {
    printf("WOZ load failed: size too small\n");
    return false;
  }

  // Validate header
  const auto *header = reinterpret_cast<const WozHeader *>(data);
  printf("WOZ signature: 0x%08X (expected WOZ2: 0x%08X)\n", header->signature, WOZ2_SIGNATURE);
  if (header->signature == WOZ1_SIGNATURE) {
    format_ = Format::WOZ1;
  } else if (header->signature == WOZ2_SIGNATURE) {
    format_ = Format::WOZ2;
  } else {
    printf("WOZ load failed: invalid signature\n");
    return false;
  }

  // Validate magic bytes
  printf("WOZ magic: high_bits=0x%02X, lfcrlf=[0x%02X, 0x%02X, 0x%02X]\n",
         header->high_bits, header->lfcrlf[0], header->lfcrlf[1], header->lfcrlf[2]);
  if (header->high_bits != 0xFF || header->lfcrlf[0] != 0x0A ||
      header->lfcrlf[1] != 0x0D || header->lfcrlf[2] != 0x0A) {
    printf("WOZ load failed: invalid magic bytes\n");
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
  printf("WOZ chunks: has_info=%d, has_tmap=%d, has_trks=%d\n", has_info, has_tmap, has_trks);
  if (!has_info || !has_tmap || !has_trks) {
    printf("WOZ load failed: missing required chunks\n");
    return false;
  }

  // Parse TRKS chunk (depends on format and TMAP)
  bool trks_ok = false;
  if (format_ == Format::WOZ1) {
    trks_ok = parseTrksChunkWoz1(trks_data, trks_size);
  } else {
    trks_ok = parseTrksChunkWoz2(data, size, trks_data, trks_size);
  }

  printf("WOZ TRKS parse result: %d\n", trks_ok);
  if (!trks_ok) {
    printf("WOZ load failed: TRKS parse failed\n");
    return false;
  }

  loaded_ = true;
  printf("WOZ load successful!\n");
  return true;
}

bool WozDiskImage::parseInfoChunk(const uint8_t *data, uint32_t size) {
  printf("parseInfoChunk: size=%u\n", size);
  // Minimum size is 60 bytes for WOZ2, but accept smaller for WOZ1
  if (size < 37) {
    printf("parseInfoChunk failed: size too small\n");
    return false;
  }

  // Copy the info structure (handle size differences)
  std::memset(&info_, 0, sizeof(info_));
  std::memcpy(&info_, data,
              std::min(size, static_cast<uint32_t>(sizeof(info_))));

  printf("parseInfoChunk: disk_type=%d\n", info_.disk_type);
  // Validate disk type
  if (info_.disk_type != 1 && info_.disk_type != 2) {
    printf("parseInfoChunk failed: invalid disk_type\n");
    return false;
  }

  return true;
}

bool WozDiskImage::parseTmapChunk(const uint8_t *data, uint32_t size) {
  printf("parseTmapChunk: size=%u (need %d)\n", size, QUARTER_TRACK_COUNT);
  if (size < QUARTER_TRACK_COUNT) {
    printf("parseTmapChunk failed: size too small\n");
    return false;
  }

  std::memcpy(tmap_.data(), data, QUARTER_TRACK_COUNT);
  printf("parseTmapChunk: tmap[0]=%d, tmap[4]=%d\n", tmap_[0], tmap_[4]);
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
  printf("parseTrksChunkWoz1: size=%u, entry_size=%zu, track_count=%zu\n",
         size, WOZ1_ENTRY_SIZE, track_count);

  tracks_.resize(track_count);

  int valid_tracks = 0;
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
      valid_tracks++;

      // Debug first few tracks
      if (i < 3) {
        printf("  Track %zu: bytes_used=%u, bit_count=%u, first_bytes=[%02X %02X %02X %02X]\n",
               i, bytes_used, bit_count,
               entry[0], entry[1], entry[2], entry[3]);
      }
    }
  }

  printf("parseTrksChunkWoz1: loaded %d valid tracks\n", valid_tracks);
  return true;
}

bool WozDiskImage::parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                                       const uint8_t *trks_data,
                                       uint32_t trks_size) {
  // WOZ2 TRKS: 160 track entries (8 bytes each) = 1280 bytes
  // Track data follows at block offsets specified in entries
  static constexpr size_t WOZ2_TRK_TABLE_SIZE = 160 * sizeof(Woz2TrackEntry);

  printf("parseTrksChunkWoz2: trks_size=%u, need=%zu, file_size=%zu\n",
         trks_size, WOZ2_TRK_TABLE_SIZE, file_size);

  if (trks_size < WOZ2_TRK_TABLE_SIZE) {
    printf("parseTrksChunkWoz2 failed: trks_size too small\n");
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

  printf("parseTrksChunkWoz2: max_track_index=%d\n", max_track_index);

  if (max_track_index < 0) {
    printf("parseTrksChunkWoz2: no tracks in TMAP\n");
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
    // Update head position when a phase is activated
    updateHeadPosition(phase);
  } else {
    phase_states_ &= ~phase_bit;
    // Turning off a phase doesn't cause stepping
  }
}

void WozDiskImage::updateHeadPosition(int phase) {
  // The Disk II uses a 4-phase stepper motor with phases 2 quarter-tracks
  // apart:
  //   Phase 0: quarter-tracks 0, 8, 16... (tracks 0, 2, 4...)
  //   Phase 1: quarter-tracks 2, 10, 18... (half-tracks 0.5, 2.5...)
  //   Phase 2: quarter-tracks 4, 12, 20... (tracks 1, 3, 5...)
  //   Phase 3: quarter-tracks 6, 14, 22... (half-tracks 1.5, 3.5...)
  //
  // Each adjacent phase change moves the head by 2 quarter-tracks (1
  // half-track). When two adjacent phases are on, head settles at odd
  // quarter-track between.

  int old_quarter_track = quarter_track_;

  // Calculate step direction based on phase difference from last activated
  // phase
  int phase_diff = phase - last_phase_;

  // Normalize to handle wrap-around
  if (phase_diff == 3)
    phase_diff = -1; // 0->3 is stepping backward
  if (phase_diff == -3)
    phase_diff = 1; // 3->0 is stepping forward

  // Only move if the phase change is a valid single step (+1 or -1)
  // Each valid step moves 2 quarter-tracks (1 half-track)
  if (phase_diff == 1) {
    // Stepping inward (toward higher track numbers)
    if (quarter_track_ < 158) // Leave room for 2-step movement
    {
      quarter_track_ += 2;
    } else if (quarter_track_ < 159) {
      quarter_track_ = 159; // Clamp to max
    }
  } else if (phase_diff == -1) {
    // Stepping outward (toward track 0)
    if (quarter_track_ > 1) {
      quarter_track_ -= 2;
    } else if (quarter_track_ > 0) {
      quarter_track_ = 0; // Clamp to min
    }
  }

  // Debug track changes
  if (quarter_track_ != old_quarter_track) {
    static int step_count = 0;
    if (step_count++ < 20) {
      uint8_t track_index = (quarter_track_ < QUARTER_TRACK_COUNT) ? tmap_[quarter_track_] : 0xFF;
      printf("Head step: phase %d->%d, quarter_track %d->%d (track %d), tmap=%d\n",
             last_phase_, phase, old_quarter_track, quarter_track_,
             quarter_track_ / 4, track_index);
    }
  }

  last_phase_ = phase;
}

int WozDiskImage::getQuarterTrack() const { return quarter_track_; }

int WozDiskImage::getTrack() const { return quarter_track_ / 4; }

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
    static int no_track_count = 0;
    if (no_track_count++ < 5) {
      printf("getCurrentTrackData: NO_TRACK at quarter_track=%d\n", quarter_track_);
    }
    return nullptr;
  }
  if (track_index >= static_cast<uint8_t>(tracks_.size())) {
    static int oob_count = 0;
    if (oob_count++ < 5) {
      printf("getCurrentTrackData: track_index=%d >= tracks_.size()=%zu at quarter_track=%d\n",
             track_index, tracks_.size(), quarter_track_);
    }
    return nullptr;
  }

  const TrackData &track = tracks_[track_index];
  if (!track.valid) {
    static int invalid_count = 0;
    if (invalid_count++ < 5) {
      printf("getCurrentTrackData: track %d is not valid at quarter_track=%d\n",
             track_index, quarter_track_);
    }
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
    static int oob_count = 0;
    if (oob_count++ < 5) {
      printf("readBitInternal: byte_offset=%u >= bits.size()=%zu, pos=%u, bit_count=%u\n",
             byte_offset, track->bits.size(), pos, track->bit_count);
    }
    return 0;
  }

  // Debug: show first read from each track
  static int last_track = -1;
  int current_track = quarter_track_ / 4;
  if (current_track != last_track) {
    printf("First read on track %d: byte_offset=%u, byte_value=0x%02X, bits.size()=%zu\n",
           current_track, byte_offset, track->bits[byte_offset], track->bits.size());
    last_track = current_track;
  }

  return (track->bits[byte_offset] >> bit_offset) & 1;
}

uint8_t WozDiskImage::readNibble() {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) {
    static int null_track_count = 0;
    if (null_track_count++ < 5) {
      printf("readNibble: no track data at quarter_track=%d\n", quarter_track_);
    }
    return 0;
  }

  // Sanity check: bit_count should be reasonable (roughly 8 * data size)
  // A normal 5.25" track has about 50,000 bits
  if (track->bit_count < 1000 || track->bit_count > 100000) {
    static int bad_bitcount = 0;
    if (bad_bitcount++ < 5) {
      printf("readNibble: suspicious bit_count=%u at quarter_track=%d\n",
             track->bit_count, quarter_track_);
    }
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
      // Debug: log first few nibbles from each track
      static int track_nibble_counts[40] = {0};
      int track_num = quarter_track_ / 4;
      if (track_num < 40 && track_nibble_counts[track_num]++ < 10) {
        printf("Nibble: track=%d, value=0x%02X, bits_read=%d\n",
               track_num, value, bits_read);
      }
      return value;
    }
  }

  // Timeout - return whatever we have
  static int timeout_count = 0;
  if (timeout_count++ < 5) {
    printf("readNibble: timeout after %d bits, value=0x%02X, track=%d, bit_pos=%u, bit_count=%u\n",
           bits_read, value, quarter_track_ / 4, bit_position_, track->bit_count);
  }
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
  // WOZ format doesn't store raw sector data - it stores bit-level data
  // We would need to decode the entire disk to provide sector data
  *size = 0;
  return nullptr;
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

} // namespace a2e
