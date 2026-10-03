/*
 * disk_inspection.hpp - Read a track's bit cells into what is recorded there
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "disk_image.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace a2e {
namespace inspect {

/*
 * What a nibble is part of. The analyser reads a track the way the drive's
 * latch does, then recognises the standard structure in the nibble stream:
 * self-sync gaps, address fields (D5 AA 96, or D5 AA B5 on a 13-sector disk)
 * and the data fields that follow them. Anything else is either a
 * well-formed nibble that belongs to no standard field, which on a
 * copy-protected disk is where the interesting part usually is, or a byte no
 * disk could have recorded: the noise read off an unformatted stretch.
 */
enum Kind : uint8_t {
  NONE = 0,
  SYNC = 1,          // FF with the two extra zero bits of a self-sync byte
  ADDR_PROLOGUE = 2, // D5 AA 96 / D5 AA B5
  ADDR = 3,          // Volume, track, sector, checksum in 4-and-4
  ADDR_EPILOGUE = 4, // DE AA EB
  DATA_PROLOGUE = 5, // D5 AA AD
  DATA = 6,          // 342 or 410 nibbles and a checksum
  DATA_EPILOGUE = 7, // DE AA EB
  OTHER = 8,         // A valid disk byte in no standard field, or in fields of
                     // a format the analyser does not know (unknown)
  INVALID = 9,       // Not a byte a disk could hold: noise, or no flux
  KIND_COUNT = 10,
};

// Set on an address or data field whose checksum failed
constexpr uint8_t BAD = 0x80;
constexpr uint8_t KIND_MASK = 0x0F;
constexpr uint8_t NO_SECTOR = 0xFF;

struct Nibble {
  uint32_t start_bit = 0; // Cell of its first 1 bit
  uint8_t value = 0;
  uint8_t kind = NONE;    // Kind, with BAD
  uint8_t sector = NO_SECTOR; // Index into TrackAnalysis::sectors
  uint8_t cells = 0;      // Cells until the next nibble starts
};

enum class DataState : uint8_t { None = 0, Good = 1, Bad = 2, Unverified = 3 };

struct Sector {
  uint8_t volume = 0;
  uint8_t track = 0;
  uint8_t sector = 0;
  uint8_t sectors_per_track = 16; // 13 for a D5 AA B5 address field
  bool address_ok = false;
  DataState data = DataState::None;
  uint32_t address_nibble = 0; // Index of the address prologue
  uint32_t data_nibble = 0;    // Index of the data prologue, if any
  std::array<uint8_t, 256> bytes{}; // Decoded, even when the checksum failed
};

struct TrackAnalysis {
  uint32_t bit_count = 0;
  std::vector<Nibble> nibbles; // One revolution, in order round the track
  std::vector<Sector> sectors; // In the order they pass the head
};

/**
 * Analyse one revolution of a track.
 *
 * The latch finds its framing a few nibbles in, so the track is read twice
 * round and the second revolution kept: its nibbles are the ones a program
 * would see, and a field that runs across the end of the track reads whole.
 */
enum class Recording : uint8_t { FiveInch, ThreeAndAHalf };

/**
 * A 3.5" disk uses the same prologues and nibble table, but its address
 * field is five 6-and-2 nibbles (track, sector, side, format, checksum) and
 * its data field is the sector number and 703 nibbles of a 524-byte sector,
 * so the fields are read the 3.5" way when the caller says the disk is one.
 * A 3.5" Sector's volume is its side, and its bytes are the first 256 of the
 * block.
 */
TrackAnalysis analyzeTrack(const uint8_t *bits, uint32_t bit_count,
                           Recording recording = Recording::FiveInch);

/*
 * The buffers the Disk Inspector window reads. Both are little-endian and
 * begin with a four-byte tag and a version, so the host can refuse a layout
 * it does not know rather than draw nonsense.
 *
 * Overview ("DINS"): the whole disk in one read, for the platter map. Every
 * quarter track is divided into `buckets` equal arcs, and each arc carries
 * the kind of most of the cells in it, the sector they belong to, and on a
 * flux track how long its cells took.
 *
 *   header  16 bytes: tag, u16 version, u16 quarter tracks, u16 buckets,
 *           u16 record size, u32 reserved
 *   record  per quarter track:
 *           u8 flags (1 present, 2 flux, 4 has 13-sector fields)
 *           u8 track id (0xFF if none)
 *           u8 sectors found, u8 data fields good,
 *           u8 address checksums bad, u8 data checksums bad, u16 reserved
 *           u32 bit count
 *           u8 kind[buckets], u8 sector[buckets], u8 cell time[buckets]
 *
 * Track ("DTRK"): one quarter track in full, for the detail pane.
 *
 *   header  24 bytes: tag, u16 version, u16 flags (as above), u32 bit count,
 *           u32 nibble count, u16 sector count, u16 timing buckets,
 *           u8 track id, 3 reserved
 *   nibble  8 bytes: u32 start cell, u8 value, u8 kind, u8 sector, u8 cells
 *   sector  272 bytes: u8 volume, track, sector, sectors per track,
 *           u8 address ok, u8 data state, u16 reserved,
 *           u32 address nibble, u32 data nibble, u8 bytes[256]
 *   timing  u8 cell time[timing buckets], zero on a track that is not flux
 */
constexpr int OVERVIEW_VERSION = 1;
constexpr int TRACK_VERSION = 1;
constexpr int QUARTER_TRACKS = 160;

std::vector<uint8_t> buildOverview(DiskImage &image, int buckets);

/**
 * One side of a 3.5" disk in the same layout, so the same platter draws it:
 * the 80 tracks spread over the 160 rings, two rings to a track, track 0
 * outermost. The image's entries are a 3.5" WOZ's, track * 2 + side.
 */
std::vector<uint8_t> buildOverview35(DiskImage &image, int side, int buckets);
std::vector<uint8_t> buildTrackDetail(DiskImage &image, int quarter_track,
                                      int timing_buckets);

} // namespace inspect
} // namespace a2e
