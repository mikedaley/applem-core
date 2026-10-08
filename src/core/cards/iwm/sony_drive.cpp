/*
 * sony_drive.cpp - An Apple 3.5" drive
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "sony_drive.hpp"
#include "../../disk-image/disk_inspection.hpp"
#include "../../disk-image/gcr35.hpp"

#include <cstring>

namespace a2e {

namespace {

// Timings, in cycles of the 1.023MHz clock. The step and settle times are
// GSSquared's, which the firmware is content with; a real drive takes about
// 12ms a step, which only makes a disk slower to read.
constexpr uint64_t STEP_CYCLES = 5000;
constexpr uint64_t SETTLE_CYCLES = 1000;      // after a step within a zone
constexpr uint64_t ZONE_SETTLE_CYCLES = 5000; // after a step that changes speed
constexpr uint64_t SPIN_UP_CYCLES = 5000;
constexpr uint64_t SPIN_DOWN_CYCLES = 511500; // half a second after deselection

// The disk-switched bit, which the table has as 0 = "the user ejected a disk".
// It reads the other way round here, and the firmware boots with it; see
// sony_drive.hpp.
constexpr bool SWITCHED_READS_HIGH = true;

// 2MG
constexpr size_t TWOIMG_HEADER = 64;
constexpr uint32_t TWOIMG_LOCKED = 0x80000000u;

uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool isWoz(const uint8_t *data, size_t size) {
  return size >= 22 && data[0] == 'W' && data[1] == 'O' && data[2] == 'Z' &&
         (data[3] == '1' || data[3] == '2');
}

// A WOZ's INFO chunk comes first: disk type 2 is a 3.5" disk.
bool isWoz35(const uint8_t *data, size_t size) {
  return isWoz(data, size) && std::memcmp(data + 12, "INFO", 4) == 0 && data[21] == 2;
}

bool isTwoImg(const uint8_t *data, size_t size) {
  return size >= TWOIMG_HEADER && std::memcmp(data, "2IMG", 4) == 0;
}

bool isBlockSize(size_t size) {
  return size == GCR35::SIZE_800K || size == GCR35::SIZE_400K;
}

} // namespace

// ===== Media =====

bool SonyDrive::isDiskImage(const uint8_t *data, size_t size) {
  if (!data) return false;
  if (isWoz(data, size)) return isWoz35(data, size);
  if (isTwoImg(data, size)) {
    const uint32_t offset = le32(data + 0x18);
    const uint32_t length = le32(data + 0x1C);
    return le32(data + 0x0C) == 1 && isBlockSize(length) &&
           static_cast<size_t>(offset) + length <= size;
  }
  return isBlockSize(size);
}

bool SonyDrive::decodeBlockImage(const uint8_t *data, size_t size) {
  sides_ = size == GCR35::SIZE_800K ? 2 : 1;
  blocks_.assign(data, data + size);
  std::vector<WozDiskImage::BitTrack> tracks(GCR35::TRACKS * 2);
  for (int track = 0; track < GCR35::TRACKS; track++) {
    for (int side = 0; side < sides_; side++) {
      WozDiskImage::BitTrack &t = tracks[static_cast<size_t>(track * 2 + side)];
      t.bit_count = GCR35::encodeTrack(blocks_.data(), track, side, sides_, t.bits);
    }
  }
  auto image = std::make_unique<WozDiskImage>();
  if (!image->createFrom35Tracks(tracks, sides_)) return false;
  image_ = std::move(image);
  blockSource_ = true;
  return true;
}

bool SonyDrive::loadImage(const uint8_t *data, size_t size, const std::string &filename) {
  image_.reset();
  filename_.clear();
  prefix_.clear();
  suffix_.clear();
  blocks_.clear();
  blockSource_ = false;
  writeProtected_ = false;

  if (isWoz(data, size)) {
    auto image = std::make_unique<WozDiskImage>();
    if (!image->load(data, size, filename)) return false;
    writeProtected_ = image->isWriteProtected();
    sides_ = 2;
    image_ = std::move(image);
  } else if (isTwoImg(data, size)) {
    const uint32_t offset = le32(data + 0x18);
    const uint32_t length = le32(data + 0x1C);
    writeProtected_ = (le32(data + 0x10) & TWOIMG_LOCKED) != 0;
    prefix_.assign(data, data + offset);
    suffix_.assign(data + offset + length, data + size);
    if (!decodeBlockImage(data + offset, length)) return false;
  } else {
    if (!decodeBlockImage(data, size)) return false;
  }

  image_->setFilename(filename);
  filename_ = filename;
  revision_++;
  return true;
}

bool SonyDrive::insert(const uint8_t *data, size_t size, const std::string &filename) {
  if (!isDiskImage(data, size)) return false;
  eject();
  clearEjected();
  if (!loadImage(data, size, filename)) return false;
  // A different disk: the firmware learns it from this bit.
  diskSwitched_ = true;
  seekHead();
  return true;
}

bool SonyDrive::restore(const uint8_t *data, size_t size, const std::string &filename) {
  if (!isDiskImage(data, size) || !loadImage(data, size, filename)) return false;
  seekHead();
  return true;
}

void SonyDrive::eject() {
  revision_++;
  image_.reset();
  filename_.clear();
  diskSwitched_ = true;
}

const uint8_t *SonyDrive::exportData(size_t *size) {
  *size = 0;
  if (!image_) return nullptr;
  if (!blockSource_) return image_->exportData(size);

  // The blocks as they were, overwritten by every sector the tracks still
  // read, so a sector the machine damaged keeps what it had.
  for (int track = 0; track < GCR35::TRACKS; track++) {
    for (int side = 0; side < sides_; side++) {
      DiskImage::TrackView view;
      if (!image_->inspectQuarterTrack(track * 2 + side, view)) continue;
      GCR35::decodeTrack(view.bits.data(), view.bit_count, track, side, sides_,
                         blocks_.data());
    }
  }
  exportBuffer_ = prefix_;
  exportBuffer_.insert(exportBuffer_.end(), blocks_.begin(), blocks_.end());
  exportBuffer_.insert(exportBuffer_.end(), suffix_.begin(), suffix_.end());
  *size = exportBuffer_.size();
  return exportBuffer_.data();
}

const uint8_t *SonyDrive::exportEjected(size_t *size) {
  *size = 0;
  if (!ejected_) return nullptr;
  // Export through the same path by putting the disk back for a moment.
  std::swap(image_, ejected_);
  std::swap(blockSource_, ejectedBlockSource_);
  std::swap(sides_, ejectedSides_);
  std::swap(prefix_, ejectedPrefix_);
  std::swap(suffix_, ejectedSuffix_);
  std::swap(blocks_, ejectedBlocks_);
  const uint8_t *data = exportData(size);
  std::swap(image_, ejected_);
  std::swap(blockSource_, ejectedBlockSource_);
  std::swap(sides_, ejectedSides_);
  std::swap(prefix_, ejectedPrefix_);
  std::swap(suffix_, ejectedSuffix_);
  std::swap(blocks_, ejectedBlocks_);
  return data;
}

void SonyDrive::clearEjected() {
  ejected_.reset();
  ejectedPrefix_.clear();
  ejectedSuffix_.clear();
  ejectedBlocks_.clear();
}

// ===== The lines =====

void SonyDrive::setLine(int line, bool on, uint64_t now) {
  if (line >= 0 && line < 3) {
    ca_[line] = on ? 1 : 0;
    return;
  }
  if (line != 3) return;
  const bool rising = on && !lstrb_;
  lstrb_ = on;
  if (rising) strobe(now);
}

void SonyDrive::setEnabled(bool enabled, uint64_t now) {
  updateMotor(now);
  if (enabled_ && !enabled && motorOn_) motorStopAt_ = now + SPIN_DOWN_CYCLES;
  if (enabled) motorStopAt_ = 0;
  enabled_ = enabled;
}

void SonyDrive::updateMotor(uint64_t now) {
  if (motorStopAt_ != 0 && now >= motorStopAt_) {
    motorOn_ = false;
    motorStopAt_ = 0;
  }
}

void SonyDrive::strobe(uint64_t now) {
  // A drive that is not selected does not hear the strobe.
  if (!enabled_) return;
  updateMotor(now);
  switch (selector()) {
  case 0x0:
    stepOutward_ = false;
    break;
  case 0x8:
    stepOutward_ = true;
    break;
  case 0x9:
    diskSwitched_ = false;
    break;
  case 0x2: {
    const int to = track_ + (stepOutward_ ? -1 : 1);
    if (to < 0 || to >= GCR35::TRACKS) break;
    const bool newZone = to / GCR35::TRACKS_PER_ZONE != track_ / GCR35::TRACKS_PER_ZONE;
    track_ = to;
    steppingUntil_ = now + STEP_CYCLES;
    readyAt_ = steppingUntil_ + (newZone ? ZONE_SETTLE_CYCLES : SETTLE_CYCLES);
    seekHead();
    break;
  }
  case 0x4:
    if (!motorOn_) readyAt_ = now + SPIN_UP_CYCLES;
    motorOn_ = true;
    motorStopAt_ = 0;
    break;
  case 0xC:
    motorOn_ = false;
    motorStopAt_ = 0;
    break;
  case 0xE:
    // The disk comes out. It is kept for the host until it has been saved.
    revision_++;
    if (image_) {
      ejected_ = std::move(image_);
      ejectedBlockSource_ = blockSource_;
      ejectedSides_ = sides_;
      ejectedPrefix_ = std::move(prefix_);
      ejectedSuffix_ = std::move(suffix_);
      ejectedBlocks_ = std::move(blocks_);
      filename_.clear();
    }
    diskSwitched_ = true;
    motorOn_ = false;
    break;
  default:
    break;
  }
}

bool SonyDrive::sense(uint64_t now) {
  updateMotor(now);
  switch (selector()) {
  case 0x0: return stepOutward_;
  case 0x1: return !image_;
  case 0x2: return now >= steppingUntil_;
  case 0x3: return image_ && !writeProtected_;
  case 0x4: return !motorOn_;
  case 0x5: return track_ != 0;
  case 0x6: return SWITCHED_READS_HIGH ? diskSwitched_ : !diskSwitched_;
  case 0x7: {
    // Sixty pulses a turn: high and low thirty times each.
    if (!image_ || !motorOn_) return false;
    return static_cast<int>(image_->getRotation() * 120.0) & 1;
  }
  case 0x8:
  case 0x9: {
    // Reading either head's data line puts that head in charge.
    const int side = selector() & 1;
    if (side != side_) {
      side_ = side;
      seekHead();
    }
    return false;
  }
  case 0xC: return true; // double-sided
  case 0xD: return !(image_ && motorOn_ && now >= readyAt_);
  case 0xF: return false; // installed
  default: return false;
  }
}

// ===== The disk turning =====

void SonyDrive::seekHead() {
  if (image_) image_->setQuarterTrack(track_ * 2 + side_);
}

bool SonyDrive::isSpinning(uint64_t now) {
  updateMotor(now);
  return image_ && motorOn_;
}

uint8_t SonyDrive::readBit() {
  if (!image_ || !image_->hasData()) return 0;
  return image_->readBit();
}

void SonyDrive::writeBit(uint8_t bit) {
  if (!image_) return;
  if (writeProtected_ || !image_->hasData()) {
    image_->readBit(); // the disk still turns under a head that cannot write
    return;
  }
  image_->writeBit(bit);
  revision_++;
}

std::vector<uint8_t> SonyDrive::overview(int side, int buckets) {
  if (!image_) return {};
  return inspect::buildOverview35(*image_, side, buckets);
}

void SonyDrive::reset() {
  motorOn_ = false;
  motorStopAt_ = 0;
  enabled_ = false;
  lstrb_ = false;
  ca_[0] = ca_[1] = ca_[2] = 0;
  sel_ = false;
  steppingUntil_ = 0;
  readyAt_ = 0;
}

// ===== Save state =====

namespace {
void put64(std::vector<uint8_t> &out, uint64_t v) {
  for (int i = 0; i < 8; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t get64(const uint8_t *&p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v |= static_cast<uint64_t>(*p++) << (8 * i);
  return v;
}
constexpr size_t STATE_BYTES = 10 + 8 * 3;
} // namespace

void SonyDrive::serialize(std::vector<uint8_t> &out) const {
  out.push_back(static_cast<uint8_t>(track_));
  out.push_back(static_cast<uint8_t>(side_));
  out.push_back(stepOutward_ ? 1 : 0);
  out.push_back(motorOn_ ? 1 : 0);
  out.push_back(diskSwitched_ ? 1 : 0);
  out.push_back(static_cast<uint8_t>(ca_[0] | (ca_[1] << 1) | (ca_[2] << 2)));
  out.push_back(lstrb_ ? 1 : 0);
  out.push_back(sel_ ? 1 : 0);
  out.push_back(enabled_ ? 1 : 0);
  out.push_back(writeProtected_ ? 1 : 0);
  put64(out, motorStopAt_);
  put64(out, steppingUntil_);
  put64(out, readyAt_);
}

bool SonyDrive::deserialize(const uint8_t *&data, const uint8_t *end) {
  if (end - data < static_cast<ptrdiff_t>(STATE_BYTES)) return false;
  track_ = data[0] < GCR35::TRACKS ? data[0] : 0;
  side_ = data[1] & 1;
  stepOutward_ = data[2] != 0;
  motorOn_ = data[3] != 0;
  diskSwitched_ = data[4] != 0;
  ca_[0] = data[5] & 1;
  ca_[1] = (data[5] >> 1) & 1;
  ca_[2] = (data[5] >> 2) & 1;
  lstrb_ = data[6] != 0;
  sel_ = data[7] != 0;
  enabled_ = data[8] != 0;
  writeProtected_ = data[9] != 0;
  data += 10;
  motorStopAt_ = get64(data);
  steppingUntil_ = get64(data);
  readyAt_ = get64(data);
  return true;
}

} // namespace a2e
