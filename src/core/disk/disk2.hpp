#pragma once

#include "../types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace a2e {

// Nibblized track data
struct NibbleTrack {
  std::vector<uint8_t> nibbles;
  bool valid = false;
  bool dirty = false;
};

// Disk image
struct DiskImage {
  // Raw sector data (143360 bytes for DSK/PO)
  std::vector<uint8_t> sectorData;

  // Nibblized tracks (generated on demand)
  std::array<NibbleTrack, 35> nibbleTracks;

  bool writeProtected = false;
  bool modified = false;
  bool loaded = false;
  std::string filename;

  // Format info
  enum class Format {
    NONE,
    DSK, // DOS order
    PO,  // ProDOS order
    WOZ  // WOZ format
  };
  Format format = Format::NONE;

  // Volume number for nibblization
  uint8_t volumeNumber = 254;

  // Head position state (managed per-disk like reference)
  int quarterTrack = 0;      // 0-139 for 35 tracks
  uint8_t phaseStates = 0;   // Bitmask of active phases
  int lastPhase = 0;         // Last activated phase
  size_t nibblePosition = 0; // Position within current track

  // For WOZ format
  std::vector<std::vector<uint8_t>> wozTracks; // Raw WOZ track data
  std::vector<uint32_t> wozBitCounts;          // Bit count per track
  size_t bitPosition = 0;                      // Bit position for WOZ
};

class Disk2Controller {
public:
  static constexpr int NUM_DRIVES = 2;
  static constexpr int NUM_TRACKS = 35;
  static constexpr int TRACK_SIZE_NIBBLES = 6656;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr int DISK_SIZE =
      NUM_TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;

  // Timing constants
  static constexpr uint64_t CYCLES_PER_NIBBLE = 32;
  static constexpr uint64_t MOTOR_OFF_DELAY_CYCLES = 1023000;

  Disk2Controller();

  // I/O access (addresses 0-15 correspond to $C0E0-$C0EF)
  uint8_t read(uint8_t reg);
  void write(uint8_t reg, uint8_t value);

  // Disk management
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const std::string &filename);
  void ejectDisk(int drive);
  bool isDiskInserted(int drive) const;
  bool isDiskModified(int drive) const;
  const uint8_t *getDiskData(int drive, size_t *size) const;

  // State access - returns synchronized state
  DriveState getDriveState(int drive) const;
  int getSelectedDrive() const { return selectedDrive_; }
  uint8_t getDataLatch() const { return dataLatch_; }
  uint8_t getPhaseStates() const { return phaseStates_; }

  // Cycle-accurate update
  void update(int cycles);

  // Set cycle count callback (like reference)
  void setCycleCount(uint64_t cycles) { currentCycle_ = cycles; }
  uint64_t getCycles() const { return currentCycle_; }

  // Reset
  void reset();

private:
  // Phase control - forwarded to disk image
  void setPhase(int drive, int phase, bool on);
  void updateHeadPosition(DiskImage &disk, int phase);

  // Ensure track is nibblized (lazy nibblization like reference)
  void ensureTrackNibblized(int drive, int track);

  // Nibblization
  void nibblizeTrack(DiskImage &disk, int track);

  // Data access
  uint8_t readDiskData();
  void writeDiskData(uint8_t value);
  uint8_t readNibble(int drive);
  void writeNibble(int drive, uint8_t value);

  // Disk format loading
  bool loadDSK(int drive, const uint8_t *data, size_t size, bool proDosOrder);
  bool loadWOZ(int drive, const uint8_t *data, size_t size);

  // Format detection
  static bool detectProDOS(const uint8_t *data, size_t size);
  static bool detectDOS33(const uint8_t *data, size_t size);

  // 6-and-2 encoding
  void encode62(const uint8_t *data, uint8_t *encoded);

  // Drive state (minimal - just motor and write mode)
  std::array<DriveState, NUM_DRIVES> drives_{};

  // Disk images (contain their own head position state)
  std::array<DiskImage, NUM_DRIVES> disks_{};

  // Controller state
  int selectedDrive_ = 0;
  uint8_t phaseStates_ = 0; // Controller's view of phase states

  // Q6/Q7 latches
  bool q6_ = false;
  bool q7_ = false;

  // Data latch
  uint8_t dataLatch_ = 0;
  bool latchValid_ = false;

  // Motor timing
  bool motorOn_ = false;
  uint64_t motorOffCycle_ = 0;

  // Cycle timing
  uint64_t currentCycle_ = 0;
  std::array<uint64_t, NUM_DRIVES> lastReadCycle_{};

  // Check if motor is actually on (accounts for delay)
  bool isMotorOn() const;

  // GCR translation table
  static constexpr std::array<uint8_t, 64> GCR_ENCODE_TABLE = {
      {0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC,
       0xAD, 0xAE, 0xAF, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA,
       0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3, 0xD6,
       0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7,
       0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5,
       0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF}};

  // DOS 3.3 sector interleave: physical sector -> logical sector
  static constexpr std::array<int, 16> DOS_PHYSICAL_TO_LOGICAL = {
      {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15}};

  // ProDOS sector interleave: physical sector -> logical sector
  static constexpr std::array<int, 16> PRODOS_PHYSICAL_TO_LOGICAL = {
      {0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15}};
};

} // namespace a2e
