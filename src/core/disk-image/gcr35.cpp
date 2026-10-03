/*
 * gcr35.cpp - How an Apple 3.5" disk is recorded
 *
 * The sector encoding is the one CiderPress2's notes describe
 * (ciderpress2.com/formatdoc/Nibble-notes.html): the 524 bytes are split
 * three ways under a rotating checksum, and the top two bits of each group of
 * three go in a fourth nibble ahead of them.
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "gcr35.hpp"
#include "gcr_encoding.hpp"

#include <cstring>

namespace a2e {
namespace GCR35 {

namespace {

constexpr int CHUNKS = 175; // ceil(524 / 3)

// Disk byte -> six bits, 0xFF for a byte that is not one.
const std::array<uint8_t, 256> &decodeTable() {
  static const auto table = [] {
    std::array<uint8_t, 256> t{};
    t.fill(0xFF);
    for (int i = 0; i < 64; i++) t[GCR::ENCODE_6_AND_2[i]] = static_cast<uint8_t>(i);
    return t;
  }();
  return table;
}

// The order sectors pass the head: 2:1, so one can be dealt with while the
// next goes by. Physical position -> sector number.
constexpr std::array<std::array<int, 12>, ZONES> INTERLEAVE = {{
    {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11},
    {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5},
    {0, 5, 1, 6, 2, 7, 3, 8, 4, 9},
    {0, 5, 1, 6, 2, 7, 3, 8, 4},
    {0, 4, 1, 5, 2, 6, 3, 7},
}};

// The format byte in every address field: double-sided, 2:1 interleave.
constexpr uint8_t FORMAT_DOUBLE_SIDED = 0x22;
constexpr uint8_t FORMAT_SINGLE_SIDED = 0x02;

// Gaps, in self-sync bytes. A track is a little longer than the drive can
// hold at that zone's speed, which only means it turns a little slower here.
constexpr int GAP_START = 64;
constexpr int GAP_ADDRESS_TO_DATA = 5;
constexpr int GAP_AFTER_SECTOR = 36;

class BitWriter {
public:
  explicit BitWriter(std::vector<uint8_t> &out) : out_(out) { out_.clear(); }
  void bit(int b) {
    if ((count_ & 7) == 0) out_.push_back(0);
    if (b) out_.back() |= static_cast<uint8_t>(0x80 >> (count_ & 7));
    count_++;
  }
  void byte(uint8_t v) {
    for (int i = 7; i >= 0; i--) bit((v >> i) & 1);
  }
  // FF and two zero cells, which a reader falls into step with.
  void sync(int n) {
    for (int i = 0; i < n; i++) {
      byte(0xFF);
      bit(0);
      bit(0);
    }
  }
  uint32_t count() const { return count_; }

private:
  std::vector<uint8_t> &out_;
  uint32_t count_ = 0;
};

// Reads disk bytes the way the latch does: shift until the top bit is set.
class NibbleReader {
public:
  NibbleReader(const uint8_t *bits, uint32_t count) : bits_(bits), count_(count) {}
  uint8_t next() {
    uint8_t v = 0;
    while (!(v & 0x80) && consumed_ < limit()) {
      const uint32_t at = position_;
      position_ = position_ + 1 == count_ ? 0 : position_ + 1;
      consumed_++;
      v = static_cast<uint8_t>((v << 1) | ((bits_[at >> 3] >> (7 - (at & 7))) & 1));
    }
    return v;
  }
  bool done() const { return consumed_ >= limit(); }

private:
  // Twice round, so a sector across the end of the track reads whole.
  uint64_t limit() const { return static_cast<uint64_t>(count_) * 2 + 8 * 1024; }
  const uint8_t *bits_;
  uint32_t count_;
  uint32_t position_ = 0;
  uint64_t consumed_ = 0;
};

} // namespace

uint8_t decodeNibble(uint8_t nibble) { return decodeTable()[nibble]; }

int blockFor(int track, int side, int sector, int sides) {
  if (track < 0 || track >= TRACKS || side < 0 || side >= sides) return -1;
  if (sector < 0 || sector >= sectorsOnTrack(track)) return -1;
  int block = 0;
  for (int t = 0; t < track; t++) block += sectorsOnTrack(t) * sides;
  return block + side * sectorsOnTrack(track) + sector;
}

int blockCount(int sides) {
  int blocks = 0;
  for (int t = 0; t < TRACKS; t++) blocks += sectorsOnTrack(t) * sides;
  return blocks;
}

void encodeSector(const uint8_t *raw, uint8_t *nibbles) {
  uint8_t part0[CHUNKS], part1[CHUNKS], part2[CHUNKS];
  uint32_t chk0 = 0, chk1 = 0, chk2 = 0;
  int in = 0, idx = 0;
  for (;;) {
    chk0 = (chk0 & 0xFF) << 1;
    if (chk0 & 0x100) chk0++;
    uint8_t v = raw[in++];
    chk2 += v;
    if (chk0 & 0x100) {
      chk2++;
      chk0 &= 0xFF;
    }
    part0[idx] = static_cast<uint8_t>(v ^ chk0);

    v = raw[in++];
    chk1 += v;
    if (chk2 > 0xFF) {
      chk1++;
      chk2 &= 0xFF;
    }
    part1[idx] = static_cast<uint8_t>(v ^ chk2);

    if (in == RAW_SECTOR_BYTES) break;

    v = raw[in++];
    chk0 += v;
    if (chk1 > 0xFF) {
      chk0++;
      chk1 &= 0xFF;
    }
    part2[idx] = static_cast<uint8_t>(v ^ chk1);
    idx++;
  }
  part2[CHUNKS - 1] = 0;

  const auto &enc = GCR::ENCODE_6_AND_2;
  int out = 0;
  for (int i = 0; i < CHUNKS; i++) {
    const uint8_t twos = static_cast<uint8_t>(((part0[i] & 0xC0) >> 2) |
                                              ((part1[i] & 0xC0) >> 4) |
                                              ((part2[i] & 0xC0) >> 6));
    nibbles[out++] = enc[twos];
    nibbles[out++] = enc[part0[i] & 0x3F];
    nibbles[out++] = enc[part1[i] & 0x3F];
    if (i != CHUNKS - 1) nibbles[out++] = enc[part2[i] & 0x3F];
  }
  const uint8_t twos = static_cast<uint8_t>(((chk0 & 0xC0) >> 6) |
                                            ((chk1 & 0xC0) >> 4) |
                                            ((chk2 & 0xC0) >> 2));
  nibbles[out++] = enc[twos];
  nibbles[out++] = enc[chk2 & 0x3F];
  nibbles[out++] = enc[chk1 & 0x3F];
  nibbles[out++] = enc[chk0 & 0x3F];
}

bool decodeSector(const uint8_t *nibbles, uint8_t *raw) {
  const auto &dec = decodeTable();
  uint8_t part0[CHUNKS], part1[CHUNKS], part2[CHUNKS];
  int in = 0;
  for (int i = 0; i < CHUNKS; i++) {
    const uint8_t twos = dec[nibbles[in++]];
    const uint8_t n0 = dec[nibbles[in++]];
    const uint8_t n1 = dec[nibbles[in++]];
    const uint8_t n2 = i != CHUNKS - 1 ? dec[nibbles[in++]] : 0;
    if (twos == 0xFF || n0 == 0xFF || n1 == 0xFF || n2 == 0xFF) return false;
    part0[i] = static_cast<uint8_t>(n0 | ((twos << 2) & 0xC0));
    part1[i] = static_cast<uint8_t>(n1 | ((twos << 4) & 0xC0));
    part2[i] = static_cast<uint8_t>(n2 | ((twos << 6) & 0xC0));
  }
  const uint8_t twos = dec[nibbles[in++]];
  const uint8_t c2 = dec[nibbles[in++]];
  const uint8_t c1 = dec[nibbles[in++]];
  const uint8_t c0 = dec[nibbles[in++]];
  if (twos == 0xFF || c0 == 0xFF || c1 == 0xFF || c2 == 0xFF) return false;
  const uint8_t readChk2 = static_cast<uint8_t>(c2 | ((twos << 2) & 0xC0));
  const uint8_t readChk1 = static_cast<uint8_t>(c1 | ((twos << 4) & 0xC0));
  const uint8_t readChk0 = static_cast<uint8_t>(c0 | ((twos << 6) & 0xC0));

  uint32_t chk0 = 0, chk1 = 0, chk2 = 0;
  int out = 0, idx = 0;
  for (;;) {
    chk0 = (chk0 & 0xFF) << 1;
    if (chk0 & 0x100) chk0++;
    uint8_t v = static_cast<uint8_t>(part0[idx] ^ chk0);
    chk2 += v;
    if (chk0 & 0x100) {
      chk2++;
      chk0 &= 0xFF;
    }
    raw[out++] = v;

    v = static_cast<uint8_t>(part1[idx] ^ chk2);
    chk1 += v;
    if (chk2 > 0xFF) {
      chk1++;
      chk2 &= 0xFF;
    }
    raw[out++] = v;

    if (out == RAW_SECTOR_BYTES) break;

    v = static_cast<uint8_t>(part2[idx] ^ chk1);
    chk0 += v;
    if (chk1 > 0xFF) {
      chk0++;
      chk1 &= 0xFF;
    }
    raw[out++] = v;
    idx++;
  }
  return readChk0 == static_cast<uint8_t>(chk0) && readChk1 == static_cast<uint8_t>(chk1) &&
         readChk2 == static_cast<uint8_t>(chk2);
}

uint32_t encodeTrack(const uint8_t *blocks, int track, int side, int sides,
                     std::vector<uint8_t> &bits) {
  const auto &enc = GCR::ENCODE_6_AND_2;
  BitWriter w(bits);
  w.sync(GAP_START);

  const int sectors = sectorsOnTrack(track);
  const auto &order = INTERLEAVE[static_cast<size_t>(track / TRACKS_PER_ZONE)];
  const uint8_t format = sides == 2 ? FORMAT_DOUBLE_SIDED : FORMAT_SINGLE_SIDED;
  uint8_t raw[RAW_SECTOR_BYTES];
  uint8_t nibbles[ENCODED_SECTOR_NIBBLES];

  for (int position = 0; position < sectors; position++) {
    const int sector = order[static_cast<size_t>(position)];

    // Address field: the track's low six bits, the sector, the side with the
    // track's seventh bit, the format, and their checksum.
    const uint8_t trackLow = static_cast<uint8_t>(track & 0x3F);
    const uint8_t sideByte = static_cast<uint8_t>((side ? 0x20 : 0) | ((track >> 6) & 1));
    const uint8_t sum = static_cast<uint8_t>((trackLow ^ sector ^ sideByte ^ format) & 0x3F);
    w.byte(0xD5);
    w.byte(0xAA);
    w.byte(0x96);
    w.byte(enc[trackLow]);
    w.byte(enc[static_cast<size_t>(sector)]);
    w.byte(enc[sideByte]);
    w.byte(enc[format]);
    w.byte(enc[sum]);
    w.byte(0xDE);
    w.byte(0xAA);
    w.sync(GAP_ADDRESS_TO_DATA);

    // Data field: the sector number again, then the tags and the block.
    std::memset(raw, 0, TAG_BYTES);
    const int block = blockFor(track, side, sector, sides);
    std::memcpy(raw + TAG_BYTES, blocks + static_cast<size_t>(block) * SECTOR_BYTES,
                SECTOR_BYTES);
    encodeSector(raw, nibbles);
    w.byte(0xD5);
    w.byte(0xAA);
    w.byte(0xAD);
    w.byte(enc[static_cast<size_t>(sector)]);
    for (uint8_t n : nibbles) w.byte(n);
    w.byte(0xDE);
    w.byte(0xAA);
    w.sync(GAP_AFTER_SECTOR);
  }
  return w.count();
}

int decodeTrack(const uint8_t *bits, uint32_t bitCount, int track, int side,
                int sides, uint8_t *blocks) {
  if (!bits || bitCount == 0) return 0;
  const auto &dec = decodeTable();
  const int sectors = sectorsOnTrack(track);
  bool found[12] = {};
  int foundCount = 0;
  NibbleReader r(bits, bitCount);
  uint8_t n0 = 0, n1 = 0, n2 = 0;
  uint8_t nibbles[ENCODED_SECTOR_NIBBLES];
  uint8_t raw[RAW_SECTOR_BYTES];

  while (foundCount < sectors && !r.done()) {
    n2 = n1;
    n1 = n0;
    n0 = r.next();
    if (!(n2 == 0xD5 && n1 == 0xAA && n0 == 0x96)) continue;
    n0 = n1 = n2 = 0;

    uint8_t f[5];
    for (uint8_t &v : f) v = dec[r.next()];
    if (f[0] == 0xFF || f[1] == 0xFF || f[2] == 0xFF || f[3] == 0xFF || f[4] == 0xFF) continue;
    if (((f[0] ^ f[1] ^ f[2] ^ f[3]) & 0x3F) != f[4]) continue;
    const int fieldTrack = f[0] | ((f[2] & 1) << 6);
    const int fieldSide = (f[2] >> 5) & 1;
    const int sector = f[1];
    if (fieldTrack != track || fieldSide != side || sector >= sectors) continue;

    // The data field follows within a few bytes.
    uint8_t d0 = 0, d1 = 0, d2 = 0;
    bool prologue = false;
    for (int i = 0; i < 48 && !r.done(); i++) {
      d2 = d1;
      d1 = d0;
      d0 = r.next();
      if (d2 == 0xD5 && d1 == 0xAA && d0 == 0xAD) {
        prologue = true;
        break;
      }
    }
    if (!prologue) continue;
    r.next(); // the sector number, again
    for (uint8_t &n : nibbles) n = r.next();
    if (!decodeSector(nibbles, raw)) continue;

    const int block = blockFor(track, side, sector, sides);
    std::memcpy(blocks + static_cast<size_t>(block) * SECTOR_BYTES, raw + TAG_BYTES,
                SECTOR_BYTES);
    if (!found[sector]) {
      found[sector] = true;
      foundCount++;
    }
  }
  return foundCount;
}

} // namespace GCR35
} // namespace a2e
