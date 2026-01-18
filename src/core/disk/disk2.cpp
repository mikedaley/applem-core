#include "disk2.hpp"
#include "dsk_disk_image.hpp"
#include "woz_disk_image.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Disk2Controller::Disk2Controller() { reset(); }

void Disk2Controller::reset() {
  // Reset controller to power-on state
  motor_on_ = false;
  motor_off_cycle_ = 0;
  selected_drive_ = 0;
  q6_ = false;
  q7_ = false;
  phase_states_ = 0;

  // Reset timing state
  total_cycles_ = 0;
  last_read_cycle_[0] = 0;
  last_read_cycle_[1] = 0;
  data_latch_ = 0;
  latch_valid_ = false;

  // Reset write state
  write_latch_ = 0;

  // Don't reset disk images - preserve loaded disks across reset
}

void Disk2Controller::stopMotor() {
  // Stop motor immediately without resetting other state
  motor_on_ = false;
  motor_off_cycle_ = 0;
}

bool Disk2Controller::isMotorOn() const {
  // Check if motor-off delay has elapsed
  if (motor_on_ && motor_off_cycle_ != 0) {
    if (total_cycles_ >= motor_off_cycle_ + MOTOR_OFF_DELAY_CYCLES) {
      // Delay has elapsed, actually turn off the motor
      motor_on_ = false;
      motor_off_cycle_ = 0;
    }
  }
  return motor_on_;
}

uint8_t Disk2Controller::read(uint8_t reg) {
  return handleSoftSwitch(reg & 0x0F, false);
}

void Disk2Controller::write(uint8_t reg, uint8_t value) {
  uint8_t offset = reg & 0x0F;

  // In write mode, any write loads data into the shift register
  // The actual disk write happens when Q6L is accessed (see handleSoftSwitch)
  if (q7_) {
    write_latch_ = value;
  }

  handleSoftSwitch(offset, true);
}

uint8_t Disk2Controller::peek(uint8_t reg) const {
  // Non-side-effecting read for debugger/memory viewer
  uint8_t offset = reg & 0x0F;

  switch (offset) {
  case PHASE0_OFF:
  case PHASE0_ON:
  case PHASE1_OFF:
  case PHASE1_ON:
  case PHASE2_OFF:
  case PHASE2_ON:
  case PHASE3_OFF:
  case PHASE3_ON:
    // Return current phase state
    return (phase_states_ >> (offset / 2)) & 1 ? 0x80 : 0x00;

  case MOTOR_OFF:
  case MOTOR_ON:
    return isMotorOn() ? 0x80 : 0x00;

  case DRIVE1_SELECT:
  case DRIVE2_SELECT:
    return selected_drive_ == 1 ? 0x80 : 0x00;

  case Q6L: // Data register in read mode
    return data_latch_;

  case Q6H: // Write protect sense
    if (hasDisk(selected_drive_)) {
      const DiskImage *disk = disk_images_[selected_drive_].get();
      return disk->isWriteProtected() ? 0x80 : 0x00;
    }
    return 0x00;

  case Q7L: // Read mode indicator
    return q7_ ? 0x00 : 0x80;

  case Q7H: // Write mode indicator
    return q7_ ? 0x80 : 0x00;

  default:
    return 0x00;
  }
}

uint8_t Disk2Controller::handleSoftSwitch(uint8_t offset, bool is_write) {
  (void)is_write; // Both reads and writes toggle/access the switches

  // Helper to forward phase changes to disk image
  auto setPhase = [this](int phase, bool on) {
    if (hasDisk(selected_drive_)) {
      disk_images_[selected_drive_]->setPhase(phase, on);
    }
  };

  switch (offset) {
  case PHASE0_OFF:
    phase_states_ &= ~0x01;
    setPhase(0, false);
    break;
  case PHASE0_ON:
    phase_states_ |= 0x01;
    setPhase(0, true);
    break;
  case PHASE1_OFF:
    phase_states_ &= ~0x02;
    setPhase(1, false);
    break;
  case PHASE1_ON:
    phase_states_ |= 0x02;
    setPhase(1, true);
    break;
  case PHASE2_OFF:
    phase_states_ &= ~0x04;
    setPhase(2, false);
    break;
  case PHASE2_ON:
    phase_states_ |= 0x04;
    setPhase(2, true);
    break;
  case PHASE3_OFF:
    phase_states_ &= ~0x08;
    setPhase(3, false);
    break;
  case PHASE3_ON:
    phase_states_ |= 0x08;
    setPhase(3, true);
    break;

  case MOTOR_OFF:
    // Start the motor-off delay timer (motor stays on for ~1 second)
    if (motor_on_ && motor_off_cycle_ == 0) {
      motor_off_cycle_ = total_cycles_;
    }
    break;
  case MOTOR_ON:
    // Cancel any pending motor-off and turn motor on
    motor_off_cycle_ = 0;
    motor_on_ = true;
    break;

  case DRIVE1_SELECT:
    selected_drive_ = 0;
    break;
  case DRIVE2_SELECT:
    selected_drive_ = 1;
    break;

  case Q6L:
    q6_ = false;
    if (!q7_) {
      // Read mode (Q7=0, Q6=0): return data from disk
      return readDiskData();
    } else {
      // Write mode (Q7=1, Q6=0): shift out data to disk
      writeDiskData();
    }
    break;

  case Q6H:
    q6_ = true;
    // In read mode (Q7=0, Q6=1): return write protect status
    // Bit 7 = 1 means write protected
    if (!q7_) {
      if (hasDisk(selected_drive_)) {
        return disk_images_[selected_drive_]->isWriteProtected() ? 0x80 : 0x00;
      }
      return 0x80; // No disk = write protected
    }
    break;

  case Q7L:
    if (q7_) {
      // Switching from write mode to read mode
      // Reset timing so we don't jump the disk position
      last_read_cycle_[selected_drive_] = getCycles();
      latch_valid_ = false;
    }
    q7_ = false; // Read mode
    break;

  case Q7H:
    q7_ = true; // Write mode
    break;
  }

  // Default return value for reads
  return 0x00;
}

// ===== Disk operations =====

bool Disk2Controller::insertDisk(int drive, const uint8_t *data, size_t size,
                                 const std::string &filename) {
  if (drive < 0 || drive > 1) {
    return false;
  }

  // Determine file type from extension
  std::string lower_filename = filename;
  std::transform(lower_filename.begin(), lower_filename.end(),
                 lower_filename.begin(), ::tolower);

  std::unique_ptr<DiskImage> image;

  if (lower_filename.find(".woz") != std::string::npos) {
    image = std::make_unique<WozDiskImage>();
  } else if (lower_filename.find(".dsk") != std::string::npos ||
             lower_filename.find(".do") != std::string::npos ||
             lower_filename.find(".po") != std::string::npos) {
    image = std::make_unique<DskDiskImage>();
  } else {
    // Try to detect format from content
    if (size >= 12 && data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
      image = std::make_unique<WozDiskImage>();
    } else if (size == 143360) {
      image = std::make_unique<DskDiskImage>();
    } else {
      return false;
    }
  }

  if (!image->load(data, size, filename)) {
    return false;
  }

  disk_images_[drive] = std::move(image);
  return true;
}

bool Disk2Controller::insertBlankDisk(int drive) {
  if (drive < 0 || drive > 1) {
    return false;
  }

  auto image = std::make_unique<WozDiskImage>();
  image->createBlank();

  disk_images_[drive] = std::move(image);
  return true;
}

void Disk2Controller::ejectDisk(int drive) {
  if (drive < 0 || drive > 1) {
    return;
  }

  // Only turn off motor if this is the currently selected drive
  if (selected_drive_ == drive) {
    motor_on_ = false;
    motor_off_cycle_ = 0;
  }

  disk_images_[drive].reset();
}

bool Disk2Controller::hasDisk(int drive) const {
  if (drive < 0 || drive > 1) {
    return false;
  }
  return disk_images_[drive] != nullptr && disk_images_[drive]->isLoaded();
}

const DiskImage *Disk2Controller::getDiskImage(int drive) const {
  if (drive < 0 || drive > 1) {
    return nullptr;
  }
  return disk_images_[drive].get();
}

int Disk2Controller::getCurrentTrack() const {
  if (hasDisk(selected_drive_)) {
    return disk_images_[selected_drive_]->getTrack();
  }
  return -1;
}

int Disk2Controller::getQuarterTrack() const {
  if (hasDisk(selected_drive_)) {
    return disk_images_[selected_drive_]->getQuarterTrack();
  }
  return -1;
}

uint8_t Disk2Controller::readDiskData() {
  // If motor is off or no disk, return 0
  if (!isMotorOn() || !hasDisk(selected_drive_)) {
    return 0;
  }

  DiskImage *disk = disk_images_[selected_drive_].get();

  // Check if current head position has data
  if (!disk->hasData()) {
    return 0;
  }

  // The Disk II hardware shifts bits into a latch. When bit 7 becomes set,
  // we have a valid nibble. The boot ROM polls Q6L in a loop:
  //   LDY $C08C,X  ; 4 cycles
  //   BPL loop     ; 2/3 cycles
  // It expects bit 7 to be CLEAR between nibbles. After reading a valid
  // nibble (~32 cycles to shift in), the ROM processes it, then polls again.
  // If we return the same nibble with bit 7 still set, the ROM will process
  // it twice! We must clear bit 7 after the first read until the next nibble.

  uint64_t current_cycle = getCycles();
  uint64_t &last_cycle = last_read_cycle_[selected_drive_];

  // Check if enough time has passed for a new nibble
  bool new_nibble_ready =
      (last_cycle == 0) || (current_cycle >= last_cycle + CYCLES_PER_NIBBLE);

  if (new_nibble_ready) {
    // In real hardware, the disk spins continuously at ~300 RPM.
    // If more than CYCLES_PER_NIBBLE elapsed since last read, the disk
    // has rotated past multiple nibbles. We need to "catch up" the disk
    // position for the extra elapsed time before reading.
    if (last_cycle != 0) {
      uint64_t elapsed = current_cycle - last_cycle;
      // Calculate how many extra nibbles worth of time passed
      // (subtract 1 because readNibble will advance by 1)
      uint64_t extra_nibbles = (elapsed / CYCLES_PER_NIBBLE);
      if (extra_nibbles > 1) {
        // Advance disk position for elapsed time (readNibble will add 1 more)
        // Use advanceBitPosition to handle the catch-up efficiently
        // We simulate the disk having rotated during CPU processing time
        for (uint64_t i = 1; i < extra_nibbles && i < 50; i++) {
          disk->readNibble(); // Advance disk position
        }
      }
    }

    // Read new nibble from disk (this also advances by 1)
    data_latch_ = disk->readNibble();
    last_cycle = current_cycle;
    latch_valid_ = true; // This nibble hasn't been read yet
  }

  // Return the nibble. If this is a repeat read before next nibble is ready,
  // clear bit 7 so the ROM's BPL loop will wait.
  if (latch_valid_) {
    latch_valid_ = false; // Mark as read
    return data_latch_;   // Return with bit 7 set
  } else {
    // Already read this nibble, return with bit 7 clear
    return data_latch_ & 0x7F;
  }
}

void Disk2Controller::writeDiskData() {
  // If motor is off, no disk, or not in write mode, do nothing
  if (!isMotorOn() || !hasDisk(selected_drive_) || !q7_) {
    return;
  }

  DiskImage *disk = disk_images_[selected_drive_].get();

  // Check write protection
  if (disk->isWriteProtected()) {
    return;
  }

  // Check if current head position has data
  if (!disk->hasData()) {
    return;
  }

  // Write the nibble
  disk->writeNibble(write_latch_);

  // Update timing to stay in sync with disk rotation
  last_read_cycle_[selected_drive_] = getCycles();
}

void Disk2Controller::update(int cycles) { total_cycles_ += cycles; }

const uint8_t *Disk2Controller::getDiskData(int drive, size_t *size) const {
  if (drive < 0 || drive > 1 || !hasDisk(drive)) {
    *size = 0;
    return nullptr;
  }

  return disk_images_[drive]->getSectorData(size);
}

const uint8_t *Disk2Controller::exportDiskData(int drive, size_t *size) {
  if (drive < 0 || drive > 1 || !hasDisk(drive)) {
    *size = 0;
    return nullptr;
  }

  return disk_images_[drive]->exportData(size);
}

} // namespace a2e
