#pragma once

#include "disk_image.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace a2e {

// Callback type for getting current CPU cycle count
using CycleCallback = std::function<uint64_t()>;

/**
 * Disk2Controller - Disk II Controller Card for Slot 6
 *
 * The Disk II controller handles floppy disk I/O for the Apple IIe.
 * It occupies:
 * - I/O space: $C0E0-$C0EF (16 soft switches)
 * - Slot ROM: $C600-$C6FF (256 bytes, P5 ROM 341-0027)
 *
 * Soft switch addresses (offset from slot 6 base $C0E0):
 * $C0E0 - Phase 0 off     $C0E1 - Phase 0 on
 * $C0E2 - Phase 1 off     $C0E3 - Phase 1 on
 * $C0E4 - Phase 2 off     $C0E5 - Phase 2 on
 * $C0E6 - Phase 3 off     $C0E7 - Phase 3 on
 * $C0E8 - Motor off       $C0E9 - Motor on
 * $C0EA - Drive 1 select  $C0EB - Drive 2 select
 * $C0EC - Q6L (read)      $C0ED - Q6H (WP sense/write load)
 * $C0EE - Q7L (read mode) $C0EF - Q7H (write mode)
 */
class Disk2Controller {
public:
  Disk2Controller();
  ~Disk2Controller() = default;

  // Delete copy constructor and assignment (non-copyable)
  Disk2Controller(const Disk2Controller &) = delete;
  Disk2Controller &operator=(const Disk2Controller &) = delete;

  // Allow move
  Disk2Controller(Disk2Controller &&) = default;
  Disk2Controller &operator=(Disk2Controller &&) = default;

  /**
   * Reset the controller to power-on state
   */
  void reset();

  /**
   * Read a byte from the controller I/O space ($C0E0-$C0EF)
   * @param reg Register offset (0-15)
   * @return byte value
   */
  uint8_t read(uint8_t reg);

  /**
   * Write a byte to the controller I/O space ($C0E0-$C0EF)
   * @param reg Register offset (0-15)
   * @param value byte value
   */
  void write(uint8_t reg, uint8_t value);

  /**
   * Update the controller state (call once per CPU cycle)
   * @param cycles Number of cycles elapsed
   */
  void update(int cycles);

  /**
   * Set the callback for getting current CPU cycle count
   * This is used for accurate disk timing during reads
   * @param callback Function that returns current CPU cycle count
   */
  void setCycleCallback(CycleCallback callback) { cycle_callback_ = callback; }

  // ===== Disk operations =====

  /**
   * Insert a disk image into a drive
   * @param drive Drive number (0 or 1)
   * @param data Pointer to disk image data
   * @param size Size of the data
   * @param filename Original filename (for format detection)
   * @return true on success
   */
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const std::string &filename);

  /**
   * Eject disk from a drive
   * @param drive Drive number (0 or 1)
   */
  void ejectDisk(int drive);

  /**
   * Check if a drive has a disk inserted
   * @param drive Drive number (0 or 1)
   * @return true if disk is inserted
   */
  bool hasDisk(int drive) const;

  /**
   * Get the disk data for saving (DSK format)
   * @param drive Drive number (0 or 1)
   * @param size Output: size of the data
   * @return Pointer to disk data, or nullptr if no disk
   */
  const uint8_t *getDiskData(int drive, size_t *size) const;

  /**
   * Get the disk image for a drive (for UI display)
   * @param drive Drive number (0 or 1)
   * @return Pointer to disk image, or nullptr if no disk
   */
  const DiskImage *getDiskImage(int drive) const;

  /**
   * Check if motor is currently on
   * @return true if motor is running
   */
  bool isMotorOn() const;

  /**
   * Get currently selected drive (0 or 1)
   * @return Selected drive number
   */
  int getSelectedDrive() const { return selected_drive_; }

  /**
   * Get the current phase magnet states
   * @return Bit field where bit 0-3 represent phases 0-3 (1 = on, 0 = off)
   */
  uint8_t getPhaseStates() const { return phase_states_; }

  /**
   * Get the current track position from the selected drive's disk image
   * @return Track number (0-34), or -1 if no disk
   */
  int getCurrentTrack() const;

  /**
   * Get the current quarter-track position from the selected drive's disk image
   * @return Quarter-track number (0-139), or -1 if no disk
   */
  int getQuarterTrack() const;

  /**
   * Get Q6 latch state
   * @return true if Q6 is high, false if low
   */
  bool getQ6() const { return q6_; }

  /**
   * Get Q7 latch state
   * @return true if Q7 is high (write mode), false if low (read mode)
   */
  bool getQ7() const { return q7_; }

  /**
   * Get the data latch value (last nibble read/written)
   * @return Current data latch value
   */
  uint8_t getDataLatch() const { return data_latch_; }

private:
  // Soft switch offsets
  static constexpr uint8_t PHASE0_OFF = 0x00;
  static constexpr uint8_t PHASE0_ON = 0x01;
  static constexpr uint8_t PHASE1_OFF = 0x02;
  static constexpr uint8_t PHASE1_ON = 0x03;
  static constexpr uint8_t PHASE2_OFF = 0x04;
  static constexpr uint8_t PHASE2_ON = 0x05;
  static constexpr uint8_t PHASE3_OFF = 0x06;
  static constexpr uint8_t PHASE3_ON = 0x07;
  static constexpr uint8_t MOTOR_OFF = 0x08;
  static constexpr uint8_t MOTOR_ON = 0x09;
  static constexpr uint8_t DRIVE1_SELECT = 0x0A;
  static constexpr uint8_t DRIVE2_SELECT = 0x0B;
  static constexpr uint8_t Q6L = 0x0C; // Read data / shift
  static constexpr uint8_t Q6H = 0x0D; // Write protect sense / write load
  static constexpr uint8_t Q7L = 0x0E; // Read mode
  static constexpr uint8_t Q7H = 0x0F; // Write mode

  // Controller state
  mutable bool motor_on_ = false;      // mutable for lazy timeout evaluation
  mutable uint64_t motor_off_cycle_ = 0; // Cycle when motor-off was requested
  int selected_drive_ = 0;             // 0 or 1
  bool q6_ = false;                    // Q6 latch state
  bool q7_ = false;                    // Q7 latch state (false=read, true=write)
  uint8_t phase_states_ = 0;           // Bit field for phase magnet states

  // Motor timeout: ~1 second at 1.023 MHz
  static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;

  // Nibble timing: ~32 cycles per nibble
  static constexpr uint64_t CYCLES_PER_NIBBLE = 32;

  // Total cycle count for timing (fallback if no callback)
  uint64_t total_cycles_ = 0;

  // Callback for getting current CPU cycle count
  CycleCallback cycle_callback_;

  /**
   * Get current cycle count, using callback if available
   */
  uint64_t getCycles() const {
    if (cycle_callback_) {
      return cycle_callback_();
    }
    return total_cycles_;
  }

  // Disk images for each drive (polymorphic - can be DSK or WOZ)
  std::unique_ptr<DiskImage> disk_images_[2];

  // Per-drive timing state
  uint64_t last_read_cycle_[2] = {0, 0};

  // Data latch (shift register)
  uint8_t data_latch_ = 0;
  bool latch_valid_ = false; // True until first read of current nibble

  // Write state
  uint8_t write_latch_ = 0;

  /**
   * Read a byte from the current disk
   * Updates disk timing and returns the data latch value
   * @return The data latch value
   */
  uint8_t readDiskData();

  /**
   * Write a byte to the current disk
   * Handles write timing and commits the nibble to disk
   */
  void writeDiskData();

  /**
   * Handle soft switch access
   * @param offset Offset (0x00-0x0F)
   * @param is_write true if write access, false if read
   * @return byte value for reads
   */
  uint8_t handleSoftSwitch(uint8_t offset, bool is_write);
};

} // namespace a2e
