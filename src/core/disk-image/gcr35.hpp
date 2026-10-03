/*
 * gcr35.hpp - How an Apple 3.5" disk is recorded
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace a2e {
namespace GCR35 {

/*
 * An 800K disk is 80 tracks on each of two sides, and the drive turns faster
 * towards the middle so that the bit cell stays at 2us however far in the head
 * is: five zones of sixteen tracks, holding 12, 11, 10, 9 and 8 sectors. A
 * sector is 524 bytes, twelve of tag and the 512 a block holds, written in
 * 6-and-2 GCR with a running three-byte checksum, which is a different
 * encoding from a 5.25" disk's even though it shares the nibble table.
 *
 * A block image lists the blocks track by track and, within a track, side 0's
 * sectors before side 1's; a single-sided 400K image has side 0 alone.
 */

constexpr int TRACKS = 80;
constexpr int ZONES = 5;
constexpr int TRACKS_PER_ZONE = 16;
constexpr int SECTOR_BYTES = 512;
constexpr int TAG_BYTES = 12;
constexpr int RAW_SECTOR_BYTES = TAG_BYTES + SECTOR_BYTES; // 524
constexpr int ENCODED_SECTOR_NIBBLES = 703;                // 699 of data, 4 of checksum
constexpr size_t SIZE_800K = 1600 * SECTOR_BYTES;
constexpr size_t SIZE_400K = 800 * SECTOR_BYTES;

/** Sectors on a track: 12 at the outside, 8 at the inside. */
inline int sectorsOnTrack(int track) {
  return 12 - (track / TRACKS_PER_ZONE);
}

/**
 * Which block of an image holds a sector.
 * @param sides 1 for a 400K disk, 2 for an 800K one
 * @return the block number, or -1 for a side the disk does not have
 */
int blockFor(int track, int side, int sector, int sides);

/** Blocks on a disk with this many sides. */
int blockCount(int sides);

/**
 * The bit stream of one side of one track, as the drive lays it down: a gap,
 * then every sector in 2:1 interleave, each an address field and a data
 * field, separated by self-sync bytes of ten bits.
 *
 * @param blocks the whole image, in block order
 * @param sides  1 or 2
 * @param bits   receives the cells, packed MSB first
 * @return the number of cells
 */
uint32_t encodeTrack(const uint8_t *blocks, int track, int side, int sides,
                     std::vector<uint8_t> &bits);

/**
 * Read a track's sectors back into an image, the way the drive would: find
 * each address field, check it, find its data field, check that. A sector
 * that cannot be read leaves the block as it was.
 *
 * @return the number of sectors read
 */
int decodeTrack(const uint8_t *bits, uint32_t bitCount, int track, int side,
                int sides, uint8_t *blocks);

/** A disk byte's six bits, or 0xFF for a byte that is not one. */
uint8_t decodeNibble(uint8_t nibble);

/** 524 bytes in, 699 nibbles and a four-nibble checksum out (703). */
void encodeSector(const uint8_t *raw, uint8_t *nibbles);

/** The reverse; false if a nibble is not a disk byte or the checksum fails. */
bool decodeSector(const uint8_t *nibbles, uint8_t *raw);

} // namespace GCR35
} // namespace a2e
