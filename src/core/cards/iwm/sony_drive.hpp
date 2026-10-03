/*
 * sony_drive.hpp - An Apple 3.5" drive
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../../disk-image/woz_disk_image.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace a2e {

/**
 * SonyDrive - the Apple 3.5" drive (A9M0106), a Sony mechanism with a little
 * logic of its own
 *
 * A 5.25" drive is told everything: the four stepper phases move the head and
 * ENABLE is the motor. A 3.5" drive is asked. The same four lines from the IWM
 * mean something else here: CA0, CA1 and CA2, with SEL from bit 7 of $C031,
 * make a sixteen-way selector, and LSTRB is a strobe. With LSTRB low the
 * selector chooses one of sixteen status bits, which the drive puts on the
 * line the IWM reads as SENSE (bit 7 of the status register); a rising edge on
 * LSTRB performs one of the control functions instead — set the step
 * direction, step a track, start or stop the spindle, eject.
 *
 * Index is CA2 CA1 CA0 SEL, high to low:
 *
 *   status                                 control (on the strobe)
 *   0000 step direction (1 = outward)      0000 step inward
 *   0001 disk in place (0 = yes)           1000 step outward
 *   0010 stepping (0 = yes)                1001 clear disk switched
 *   0011 write protect (0 = protected)     0010 step one track
 *   0100 spindle (0 = turning)             0100 spindle on
 *   0101 track 0 (0 = at it)               1100 spindle off
 *   0110 disk switched                     1110 eject
 *   0111 tachometer, 60 pulses a turn
 *   1000 lower head (and selects it)
 *   1001 upper head (and selects it)
 *   1100 sides (1 = double)
 *   1101 ready (0 = ready to read)
 *   1111 installed (0 = yes)
 *
 * The table is Neil Parker's, from the IIgs firmware's own SEL35/STAT35/CONT35
 * routines; GSSquared reads the disk-switched bit the other way round from the
 * table and boots with it, so this does too.
 *
 * ENABLE does not turn the spindle. It selects the drive — the light, the
 * clamp — and the spindle answers to its own control bits; a drive deselected
 * with the spindle running stops it half a second later.
 *
 * The disk is held as a 3.5" WOZ, whatever it arrived as: a WOZ is kept as it
 * is, and a 400K or 800K block image is encoded track by track into one, and
 * decoded back into blocks when it is saved, so what goes back out is the
 * format that came in.
 */
class SonyDrive {
public:
  SonyDrive() = default;

  // ===== Media =====

  /**
   * Insert a disk: a 3.5" WOZ, a 400K or 800K block image (.po, .hdv, .dsk,
   * .img or anything else of exactly that size), or a 2MG holding one.
   * @return false if it is none of those
   */
  bool insert(const uint8_t *data, size_t size, const std::string &filename);
  /**
   * Put back the disk a save state carried, leaving the mechanism exactly as
   * the state left it: unlike insert(), this is not a disk being changed.
   */
  bool restore(const uint8_t *data, size_t size, const std::string &filename);
  /** The same for a drive the state had empty. */
  void restoreEmpty() {
    image_.reset();
    filename_.clear();
  }
  /** Take the disk out, as the host does it. */
  void eject();
  bool hasDisk() const { return image_ != nullptr; }
  const std::string &filename() const { return filename_; }
  bool isModified() const { return image_ && image_->isModified(); }
  bool isWriteProtected() const { return writeProtected_; }

  /** The disk in the format it arrived in, written back from its tracks. */
  const uint8_t *exportData(size_t *size);

  /** Whether an image this size, with this name, is a 3.5" disk. */
  static bool isDiskImage(const uint8_t *data, size_t size);

  /**
   * The machine ejected the disk (the eject control). The disk is out of the
   * drive, but kept here so the host can save what was written to it before
   * forgetting it: exportEjected() is that disk, until clearEjected().
   */
  bool hasEjected() const { return ejected_ != nullptr; }
  const uint8_t *exportEjected(size_t *size);
  void clearEjected();

  // ===== The lines from the IWM =====

  /** CA0, CA1, CA2 (0-2) or LSTRB (3). */
  void setLine(int line, bool on, uint64_t now);
  /** SEL, from $C031 bit 7. */
  void setSel(bool sel) { sel_ = sel; }
  /** /ENBL routed to this drive. */
  void setEnabled(bool enabled, uint64_t now);
  /** The status bit the selector names, as SENSE reads it. */
  bool sense(uint64_t now);

  // ===== The disk turning =====

  /** Whether bits are passing the head: selected and the spindle turning. */
  bool isSpinning(uint64_t now);
  uint8_t readBit();
  void writeBit(uint8_t bit);

  // ===== For the host and the debugger =====

  bool isMotorOn(uint64_t now) {
    updateMotor(now);
    return motorOn_;
  }
  int track() const { return track_; }
  int side() const { return side_; }
  bool isEnabled() const { return enabled_; }
  /** How far round the disk is under the head, 0 to 1. */
  double rotation() const { return image_ ? image_->getRotation() : 0.0; }
  /** Changes whenever what is on the disk may have: insert, eject, a write. */
  uint32_t revision() const { return revision_; }
  /** The disk as the drive holds it, a 3.5" WOZ: entries are track * 2 + side. */
  DiskImage *image() { return image_.get(); }
  /** One side as the Disk Inspector's platter draws it (inspect::buildOverview35). */
  std::vector<uint8_t> overview(int side, int buckets);

  /** What happens across a reset: the spindle stops, the head stays. */
  void reset();

  /** The mechanism's state (not the disk) for a save state. */
  void serialize(std::vector<uint8_t> &out) const;
  bool deserialize(const uint8_t *&data, const uint8_t *end);

private:
  uint8_t selector() const {
    return static_cast<uint8_t>((ca_[2] << 3) | (ca_[1] << 2) | (ca_[0] << 1) | (sel_ ? 1 : 0));
  }
  void strobe(uint64_t now);
  void seekHead();
  void updateMotor(uint64_t now);
  bool decodeBlockImage(const uint8_t *data, size_t size);
  bool loadImage(const uint8_t *data, size_t size, const std::string &filename);

  // The disk, and what it came in as
  std::unique_ptr<WozDiskImage> image_;
  std::string filename_;
  bool writeProtected_ = false;
  bool blockSource_ = false;     // a block image, written back as one
  int sides_ = 2;
  std::vector<uint8_t> prefix_;  // a 2MG's header, kept for the save
  std::vector<uint8_t> suffix_;  // and anything after its data
  std::vector<uint8_t> blocks_;  // the blocks, decoded back on export
  std::vector<uint8_t> exportBuffer_;

  std::unique_ptr<WozDiskImage> ejected_;
  bool ejectedBlockSource_ = false;
  int ejectedSides_ = 2;
  std::vector<uint8_t> ejectedPrefix_, ejectedSuffix_, ejectedBlocks_;

  // The lines
  uint8_t ca_[3] = {0, 0, 0};
  bool lstrb_ = false;
  bool sel_ = false;
  bool enabled_ = false;

  // The mechanism
  int track_ = 0;
  int side_ = 0;
  bool stepOutward_ = false;
  bool motorOn_ = false;
  bool diskSwitched_ = false;
  uint64_t motorStopAt_ = 0;  // deselected with the spindle on: when it stops
  uint64_t steppingUntil_ = 0;
  uint64_t readyAt_ = 0;
  uint32_t revision_ = 0;
};

} // namespace a2e
