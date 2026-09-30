/*
 * disk_inspection.cpp - Read a track's bit cells into what is recorded there
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disk_inspection.hpp"
#include "gcr_encoding.hpp"

#include <algorithm>
#include <map>

namespace a2e {
namespace inspect {

namespace {

constexpr size_t DATA_62_NIBBLES = 343; // 342 and the checksum
constexpr size_t DATA_53_NIBBLES = 411; // 410 and the checksum

// How far past an address field its data field may start: DOS 3.3 leaves a
// gap of five to ten sync bytes, and some formatters leave rather more
constexpr uint32_t DATA_SEARCH_NIBBLES = 48;

inline uint8_t bitAt(const uint8_t *bits, uint32_t pos) {
  return (bits[pos >> 3] >> (7 - (pos & 7))) & 1;
}

// A disk byte has its top bit set and never three zero cells in a row: the
// read amplifier cannot hold its level that long without a transition, and
// what it produces instead is noise. A nibble followed by that many zeros is
// just as suspect.
bool isValidDiskByte(uint8_t value, uint8_t cells) {
  if (cells > 10) return false;
  for (int k = 0; k <= 5; k++) {
    if (((value >> k) & 7) == 0) return false;
  }
  return true;
}

struct Writer {
  std::vector<uint8_t> &out;
  void u8(uint8_t v) { out.push_back(v); }
  void u16(uint16_t v) {
    u8(v & 0xFF);
    u8(v >> 8);
  }
  void u32(uint32_t v) {
    u16(v & 0xFFFF);
    u16(v >> 16);
  }
  void tag(const char *t) {
    for (int i = 0; i < 4; i++) u8(static_cast<uint8_t>(t[i]));
  }
};

// Average a per-cell time over equal arcs of the track
std::vector<uint8_t> bucketTimes(const std::vector<uint8_t> &cell_time,
                                 uint32_t bit_count, int buckets) {
  std::vector<uint8_t> out(buckets, 0);
  if (cell_time.empty() || bit_count == 0) return out;
  std::vector<uint32_t> sum(buckets, 0), count(buckets, 0);
  uint32_t n = std::min<uint32_t>(bit_count, cell_time.size());
  for (uint32_t c = 0; c < n; c++) {
    int b = static_cast<int>(uint64_t(c) * buckets / bit_count);
    sum[b] += cell_time[c];
    count[b]++;
  }
  for (int b = 0; b < buckets; b++) {
    if (count[b]) out[b] = static_cast<uint8_t>(sum[b] / count[b]);
  }
  return out;
}

uint8_t trackFlags(const DiskImage::TrackView &view,
                   const TrackAnalysis &analysis) {
  uint8_t flags = 1;
  if (view.flux) flags |= 2;
  for (const Sector &s : analysis.sectors) {
    if (s.sectors_per_track == 13) flags |= 4;
  }
  return flags;
}

} // namespace

TrackAnalysis analyzeTrack(const uint8_t *bits, uint32_t bit_count) {
  TrackAnalysis result;
  result.bit_count = bit_count;
  if (!bits || bit_count == 0) return result;

  // ---- The latch: shift cells in until the top bit is set ----------------
  // Read from the start of the track, and keep what starts in the second
  // revolution, by when the framing has settled on the track's own.
  const uint64_t first = bit_count;
  const uint64_t end = uint64_t(bit_count) * 2;
  const uint64_t limit = uint64_t(bit_count) * 3;
  uint8_t shift = 0;
  uint64_t start = 0;
  for (uint64_t pos = 0; pos < limit; pos++) {
    if (pos >= end && shift == 0) break;
    uint8_t bit = bitAt(bits, static_cast<uint32_t>(pos % bit_count));
    if (shift == 0) {
      if (!bit) continue;
      start = pos;
    }
    shift = static_cast<uint8_t>((shift << 1) | bit);
    if (shift & 0x80) {
      if (start >= first && start < end) {
        Nibble n;
        n.start_bit = static_cast<uint32_t>(start - first);
        n.value = shift;
        result.nibbles.push_back(n);
      }
      shift = 0;
    }
  }

  auto &nibs = result.nibbles;
  const size_t count = nibs.size();
  if (count == 0) return result;
  for (size_t i = 0; i < count; i++) {
    uint64_t next = (i + 1 < count) ? nibs[i + 1].start_bit
                                    : uint64_t(nibs[0].start_bit) + bit_count;
    nibs[i].cells = static_cast<uint8_t>(
        std::min<uint64_t>(255, next - nibs[i].start_bit));
  }

  // ---- Fields -------------------------------------------------------------
  // The nibbles are a ring: a sector can straddle the end of the track
  auto at = [&](size_t i) -> uint8_t { return nibs[i % count].value; };
  auto mark = [&](size_t i, size_t n, uint8_t kind, uint8_t sector) {
    for (size_t k = 0; k < n; k++) {
      Nibble &nib = nibs[(i + k) % count];
      nib.kind = kind;
      nib.sector = sector;
    }
  };

  if (count >= 16) {
    for (size_t i = 0; i < count && result.sectors.size() < NO_SECTOR; i++) {
      if (nibs[i].kind != NONE) continue;
      if (at(i) != 0xD5 || at(i + 1) != 0xAA) continue;
      const bool thirteen = at(i + 2) == 0xB5;
      if (at(i + 2) != 0x96 && !thirteen) continue;

      Sector sector;
      sector.sectors_per_track = thirteen ? 13 : 16;
      sector.address_nibble = static_cast<uint32_t>(i);
      sector.volume = GCR::decode4and4(at(i + 3), at(i + 4));
      sector.track = GCR::decode4and4(at(i + 5), at(i + 6));
      sector.sector = GCR::decode4and4(at(i + 7), at(i + 8));
      uint8_t checksum = GCR::decode4and4(at(i + 9), at(i + 10));
      sector.address_ok =
          (sector.volume ^ sector.track ^ sector.sector) == checksum;

      const uint8_t index = static_cast<uint8_t>(result.sectors.size());
      mark(i, 3, ADDR_PROLOGUE, index);
      mark(i + 3, 8, ADDR | (sector.address_ok ? 0 : BAD), index);
      size_t next = i + 11;
      if (at(next) == 0xDE && at(next + 1) == 0xAA) {
        size_t n = at(next + 2) == 0xEB ? 3 : 2;
        mark(next, n, ADDR_EPILOGUE, index);
        next += n;
      }

      // The data field that belongs to it, unless another address field
      // comes first
      for (size_t j = i + 11; j < i + 11 + DATA_SEARCH_NIBBLES; j++) {
        if (at(j) != 0xD5 || at(j + 1) != 0xAA) continue;
        if (at(j + 2) != 0xAD) break;

        const size_t length = thirteen ? DATA_53_NIBBLES : DATA_62_NIBBLES;
        if (length + 3 > count) break;
        sector.data_nibble = static_cast<uint32_t>(j % count);
        if (thirteen) {
          // 5-and-3 is recognised by its place, not decoded
          sector.data = DataState::Unverified;
        } else {
          std::array<uint8_t, DATA_62_NIBBLES> field;
          for (size_t k = 0; k < DATA_62_NIBBLES; k++) field[k] = at(j + 3 + k);
          if (GCR::decode6and2(field.data(), sector.bytes.data(), true)) {
            sector.data = DataState::Good;
          } else {
            GCR::decode6and2(field.data(), sector.bytes.data(), false);
            sector.data = DataState::Bad;
          }
        }
        mark(j, 3, DATA_PROLOGUE, index);
        mark(j + 3, length,
             DATA | (sector.data == DataState::Bad ? BAD : 0), index);
        size_t tail = j + 3 + length;
        if (at(tail) == 0xDE && at(tail + 1) == 0xAA) {
          mark(tail, at(tail + 2) == 0xEB ? 3 : 2, DATA_EPILOGUE, index);
        }
        break;
      }
      result.sectors.push_back(sector);
    }
  }

  // ---- Everything else ----------------------------------------------------
  for (Nibble &nib : nibs) {
    if (nib.kind != NONE) continue;
    // A self-sync byte is nine or ten cells; longer is a gap in the flux
    if (nib.value == 0xFF && nib.cells >= 9 && nib.cells <= 10) {
      nib.kind = SYNC;
    } else if (isValidDiskByte(nib.value, nib.cells)) {
      nib.kind = OTHER;
    } else {
      nib.kind = INVALID;
    }
  }
  return result;
}

std::vector<uint8_t> buildOverview(DiskImage &image, int buckets) {
  buckets = std::max(16, std::min(buckets, 2048));
  const int record = 12 + buckets * 3;

  std::vector<uint8_t> out;
  out.reserve(16 + QUARTER_TRACKS * record);
  Writer w{out};
  w.tag("DINS");
  w.u16(OVERVIEW_VERSION);
  w.u16(QUARTER_TRACKS);
  w.u16(static_cast<uint16_t>(buckets));
  w.u16(static_cast<uint16_t>(record));
  w.u32(0);

  // Quarter tracks that read the same stored track are analysed once
  std::map<int, std::vector<uint8_t>> done;

  for (int qt = 0; qt < QUARTER_TRACKS; qt++) {
    DiskImage::TrackView view;
    if (!image.inspectQuarterTrack(qt, view)) {
      out.insert(out.end(), record, 0);
      out[out.size() - record + 1] = 0xFF; // no track id
      continue;
    }
    auto cached = done.find(view.track_id);
    if (cached != done.end()) {
      out.insert(out.end(), cached->second.begin(), cached->second.end());
      continue;
    }

    TrackAnalysis analysis = analyzeTrack(view.bits.data(), view.bit_count);
    std::vector<uint8_t> rec;
    rec.reserve(record);
    Writer r{rec};

    uint8_t found = 0, good = 0, addrBad = 0, dataBad = 0;
    for (const Sector &s : analysis.sectors) {
      found++;
      if (s.data == DataState::Good) good++;
      if (!s.address_ok) addrBad++;
      if (s.data == DataState::Bad) dataBad++;
    }
    r.u8(trackFlags(view, analysis));
    r.u8(static_cast<uint8_t>(std::min(254, view.track_id)));
    r.u8(found);
    r.u8(good);
    r.u8(addrBad);
    r.u8(dataBad);
    r.u16(0);
    r.u32(view.bit_count);

    // Each arc takes the kind covering most of its cells. A failed checksum
    // counts double, so a bad sector is not lost in a narrow arc.
    std::vector<std::array<uint32_t, KIND_COUNT + 1>> weight(buckets);
    for (auto &a : weight) a.fill(0);
    std::vector<uint8_t> sectorAt(buckets, NO_SECTOR);
    const uint32_t bits = view.bit_count;
    for (const Nibble &nib : analysis.nibbles) {
      const uint8_t kind = nib.kind & KIND_MASK;
      const bool bad = (nib.kind & BAD) != 0;
      const uint8_t sector = nib.sector == NO_SECTOR
                                 ? NO_SECTOR
                                 : analysis.sectors[nib.sector].sector;
      for (uint32_t c = 0; c < nib.cells; c++) {
        uint32_t cell = (nib.start_bit + c) % bits;
        int b = static_cast<int>(uint64_t(cell) * buckets / bits);
        if (bad) {
          weight[b][KIND_COUNT] += 2;
        } else {
          weight[b][kind] += 1;
        }
        // The sector at the middle of the arc names it
        uint32_t middle =
            static_cast<uint32_t>((uint64_t(2 * b + 1) * bits) / (2 * buckets));
        if (cell == middle) sectorAt[b] = sector;
      }
    }
    for (int b = 0; b < buckets; b++) {
      int best = NONE;
      uint32_t most = 0;
      for (int k = 0; k <= KIND_COUNT; k++) {
        if (weight[b][k] > most) {
          most = weight[b][k];
          best = k;
        }
      }
      // The bad slot stands for the field kind it spoiled; which one does not
      // matter to the map, so it is drawn as bad data
      r.u8(best == KIND_COUNT ? (DATA | BAD) : static_cast<uint8_t>(best));
    }
    rec.insert(rec.end(), sectorAt.begin(), sectorAt.end());
    auto times = bucketTimes(view.cell_time, bits, buckets);
    rec.insert(rec.end(), times.begin(), times.end());

    out.insert(out.end(), rec.begin(), rec.end());
    done.emplace(view.track_id, std::move(rec));
  }
  return out;
}

std::vector<uint8_t> buildTrackDetail(DiskImage &image, int quarter_track,
                                      int timing_buckets) {
  timing_buckets = std::max(16, std::min(timing_buckets, 8192));
  std::vector<uint8_t> out;
  Writer w{out};

  DiskImage::TrackView view;
  const bool present = image.inspectQuarterTrack(quarter_track, view);
  TrackAnalysis analysis;
  if (present) {
    analysis = analyzeTrack(view.bits.data(), view.bit_count);
  }

  w.tag("DTRK");
  w.u16(TRACK_VERSION);
  w.u16(present ? trackFlags(view, analysis) : 0);
  w.u32(view.bit_count);
  w.u32(static_cast<uint32_t>(analysis.nibbles.size()));
  w.u16(static_cast<uint16_t>(analysis.sectors.size()));
  w.u16(static_cast<uint16_t>(timing_buckets));
  w.u8(present ? static_cast<uint8_t>(std::min(254, view.track_id)) : 0xFF);
  w.u8(0);
  w.u8(0);
  w.u8(0);

  out.reserve(out.size() + analysis.nibbles.size() * 8 +
              analysis.sectors.size() * 272 + timing_buckets);
  for (const Nibble &n : analysis.nibbles) {
    w.u32(n.start_bit);
    w.u8(n.value);
    w.u8(n.kind);
    w.u8(n.sector);
    w.u8(n.cells);
  }
  for (const Sector &s : analysis.sectors) {
    w.u8(s.volume);
    w.u8(s.track);
    w.u8(s.sector);
    w.u8(s.sectors_per_track);
    w.u8(s.address_ok ? 1 : 0);
    w.u8(static_cast<uint8_t>(s.data));
    w.u16(0);
    w.u32(s.address_nibble);
    w.u32(s.data_nibble);
    out.insert(out.end(), s.bytes.begin(), s.bytes.end());
  }
  auto times = bucketTimes(view.cell_time, view.bit_count, timing_buckets);
  out.insert(out.end(), times.begin(), times.end());
  return out;
}

} // namespace inspect
} // namespace a2e
