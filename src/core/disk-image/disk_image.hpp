/*
 * disk_image.hpp - Abstract base class for disk image formats
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include <cstdint>
#include <string>

namespace a2e {

/**
 * DiskImage - Abstract base class for disk image formats
 *
 * This interface defines the contract for disk image implementations.
 * The disk controller uses this interface to access disk data without
 * needing to know the specific format details.
 *
 * The disk image manages head positioning internally based on stepper
 * motor phase changes. The controller only knows about phases (0-3),
 * not tracks. The disk image translates phase sequences into head
 * movement using 4-phase stepper motor physics.
 *
 * Data access is nibble-based. The controller reads nibbles sequentially
 * as the virtual disk spins. Each track contains a stream of nibbles
 * that encode the sector data using GCR (Group Coded Recording).
 */
class DiskImage {
public:
  /**
   * Disk image format types
   */
  enum class Format {
    Unknown,
    WOZ1,
    WOZ2,
    DSK, // Raw sector format (140KB, 35 tracks x 16 sectors x 256 bytes)
    DO,  // DOS-order DSK (same as DSK)
    PO   // ProDOS-order DSK
  };

  virtual ~DiskImage() = default;

  // Non-copyable
  DiskImage(const DiskImage &) = delete;
  DiskImage &operator=(const DiskImage &) = delete;

  // Movable
  DiskImage(DiskImage &&) = default;
  DiskImage &operator=(DiskImage &&) = default;

  // ===== Loading =====

  /**
   * Load a disk image from raw data
   * @param data Pointer to disk image data
   * @param size Size of the data
   * @param filename Original filename (for format detection)
   * @return true on success, false on failure
   */
  virtual bool load(const uint8_t *data, size_t size,
                    const std::string &filename) = 0;

  /**
   * Check if a disk image is currently loaded
   * @return true if a valid disk image is loaded
   */
  virtual bool isLoaded() const = 0;

  /**
   * Get the format of the loaded disk image
   * @return Format enum value
   */
  virtual Format getFormat() const = 0;

  // ===== Head Positioning =====

  /**
   * Notify the disk of a phase magnet state change
   * The disk image tracks phase states and moves the head accordingly
   * using 4-phase stepper motor physics.
   *
   * @param phase Phase number (0-3)
   * @param on true if phase is being activated, false if deactivated
   */
  virtual void setPhase(int phase, bool on) = 0;

  /**
   * Get the current quarter-track position (0-139)
   * This is for display/debugging purposes only.
   * @return Current quarter-track position
   */
  virtual int getQuarterTrack() const = 0;

  /**
   * Get the current track position (0-34)
   * This is for display/debugging purposes only.
   * @return Current track number (quarter_track / 4)
   */
  virtual int getTrack() const = 0;

  /**
   * Set the quarter-track position directly (for state restoration)
   * @param quarter_track Quarter-track position (0-139)
   */
  virtual void setQuarterTrack(int quarter_track) = 0;

  // ===== Geometry =====

  /**
   * Get the number of tracks on the disk
   * Standard 5.25" disks have 35 tracks (0-34)
   * @return Number of tracks
   */
  virtual int getTrackCount() const = 0;

  /**
   * Check if current head position has data
   * @return true if the current position has data
   */
  virtual bool hasData() const = 0;

  // ===== Data Access =====

  /**
   * Advance the bit position based on elapsed CPU cycles
   * This simulates the disk rotating while the motor is on.
   * Call this before reading to account for disk rotation during
   * the time since the last read.
   *
   * @param current_cycles Current total CPU cycle count
   */
  virtual void advanceBitPosition(uint64_t current_cycles) = 0;

  /**
   * Read a nibble from the disk at the current position
   * This simulates the disk read head reading data.
   * The bit position advances as bits are read until a complete
   * nibble (byte with high bit set) is assembled.
   *
   * @return The nibble read (high bit set for valid data)
   */
  virtual uint8_t readNibble() = 0;

  // ===== Write Operations =====

  /**
   * Write a nibble to the disk at the current position
   * This simulates the disk write head writing data.
   * The bit position advances as bits are written.
   *
   * @param nibble The nibble to write (should have high bit set for valid GCR
   * data)
   */
  virtual void writeNibble(uint8_t nibble) = 0;

  // ===== Status =====

  /**
   * Check if the disk is write-protected
   * @return true if write-protected
   */
  virtual bool isWriteProtected() const = 0;

  /**
   * Check if the disk has been modified
   * @return true if modified since load
   */
  virtual bool isModified() const = 0;

  /**
   * Get a human-readable format name
   * @return Format name string (e.g., "WOZ 2.0")
   */
  virtual std::string getFormatName() const = 0;

  /**
   * Get the raw sector data for saving (DSK format)
   * @param size Output: size of the data
   * @return Pointer to sector data, or nullptr if not available
   */
  virtual const uint8_t *getSectorData(size_t *size) const = 0;

  /**
   * Export disk data in its native format for saving
   * This reconstructs the disk image file that can be saved to disk.
   * The returned pointer is valid until the next call to exportData()
   * or until the disk image is destroyed.
   *
   * @param size Output: size of the exported data
   * @return Pointer to exported data, or nullptr if export not supported
   */
  virtual const uint8_t *exportData(size_t *size) = 0;

  // ===== Debug Methods =====

  /**
   * Get a nibble at a specific track and position (debug only)
   * @param track Track number (0-34)
   * @param position Position in nibble stream
   * @return Nibble value at that position
   */
  virtual uint8_t getNibbleAt(int track, int position) const = 0;

  /**
   * Get the number of nibbles in a track (debug only)
   * @param track Track number (0-34)
   * @return Number of nibbles in the track
   */
  virtual int getTrackNibbleCount(int track) const = 0;

  /**
   * Get the current nibble position within the track (debug only)
   * @return Current nibble position
   */
  virtual size_t getCurrentNibblePosition() const = 0;

  // ===== Filename Tracking =====

  /**
   * Get the filename associated with this disk image
   * @return Filename string
   */
  const std::string &getFilename() const { return filename_; }

  /**
   * Set the filename associated with this disk image
   * @param filename The filename to set
   */
  void setFilename(const std::string &filename) { filename_ = filename; }

protected:
  DiskImage() = default;
  std::string filename_;
};

} // namespace a2e
