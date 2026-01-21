#pragma once

#include "disk_image.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace a2e {

/**
 * DskDiskImage - DSK/DO/PO disk image format support
 *
 * DSK is a raw sector image format commonly used for Apple II disk images.
 * It stores 35 tracks x 16 sectors x 256 bytes = 143,360 bytes (140KB).
 *
 * There are two sector ordering schemes:
 * - DOS order (.dsk, .do): Sectors stored in DOS 3.3 logical order
 * - ProDOS order (.po): Sectors stored in ProDOS physical order
 *
 * This class converts the raw sector data to/from GCR-encoded nibbles
 * on-the-fly, simulating how a real Disk II controller reads/writes.
 */
class DskDiskImage : public DiskImage {
public:
  // Disk geometry constants
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int TRACK_SIZE =
      SECTORS_PER_TRACK * BYTES_PER_SECTOR; // 4096 bytes
  static constexpr int DISK_SIZE = TRACKS * TRACK_SIZE; // 143360 bytes

  // Nibble track size (approximate, varies slightly per track)
  static constexpr int NIBBLES_PER_TRACK = 6656;

  DskDiskImage();
  ~DskDiskImage() override = default;

  // ===== Loading =====
  bool load(const uint8_t *data, size_t size,
            const std::string &filename) override;
  bool isLoaded() const override { return loaded_; }
  Format getFormat() const override { return format_; }

  // ===== Head Positioning =====
  void setPhase(int phase, bool on) override;
  int getQuarterTrack() const override { return quarter_track_; }
  int getTrack() const override { return quarter_track_ / 4; }

  // ===== Geometry =====
  int getTrackCount() const override { return TRACKS; }
  bool hasData() const override;

  // ===== Data Access =====
  void advanceBitPosition(uint64_t current_cycles) override;
  uint8_t readNibble() override;

  // ===== Write Operations =====
  void writeNibble(uint8_t nibble) override;

  // ===== Status =====
  bool isWriteProtected() const override { return write_protected_; }
  bool isModified() const override { return modified_; }
  std::string getFormatName() const override;
  const uint8_t *getSectorData(size_t *size) const override;
  const uint8_t *exportData(size_t *size) override;

  // ===== Debug Methods =====
  uint8_t getNibbleAt(int track, int position) const override;
  int getTrackNibbleCount(int track) const override;
  size_t getCurrentNibblePosition() const override { return nibble_position_; }

  // ===== DSK-specific =====

  /**
   * Check if this is a ProDOS-order image
   */
  bool isProDOSOrder() const { return format_ == Format::PO; }

  /**
   * Get the volume number (default 254)
   */
  uint8_t getVolumeNumber() const { return volume_number_; }

  /**
   * Set the volume number for address field encoding
   */
  void setVolumeNumber(uint8_t volume) { volume_number_ = volume; }

private:
  // Raw sector data storage
  std::array<uint8_t, DISK_SIZE> sector_data_{};

  // Nibblized track cache
  struct NibbleTrack {
    std::vector<uint8_t> nibbles;
    bool dirty = false; // Track has been modified
    bool valid = false; // Track has been nibblized
  };
  std::array<NibbleTrack, TRACKS> nibble_tracks_{};

  // State
  Format format_ = Format::Unknown;
  bool loaded_ = false;
  bool write_protected_ = false;
  bool modified_ = false;
  uint8_t volume_number_ = 254;

  // Head position (0-139 quarter-tracks)
  int quarter_track_ = 0;
  uint8_t phase_states_ = 0; // Bit field of active phases
  int current_phase_ = 0; // Current phase where head is settled (for stepper)

  // Nibble position within current track
  size_t nibble_position_ = 0;

  // Cycle count for disk rotation timing
  uint64_t last_cycle_count_ = 0;

  // ===== Internal Methods =====

  /**
   * Detect disk format from content (ProDOS vs DOS order)
   * Checks for filesystem signatures rather than relying on file extension
   */
  Format detectFormat(const std::string &filename) const;

  /**
   * Nibblize a track from sector data
   */
  void nibblizeTrack(int track);

  /**
   * Denibblize a track back to sector data
   */
  void denibblizeTrack(int track);

  /**
   * Update head position based on phase magnet states
   * Called when a phase is turned OFF to check if stepping should occur
   */
  void updateHeadPosition();

  /**
   * Get the logical sector number for a physical sector
   */
  int getLogicalSector(int physical_sector) const;

  /**
   * Ensure the current track is nibblized
   */
  void ensureTrackNibblized();

  /**
   * Decode a 4-and-4 encoded byte pair
   */
  static uint8_t decode4and4(uint8_t odd, uint8_t even);

  /**
   * Decode 6-and-2 encoded data back to 256 bytes
   */
  static bool decode6and2(const uint8_t *encoded, uint8_t *output);
};

} // namespace a2e
