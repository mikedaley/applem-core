/*
 * woz_disk_image.cpp - WOZ 1.0/2.0/2.1 bit-accurate disk image implementation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "woz_disk_image.hpp"
#include "gcr_encoding.hpp"
#include <algorithm>
#include <cmath>
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
  has_flux_ = false;

  // Reset head positioning state
  phase_states_ = 0;
  quarter_track_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;

  // Clear decoded sector cache
  decoded_sectors_.clear();
  sectors_decoded_ = false;
}

void WozDiskImage::resetState() {
  phase_states_ = 0;
  quarter_track_ = 0;
  bit_position_ = 0;
  last_cycle_count_ = 0;
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
  const uint8_t *flux_map = nullptr;

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

    case FLUX_CHUNK_ID:
      // WOZ 2.1: a second quarter-track map, naming TRKS entries that hold
      // flux timings rather than bits
      if (chunk->size >= QUARTER_TRACK_COUNT) {
        flux_map = chunk_data;
      }
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
    trks_ok = parseTrksChunkWoz2(data, size, trks_data, trks_size, flux_map);
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
      // No more bits than the bytes hold, whatever the file says: the
      // readers trust the count, and one past the bytes read past the end.
      tracks_[i].bit_count = std::min<uint32_t>(bit_count, static_cast<uint32_t>(bytes_used) * 8);
      tracks_[i].bits.assign(entry, entry + bytes_used);
      tracks_[i].valid = true;
    }
  }

  return true;
}

bool WozDiskImage::parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                                       const uint8_t *trks_data,
                                       uint32_t trks_size,
                                       const uint8_t *flux_map) {
  // WOZ2 TRKS: 160 track entries (8 bytes each) = 1280 bytes
  // Track data follows at block offsets specified in entries
  static constexpr size_t WOZ2_TRK_TABLE_SIZE = 160 * sizeof(Woz2TrackEntry);

  if (trks_size < WOZ2_TRK_TABLE_SIZE) {
    return false;
  }

  const auto *entries = reinterpret_cast<const Woz2TrackEntry *>(trks_data);

  // Which entries hold flux rather than bits. The spec has a quarter track
  // in FLUX take precedence over TMAP, so the flux map is folded into TMAP
  // here and from then on the drive cannot tell the two kinds apart.
  std::array<bool, 256> is_flux{};
  if (flux_map) {
    for (int i = 0; i < QUARTER_TRACK_COUNT; i++) {
      if (flux_map[i] != NO_TRACK) {
        is_flux[flux_map[i]] = true;
        tmap_[i] = flux_map[i];
      }
    }
  }

  // Find max track index referenced by the map
  int max_track_index = -1;
  for (int i = 0; i < QUARTER_TRACK_COUNT; i++) {
    if (tmap_[i] != NO_TRACK && tmap_[i] > max_track_index) {
      max_track_index = tmap_[i];
    }
  }

  if (max_track_index < 0) {
    return true; // No tracks - empty disk
  }

  // TRKS has 160 entries; a map naming one past them names nothing
  max_track_index = std::min(max_track_index, QUARTER_TRACK_COUNT - 1);
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

    if (is_flux[i]) {
      // For a flux track the "bit count" field is a byte count
      size_t flux_size = std::min<size_t>(entry.bit_count, track_size);
      fluxToPulses(file_data + track_offset, flux_size, tracks_[i]);
      has_flux_ = has_flux_ || tracks_[i].valid;
      continue;
    }

    // No more bits than the blocks hold, whatever the file says. The disk
    // inspector's analyser reads up to three times the count, and a count of
    // $FFFFFFFF over one block read far past the end of it.
    tracks_[i].bit_count = static_cast<uint32_t>(std::min<size_t>(entry.bit_count, track_size * 8));
    tracks_[i].bits.assign(file_data + track_offset,
                           file_data + track_offset + track_size);
    tracks_[i].valid = true;
  }

  return true;
}

// The flux data counts 125ns; the sequencer ticks every 7 cycles of the
// 14.31818MHz (315/22 MHz) master clock. One flux tick is therefore 45/176 of
// a sequencer tick, and a bit cell of eight sequencer ticks is 31.29 flux
// ticks rather than the nominal 32. Using the machine's own clock keeps a
// flux track the same length as a bit track of the same disk, so a
// revolution takes the same emulated time whichever way it was stored.
static constexpr uint64_t FLUX_TO_TICK_NUM = 45;
static constexpr uint64_t FLUX_TO_TICK_DEN = 176;

void WozDiskImage::fluxToPulses(const uint8_t *flux, size_t size,
                                TrackData &out) {
  out.bits.clear();
  out.bit_count = 0;
  out.valid = false;
  out.flux = false;
  out.flux_source.clear();

  // Where each transition falls, in flux ticks from the start of the track
  std::vector<uint64_t> times;
  times.reserve(size);
  uint64_t now = 0;
  for (size_t i = 0; i < size; i++) {
    now += flux[i];
    if (flux[i] != 0xFF) {
      times.push_back(now);
    }
  }
  // The track is one revolution: its length is the time to the last
  // transition, and the first interval is measured from the one before it
  uint64_t ticks = now * FLUX_TO_TICK_NUM / FLUX_TO_TICK_DEN;
  if (times.empty() || ticks == 0) {
    return;
  }

  out.bits.assign((ticks + 7) / 8, 0);
  for (uint64_t t : times) {
    uint64_t tick = (t * FLUX_TO_TICK_NUM / FLUX_TO_TICK_DEN) % ticks;
    out.bits[tick / 8] |= static_cast<uint8_t>(0x80 >> (tick % 8));
  }
  out.bit_count = static_cast<uint32_t>(ticks);
  out.flux_source.assign(flux, flux + size);
  out.flux = true;
  out.valid = true;
}

void WozDiskImage::fluxToBits(const uint8_t *flux, size_t size,
                              TrackData &out,
                              std::vector<uint8_t> *cell_time) {
  static constexpr double FLUX_TICKS_PER_CELL =
      double(TICKS_PER_CELL) * FLUX_TO_TICK_DEN / FLUX_TO_TICK_NUM;

  out.bits.clear();
  out.bit_count = 0;
  out.valid = false;
  out.flux = false;
  out.flux_source.clear();
  if (cell_time) {
    cell_time->clear();
  }

  std::vector<uint8_t> bits;
  bits.reserve(size * 2 / 8 + 1);
  uint32_t bit_count = 0;
  auto push = [&](uint8_t bit) {
    if ((bit_count & 7) == 0) {
      bits.push_back(0);
    }
    if (bit) {
      bits.back() |= static_cast<uint8_t>(0x80 >> (bit_count & 7));
    }
    bit_count++;
  };

  uint32_t ticks = 0;
  for (size_t i = 0; i < size; i++) {
    ticks += flux[i];
    if (flux[i] == 0xFF) {
      continue; // the count carries on into the next byte
    }
    // Rounded to the nearest whole cell, and never less than one: two
    // transitions inside a cell still read as one pulse
    long cells = std::lround(ticks / FLUX_TICKS_PER_CELL);
    if (cells < 1) {
      cells = 1;
    }
    for (long c = 1; c < cells; c++) {
      push(0);
    }
    push(1);
    if (cell_time) {
      // Each cell of the interval took an equal share of it
      long quarters = std::lround(4.0 * ticks / cells);
      cell_time->insert(cell_time->end(), cells,
                        static_cast<uint8_t>(std::min(255L, quarters)));
    }
    ticks = 0;
  }

  if (bit_count == 0) {
    return;
  }
  out.bits = std::move(bits);
  out.bit_count = bit_count;
  out.valid = true;
}

void WozDiskImage::convertFluxTrackToBits(TrackData &track) {
  if (!track.flux) {
    return;
  }
  const bool current = getCurrentTrackData() == &track;
  const uint32_t ticks = track.bit_count;
  std::vector<uint8_t> source = std::move(track.flux_source);
  fluxToBits(source.data(), source.size(), track);
  if (current && ticks > 0 && track.bit_count > 0) {
    bit_position_ = static_cast<uint32_t>(
        uint64_t(bit_position_ % ticks) * track.bit_count / ticks);
  }
}

bool WozDiskImage::isFluxAt(int quarter_track) const {
  if (quarter_track < 0 || quarter_track >= QUARTER_TRACK_COUNT) {
    return false;
  }
  uint8_t index = tmap_[quarter_track];
  return index != NO_TRACK && index < tracks_.size() &&
         tracks_[index].valid && tracks_[index].flux;
}

void WozDiskImage::moveHeadTo(int quarter_track) {
  const bool was_flux = isFluxAt(quarter_track_);
  quarter_track_ = quarter_track;
  const bool is_flux = isFluxAt(quarter_track_);
  if (was_flux && !is_flux) {
    bit_position_ /= TICKS_PER_CELL;
  } else if (!was_flux && is_flux) {
    bit_position_ *= TICKS_PER_CELL;
  }
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

  // Deliberately NOT marked modified. Creating the disk is not a change to it:
  // a blank disk nobody has written to holds nothing worth saving, and marking
  // it here made every eject offer to save an empty image.
  modified_ = false;
}

bool WozDiskImage::createFromTrackBits(const std::vector<BitTrack> &tracks) {
  // Start from a blank disk so the INFO chunk and quarter-track map are the
  // ones createBlank() already settles on, then replace the track data.
  createBlank();

  int trackCount = static_cast<int>(tracks.size());
  if (trackCount <= 0) {
    return false;
  }
  if (trackCount > QUARTER_TRACK_COUNT / 4) {
    trackCount = QUARTER_TRACK_COUNT / 4;
  }

  tracks_.clear();
  tracks_.resize(trackCount);

  bool anyData = false;
  size_t largestBlocks = 0;

  for (int t = 0; t < trackCount; t++) {
    const BitTrack &src = tracks[t];
    if (src.bit_count == 0 || src.bits.empty()) {
      tracks_[t].valid = false;
      tracks_[t].bit_count = 0;
      tracks_[t].bits.clear();
      continue;
    }
    tracks_[t].bits = src.bits;
    tracks_[t].bit_count = src.bit_count;
    tracks_[t].valid = true;
    anyData = true;

    size_t blocks = (src.bits.size() + 511) / 512;
    if (blocks > largestBlocks) largestBlocks = blocks;
  }

  // TMAP: quarter-tracks either side of a whole track read that track, and
  // anything past the last track is unmapped.
  for (int qt = 0; qt < QUARTER_TRACK_COUNT; qt++) {
    int track = qt / 4;
    tmap_[qt] = (track < trackCount && tracks_[track].valid)
                    ? static_cast<uint8_t>(track)
                    : NO_TRACK;
  }

  info_.largest_track = static_cast<uint16_t>(largestBlocks);
  loaded_ = anyData;
  modified_ = false;
  return anyData;
}

bool WozDiskImage::createFrom35Tracks(const std::vector<BitTrack> &tracks, int sides) {
  createBlank();
  info_.disk_type = 2;           // 3.5"
  info_.disk_sides = static_cast<uint8_t>(sides);
  info_.optimal_bit_timing = 16; // 2 microseconds

  const int count = std::min<int>(static_cast<int>(tracks.size()), QUARTER_TRACK_COUNT);
  tracks_.clear();
  tracks_.resize(static_cast<size_t>(count));
  tmap_.fill(NO_TRACK);
  bool anyData = false;
  size_t largestBlocks = 0;
  for (int i = 0; i < count; i++) {
    const BitTrack &src = tracks[static_cast<size_t>(i)];
    if (src.bit_count == 0 || src.bits.empty()) continue;
    tracks_[static_cast<size_t>(i)].bits = src.bits;
    tracks_[static_cast<size_t>(i)].bit_count = src.bit_count;
    tracks_[static_cast<size_t>(i)].valid = true;
    tmap_[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    largestBlocks = std::max(largestBlocks, (src.bits.size() + 511) / 512);
    anyData = true;
  }
  info_.largest_track = static_cast<uint16_t>(largestBlocks);
  loaded_ = anyData;
  modified_ = false;
  return anyData;
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
  }

  // Re-evaluate head position on every magnet change (both ON and OFF). The
  // head is pulled toward a newly energised magnet, so we must step on ON, not
  // only on OFF.
  updateHeadPosition();
}

void WozDiskImage::updateHeadPosition() {
  // Canonical 4-magnet stepper model (as used by AppleWin / OpenEmulator).
  //
  // The head *position* is the state; the magnet currently aligned with the
  // head is derived from the position (each magnet spans 2 quarter-tracks, the
  // four magnets repeating every 8 quarter-tracks). On any magnet-state change
  // we sum the pull from the two neighbouring magnets and, if there is a net
  // pull, move one half-track (2 quarter-tracks) toward the energised
  // neighbour.
  //
  // Deriving the aligned magnet from the position — rather than tracking a
  // separate "current phase" — keeps the head in sync through recalibration
  // (head banging against track 0) and through step patterns that pulse each
  // magnet on/off without overlapping the next. The earlier model only stepped
  // when a phase was turned OFF while an adjacent phase was still ON, so
  // non-overlapping seek routines (used by some copy-protected disks, e.g.
  // subLOGIC's Flight Simulator II) moved the head zero tracks and hung.
  int aligned = (quarter_track_ >> 1) & 3;
  int direction = 0;
  if (phase_states_ & (1 << ((aligned + 1) & 3))) {
    direction += 1; // pull inward (toward higher track numbers)
  }
  if (phase_states_ & (1 << ((aligned + 3) & 3))) {
    direction -= 1; // pull outward (toward track 0)
  }

  if (direction != 0) {
    moveHeadTo(std::max(
        0, std::min(QUARTER_TRACK_COUNT - 1, quarter_track_ + 2 * direction)));
  }
  // If both or neither neighbouring magnets are energised, the head is settled.
}

int WozDiskImage::getQuarterTrack() const { return quarter_track_; }

int WozDiskImage::getTrack() const { return quarter_track_ / 4; }

void WozDiskImage::setQuarterTrack(int quarter_track) {
  moveHeadTo(std::max(0, std::min(quarter_track, QUARTER_TRACK_COUNT - 1)));
}

size_t WozDiskImage::getCurrentNibblePosition() const {
  const TrackData *track = getCurrentTrackData();
  uint32_t cells = (track && track->flux) ? bit_position_ / TICKS_PER_CELL
                                          : bit_position_;
  return cells / 8;
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

  // Calculate how many bits have passed (two sequencer ticks to a cycle)
  uint32_t bits_elapsed =
      track->flux ? static_cast<uint32_t>(elapsed_cycles * 2)
                  : static_cast<uint32_t>(elapsed_cycles / CYCLES_PER_BIT);

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
    uint8_t bit = readBit();
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
    return has_flux_ ? "WOZ 2.1 (flux)" : "WOZ 2.0";
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

// ===== Bit-Level Access (for LSS) =====

uint8_t WozDiskImage::readBit() {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) return 0;
  if (track->flux) {
    // A cell's worth of ticks: a 1 if any transition arrived in it
    uint8_t bit = 0;
    for (uint32_t i = 0; i < TICKS_PER_CELL; i++) {
      bit |= readTick();
    }
    return bit;
  }
  uint8_t bit = readBitInternal();
  bit_position_ = (bit_position_ + 1) % track->bit_count;
  return bit;
}

bool WozDiskImage::inspectQuarterTrack(int quarter_track, TrackView &out) {
  out = TrackView{};
  if (quarter_track < 0 || quarter_track >= QUARTER_TRACK_COUNT) return false;
  uint8_t index = tmap_[quarter_track];
  if (index == NO_TRACK || index >= tracks_.size() || !tracks_[index].valid) {
    return false;
  }
  const TrackData &track = tracks_[index];
  out.track_id = index;
  if (track.flux) {
    TrackData cells;
    fluxToBits(track.flux_source.data(), track.flux_source.size(), cells,
               &out.cell_time);
    out.bits = std::move(cells.bits);
    out.bit_count = cells.bit_count;
    out.flux = true;
  } else {
    out.bits = track.bits;
    out.bit_count = track.bit_count;
  }
  return out.bit_count > 0;
}

double WozDiskImage::getRotation() const {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) return 0.0;
  return double(bit_position_ % track->bit_count) / track->bit_count;
}

bool WozDiskImage::isTickTimed() const {
  const TrackData *track = getCurrentTrackData();
  return track && track->flux;
}

uint8_t WozDiskImage::readTick() {
  const TrackData *track = getCurrentTrackData();
  if (!track || track->bit_count == 0) return 0;
  // A flux track's bits are one per tick, so this is the bit-stream read
  uint8_t pulse = readBitInternal();
  bit_position_ = (bit_position_ + 1) % track->bit_count;
  return pulse;
}

void WozDiskImage::writeBit(uint8_t bit) {
  TrackData *track = getMutableCurrentTrackData();
  if (!track || track->bit_count == 0) return;
  convertFluxTrackToBits(*track);
  writeBitInternal(bit);
  bit_position_ = (bit_position_ + 1) % track->bit_count;
  modified_ = true;
  sectors_decoded_ = false; // bitstream changed — drop stale logical-sector cache
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
  convertFluxTrackToBits(*track);

  // Write 8 bits, MSB first
  for (int i = 7; i >= 0; i--) {
    writeBitInternal((nibble >> i) & 1);
    bit_position_ = (bit_position_ + 1) % track->bit_count;
  }

  modified_ = true;
  sectors_decoded_ = false; // bitstream changed — drop stale logical-sector cache
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

  // WOZ2 layout: the header and the INFO, TMAP and TRKS chunk headers fill
  // the first three blocks exactly, and the TRKS chunk runs on over the track
  // data that follows them. A flux track is written back as the flux bytes it
  // was loaded from, mapped by a FLUX chunk after the tracks (WOZ 2.1), which
  // has to start on a block boundary because INFO names it by block.
  static constexpr size_t TRKS_TABLE_SIZE = 160 * 8; // 1280 bytes
  static constexpr size_t TRACK_DATA_START_BLOCK = 3;
  static constexpr size_t BLOCK_SIZE = 512;
  static constexpr size_t FLUX_CHUNK_SIZE = 8 + QUARTER_TRACK_COUNT;

  auto trackBytes = [](const TrackData &track) -> size_t {
    if (!track.valid || track.bit_count == 0) return 0;
    return track.flux ? track.flux_source.size() : (track.bit_count + 7) / 8;
  };
  auto blocksFor = [](size_t bytes) {
    return (bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
  };

  size_t total_track_blocks = 0;
  size_t largest_blocks = 0;
  size_t largest_flux_blocks = 0;
  bool any_flux = false;
  size_t track_count = std::min<size_t>(tracks_.size(), 160);
  for (size_t i = 0; i < track_count; i++) {
    size_t blocks = blocksFor(trackBytes(tracks_[i]));
    total_track_blocks += blocks;
    if (tracks_[i].flux) {
      any_flux = true;
      largest_flux_blocks = std::max(largest_flux_blocks, blocks);
    } else {
      largest_blocks = std::max(largest_blocks, blocks);
    }
  }

  const size_t flux_block = TRACK_DATA_START_BLOCK + total_track_blocks;
  size_t total_size = flux_block * BLOCK_SIZE;
  if (any_flux) {
    total_size += FLUX_CHUNK_SIZE;
  }
  export_buffer_.assign(total_size, 0);

  size_t offset = 0;
  auto u8 = [&](uint8_t v) { export_buffer_[offset++] = v; };
  auto u32 = [&](uint32_t v) {
    for (int i = 0; i < 4; i++) u8((v >> (8 * i)) & 0xFF);
  };
  auto id = [&](const char *name) {
    for (int i = 0; i < 4; i++) u8(static_cast<uint8_t>(name[i]));
  };

  // === WOZ2 Header (12 bytes) ===
  id("WOZ2");
  u8(0xFF);
  u8(0x0A);
  u8(0x0D);
  u8(0x0A);
  u32(0); // CRC32: zero means not computed, which the spec allows

  // === INFO Chunk ===
  InfoChunk info = info_;
  if (any_flux) {
    info.version = std::max<uint8_t>(info.version, 3);
    info.flux_block = static_cast<uint16_t>(flux_block);
    info.largest_flux_track = static_cast<uint16_t>(largest_flux_blocks);
  } else {
    info.flux_block = 0;
    info.largest_flux_track = 0;
  }
  info.largest_track = static_cast<uint16_t>(largest_blocks);
  id("INFO");
  u32(sizeof(info));
  std::memcpy(&export_buffer_[offset], &info, sizeof(info));
  offset += sizeof(info);

  // === TMAP Chunk, and the FLUX map it hands flux tracks to ===
  std::array<uint8_t, QUARTER_TRACK_COUNT> tmap = tmap_;
  std::array<uint8_t, QUARTER_TRACK_COUNT> flux_map;
  flux_map.fill(NO_TRACK);
  for (int qt = 0; qt < QUARTER_TRACK_COUNT; qt++) {
    if (isFluxAt(qt)) {
      flux_map[qt] = tmap_[qt];
      tmap[qt] = NO_TRACK;
    }
  }
  id("TMAP");
  u32(QUARTER_TRACK_COUNT);
  std::memcpy(&export_buffer_[offset], tmap.data(), QUARTER_TRACK_COUNT);
  offset += QUARTER_TRACK_COUNT;

  // === TRKS Chunk ===
  id("TRKS");
  u32(static_cast<uint32_t>(TRKS_TABLE_SIZE + total_track_blocks * BLOCK_SIZE));

  size_t current_block = TRACK_DATA_START_BLOCK;
  for (size_t i = 0; i < 160; i++) {
    size_t bytes = i < track_count ? trackBytes(tracks_[i]) : 0;
    if (bytes == 0) {
      offset += 8; // empty entry, already zero
      continue;
    }
    const TrackData &track = tracks_[i];
    size_t block_count = blocksFor(bytes);
    // A flux track's count is of bytes, a bit track's of bits
    uint32_t count = track.flux ? static_cast<uint32_t>(bytes) : track.bit_count;

    u8(current_block & 0xFF);
    u8((current_block >> 8) & 0xFF);
    u8(block_count & 0xFF);
    u8((block_count >> 8) & 0xFF);
    u32(count);

    const uint8_t *data = track.flux ? track.flux_source.data() : track.bits.data();
    std::memcpy(&export_buffer_[current_block * BLOCK_SIZE], data,
                std::min(bytes, track.flux ? track.flux_source.size()
                                           : track.bits.size()));
    current_block += block_count;
  }

  // === FLUX Chunk ===
  if (any_flux) {
    offset = flux_block * BLOCK_SIZE;
    id("FLUX");
    u32(QUARTER_TRACK_COUNT);
    std::memcpy(&export_buffer_[offset], flux_map.data(), QUARTER_TRACK_COUNT);
  }

  *size = export_buffer_.size();
  return export_buffer_.data();
}

// ===== Sector Decoding Implementation =====

std::vector<uint8_t> WozDiskImage::readTrackNibbles(size_t track_index) const {
  std::vector<uint8_t> nibbles;

  if (track_index >= tracks_.size() || !tracks_[track_index].valid) {
    return nibbles;
  }

  // A flux track is read as the cells a drive would see in it
  TrackData cells;
  if (tracks_[track_index].flux) {
    const auto &source = tracks_[track_index].flux_source;
    fluxToBits(source.data(), source.size(), cells);
  }
  const TrackData &track = tracks_[track_index].flux ? cells : tracks_[track_index];
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
    uint8_t volume = GCR::decode4and4(nibbles[i + 3], nibbles[i + 4]);
    uint8_t track = GCR::decode4and4(nibbles[i + 5], nibbles[i + 6]);
    uint8_t sector = GCR::decode4and4(nibbles[i + 7], nibbles[i + 8]);
    uint8_t checksum = GCR::decode4and4(nibbles[i + 9], nibbles[i + 10]);

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
    if (GCR::decode6and2(&nibbles[data_start], sectors[sector].data(), true)) {
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
      int logical_sector = GCR::DOS_PHYSICAL_TO_LOGICAL[phys_sector];
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
