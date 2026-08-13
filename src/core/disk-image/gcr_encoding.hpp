/*
 * gcr_encoding.hpp - GCR encoding tables and routines for Disk II emulation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace a2e {
namespace GCR {

// 6-and-2 encoding lookup table
// Maps 6-bit values (0-63) to valid disk nibbles (bit 7 set, no adjacent zeros)
constexpr std::array<uint8_t, 64> ENCODE_6_AND_2 = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC,
    0xAD, 0xAE, 0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA,
    0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6,
    0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7,
    0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5,
    0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

// Address field markers
constexpr uint8_t ADDR_PROLOGUE[3] = {0xD5, 0xAA, 0x96};
constexpr uint8_t ADDR_EPILOGUE[3] = {0xDE, 0xAA, 0xEB};

// Data field markers
constexpr uint8_t DATA_PROLOGUE[3] = {0xD5, 0xAA, 0xAD};
constexpr uint8_t DATA_EPILOGUE[3] = {0xDE, 0xAA, 0xEB};

// Self-sync byte
constexpr uint8_t SYNC_BYTE = 0xFF;

// ===== Sector interleave =====
//
// A track's sectors are not written in numerical order: the interleave gives
// the CPU time to process one sector before the next passes the head. Which
// interleave applies is a property of the filesystem that wrote the disk, so
// both are here and the caller picks.

// DOS 3.3: logical sector (file offset) -> physical sector (position on track)
constexpr std::array<int, 16> DOS_LOGICAL_TO_PHYSICAL = {
    0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};

// DOS 3.3: physical -> logical
constexpr std::array<int, 16> DOS_PHYSICAL_TO_LOGICAL = {
    0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};

// ProDOS: logical -> physical
constexpr std::array<int, 16> PRODOS_LOGICAL_TO_PHYSICAL = {
    0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};

// ProDOS: physical -> logical
constexpr std::array<int, 16> PRODOS_PHYSICAL_TO_LOGICAL = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15};

// ProDOS logical sector -> the DOS logical sector holding it, which is how a
// ProDOS volume is found inside a DOS-ordered image (and the reverse, since
// the mapping is its own inverse).
constexpr std::array<uint8_t, 16> PRODOS_TO_DOS_SECTOR = {
    0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15};

/**
 * 4-and-4 encoding for address field values
 * Splits byte into odd bits (first) and even bits (second)
 * Each byte is OR'd with 0xAA to ensure high bit set
 *
 * @param value Byte to encode
 * @return Pair of encoded bytes (odd bits, even bits)
 */
inline std::pair<uint8_t, uint8_t> encode4and4(uint8_t value) {
  // Odd bits: bits 7,5,3,1 go to positions 6,4,2,0
  uint8_t odd = 0xAA | ((value >> 1) & 0x55);
  // Even bits: bits 6,4,2,0 stay in positions 6,4,2,0
  uint8_t even = 0xAA | (value & 0x55);
  return {odd, even};
}

/**
 * Decode a 4-and-4 encoded pair back to the byte it carries
 *
 * @param odd  First encoded byte (bits 7,5,3,1)
 * @param even Second encoded byte (bits 6,4,2,0)
 */
inline uint8_t decode4and4(uint8_t odd, uint8_t even) {
  return static_cast<uint8_t>(((odd << 1) & 0xAA) | (even & 0x55));
}

/**
 * Encode 256 bytes of sector data using 6-and-2 encoding
 * Returns 343 nibbles (342 encoded data + 1 checksum)
 *
 * @param data Pointer to 256 bytes of sector data
 * @return Vector of 343 encoded nibbles
 */
std::vector<uint8_t> encode6and2(const uint8_t *data);

/**
 * Decode 343 nibbles back to 256 bytes of sector data.
 *
 * @param nibbles         343 nibbles: 342 encoded bytes plus the checksum
 * @param output          Receives 256 decoded bytes
 * @param requireChecksum true to reject a sector whose checksum does not
 *        match, false to decode it anyway. Sector images are routinely a
 *        little imperfect and the data is still worth having; a bit-accurate
 *        image is held to the stricter standard, since a sector that does not
 *        verify is exactly what its copy protection is made of.
 * @return true if the sector was decoded
 */
bool decode6and2(const uint8_t *nibbles, uint8_t *output,
                 bool requireChecksum);

/**
 * Build a complete sector as a nibble stream
 * Includes sync bytes, address field, gap, and data field
 *
 * @param volume Volume number (typically 254)
 * @param track Track number (0-34)
 * @param sector Sector number (0-15)
 * @param data Pointer to 256 bytes of sector data
 * @return Vector of nibbles for the complete sector
 */
std::vector<uint8_t> buildSector(uint8_t volume, uint8_t track, uint8_t sector,
                                 const uint8_t *data);

} // namespace GCR
} // namespace a2e
