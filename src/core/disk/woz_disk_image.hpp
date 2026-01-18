#pragma once

#include "disk_image.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace a2e {

/**
 * WozDiskImage - WOZ format disk image implementation
 *
 * WOZ is a bit-accurate disk image format that captures the exact
 * magnetic flux transitions on a 5.25" floppy disk. It supports:
 *
 * - Quarter-track positioning (160 positions for 40 tracks)
 * - Variable track lengths (accurate timing)
 * - Copy-protected disk preservation
 * - Write protection status
 *
 * File format reference: https://applesaucefdc.com/woz/
 *
 * This implementation supports both WOZ 1.0 and WOZ 2.0 formats.
 */
class WozDiskImage : public DiskImage {
public:
  WozDiskImage();
  ~WozDiskImage() override = default;

  // Move operations
  WozDiskImage(WozDiskImage &&) = default;
  WozDiskImage &operator=(WozDiskImage &&) = default;

  // ===== DiskImage interface implementation =====

  bool load(const uint8_t *data, size_t size,
            const std::string &filename) override;
  bool isLoaded() const override;
  Format getFormat() const override;

  // Head positioning
  void setPhase(int phase, bool on) override;
  int getQuarterTrack() const override;
  int getTrack() const override;

  // Geometry
  int getTrackCount() const override;
  bool hasData() const override;

  // Data access
  void advanceBitPosition(uint64_t current_cycles) override;
  uint8_t readNibble() override;

  // Write operations
  void writeNibble(uint8_t nibble) override;

  bool isWriteProtected() const override;
  bool isModified() const override { return modified_; }
  std::string getFormatName() const override;
  const uint8_t *getSectorData(size_t *size) const override;
  const uint8_t *exportData(size_t *size) override;

  // ===== Debug Methods =====
  uint8_t getNibbleAt(int track, int position) const override;
  int getTrackNibbleCount(int track) const override;
  size_t getCurrentNibblePosition() const override { return bit_position_ / 8; }

  // ===== WOZ-specific methods =====

  /**
   * Create a blank, unformatted WOZ2 disk
   * The disk will have 35 empty tracks filled with sync bytes
   */
  void createBlank();

  /**
   * Get the disk type from INFO chunk
   * @return 1 = 5.25", 2 = 3.5"
   */
  uint8_t getDiskType() const;

  /**
   * Get disk type as a human-readable string
   * @return "5.25\"" or "3.5\""
   */
  std::string getDiskTypeString() const;

  /**
   * Get the optimal bit timing from INFO chunk
   * @return Bit timing in 125ns units (default 32 = 4us)
   */
  uint8_t getOptimalBitTiming() const;

private:
  // WOZ file signature constants
  static constexpr uint32_t WOZ1_SIGNATURE = 0x315A4F57; // "WOZ1"
  static constexpr uint32_t WOZ2_SIGNATURE = 0x325A4F57; // "WOZ2"
  static constexpr uint32_t INFO_CHUNK_ID = 0x4F464E49;  // "INFO"
  static constexpr uint32_t TMAP_CHUNK_ID = 0x50414D54;  // "TMAP"
  static constexpr uint32_t TRKS_CHUNK_ID = 0x534B5254;  // "TRKS"

  // Quarter-track mapping
  static constexpr int QUARTER_TRACK_COUNT = 160;
  static constexpr uint8_t NO_TRACK = 0xFF;

  // WOZ2 track storage
  static constexpr size_t WOZ2_TRACK_BLOCK_SIZE = 512;

#pragma pack(push, 1)
  /**
   * WOZ file header (12 bytes)
   */
  struct WozHeader {
    uint32_t signature; // "WOZ1" or "WOZ2"
    uint8_t high_bits;  // 0xFF
    uint8_t lfcrlf[3];  // 0x0A 0x0D 0x0A
    uint32_t crc32;     // CRC32 of all data after this field
  };

  /**
   * Chunk header (8 bytes)
   */
  struct ChunkHeader {
    uint32_t chunk_id; // 4-character chunk identifier
    uint32_t size;     // Size of chunk data (not including header)
  };

  /**
   * INFO chunk data (60 bytes in WOZ2)
   */
  struct InfoChunk {
    uint8_t version;              // INFO chunk version (1 or 2)
    uint8_t disk_type;            // 1 = 5.25", 2 = 3.5"
    uint8_t write_protected;      // 1 = write protected
    uint8_t synchronized;         // 1 = tracks are synchronized
    uint8_t cleaned;              // 1 = MC3470 fake bits removed
    char creator[32];             // Creator software name
    uint8_t disk_sides;           // 1 or 2
    uint8_t boot_sector_format;   // 0=unknown, 1=16-sector, 2=13-sector, 3=both
    uint8_t optimal_bit_timing;   // 125ns units (default 32 = 4us)
    uint16_t compatible_hardware; // Bit field of compatible hardware
    uint16_t required_ram;        // Minimum RAM in KB
    uint16_t largest_track;       // Block count of largest track
    uint8_t reserved[10];         // Padding to 60 bytes
  };

  /**
   * WOZ2 TRKS chunk entry (8 bytes per track)
   */
  struct Woz2TrackEntry {
    uint16_t starting_block; // Starting 512-byte block
    uint16_t block_count;    // Number of 512-byte blocks
    uint32_t bit_count;      // Number of valid bits in track
  };
#pragma pack(pop)

  /**
   * Internal track data storage
   */
  struct TrackData {
    std::vector<uint8_t> bits; // Raw bit data
    uint32_t bit_count = 0;    // Number of valid bits
    bool valid = false;        // Track has data
  };

  // Loaded file info
  Format format_ = Format::Unknown;
  bool loaded_ = false;
  bool modified_ = false;

  // Export buffer for saving
  mutable std::vector<uint8_t> export_buffer_;

  // INFO chunk data
  InfoChunk info_{};

  // TMAP: quarter-track to track index mapping
  std::array<uint8_t, QUARTER_TRACK_COUNT> tmap_{};

  // Track data storage (indexed by TMAP values, not quarter-track)
  std::vector<TrackData> tracks_;

  // ===== Head positioning state =====
  uint8_t phase_states_ = 0; // Bit field for phase magnet states (bits 0-3)
  int quarter_track_ = 0;    // Current head position (0-159)
  int last_phase_ = 0;       // Last activated phase for step direction

  // ===== Bit position =====
  uint32_t bit_position_ = 0; // Current bit position within track

  // Cycle count for disk rotation timing
  uint64_t last_cycle_count_ = 0;

  // ===== Internal methods =====

  /**
   * Reset all state to unloaded
   */
  void reset();

  /**
   * Parse INFO chunk
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseInfoChunk(const uint8_t *data, uint32_t size);

  /**
   * Parse TMAP chunk
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseTmapChunk(const uint8_t *data, uint32_t size);

  /**
   * Parse TRKS chunk for WOZ1 format
   * @param data Chunk data
   * @param size Chunk size
   * @return true on success
   */
  bool parseTrksChunkWoz1(const uint8_t *data, uint32_t size);

  /**
   * Parse TRKS chunk for WOZ2 format
   * @param file_data Full file data
   * @param file_size File size
   * @param trks_data TRKS chunk data
   * @param trks_size TRKS chunk size
   * @return true on success
   */
  bool parseTrksChunkWoz2(const uint8_t *file_data, size_t file_size,
                          const uint8_t *trks_data, uint32_t trks_size);

  /**
   * Get track data at current head position
   * @return Pointer to track data, or nullptr if no data
   */
  const TrackData *getCurrentTrackData() const;

  /**
   * Read a raw bit from the disk at current position (internal helper)
   * @return 0 or 1
   */
  uint8_t readBitInternal() const;

  /**
   * Update head position based on newly activated phase
   * Uses 4-phase stepper motor physics
   * @param phase The phase that was just activated (0-3)
   */
  void updateHeadPosition(int phase);

  /**
   * Get mutable track data at current head position
   * @return Pointer to track data, or nullptr if no data
   */
  TrackData *getMutableCurrentTrackData();

  /**
   * Write a single bit to the disk at current position
   * @param bit The bit value (0 or 1)
   */
  void writeBitInternal(uint8_t bit);
};

} // namespace a2e
