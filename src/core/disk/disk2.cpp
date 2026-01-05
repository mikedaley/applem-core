#include "disk2.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Disk2Controller::Disk2Controller() { reset(); }

void Disk2Controller::reset() {
  for (auto &drive : drives_) {
    drive = DriveState{};
  }
  for (auto &disk : disks_) {
    disk = DiskImage{};
  }

  selectedDrive_ = 0;
  phaseStates_ = 0;
  q6_ = false;
  q7_ = false;
  dataLatch_ = 0;
  latchValid_ = false;
  motorOn_ = false;
  motorOffCycle_ = 0;
  currentCycle_ = 0;
  lastReadCycle_.fill(0);
}

bool Disk2Controller::isMotorOn() const {
  if (!motorOn_)
    return false;

  // Check if motor-off delay has elapsed
  if (motorOffCycle_ != 0) {
    if (currentCycle_ >= motorOffCycle_ + MOTOR_OFF_DELAY_CYCLES) {
      const_cast<Disk2Controller *>(this)->motorOn_ = false;
      const_cast<Disk2Controller *>(this)->motorOffCycle_ = 0;
      return false;
    }
  }
  return true;
}

uint8_t Disk2Controller::read(uint8_t reg) {
  switch (reg) {
  // Phase control - exactly like reference
  case 0x00:
    phaseStates_ &= ~0x01;
    setPhase(selectedDrive_, 0, false);
    break;
  case 0x01:
    phaseStates_ |= 0x01;
    setPhase(selectedDrive_, 0, true);
    break;
  case 0x02:
    phaseStates_ &= ~0x02;
    setPhase(selectedDrive_, 1, false);
    break;
  case 0x03:
    phaseStates_ |= 0x02;
    setPhase(selectedDrive_, 1, true);
    break;
  case 0x04:
    phaseStates_ &= ~0x04;
    setPhase(selectedDrive_, 2, false);
    break;
  case 0x05:
    phaseStates_ |= 0x04;
    setPhase(selectedDrive_, 2, true);
    break;
  case 0x06:
    phaseStates_ &= ~0x08;
    setPhase(selectedDrive_, 3, false);
    break;
  case 0x07:
    phaseStates_ |= 0x08;
    setPhase(selectedDrive_, 3, true);
    break;

  case 0x08: // Motor off
    if (motorOn_ && motorOffCycle_ == 0) {
      motorOffCycle_ = currentCycle_;
    }
    break;

  case 0x09: // Motor on
    motorOffCycle_ = 0;
    motorOn_ = true;
    break;

  case 0x0A: // Select drive 1
    selectedDrive_ = 0;
    break;

  case 0x0B: // Select drive 2
    selectedDrive_ = 1;
    break;

  case 0x0C: // Q6L
    q6_ = false;
    if (!q7_) {
      return readDiskData();
    }
    break;

  case 0x0D: // Q6H
    q6_ = true;
    if (!q7_) {
      // Return write protect status
      if (isDiskInserted(selectedDrive_)) {
        return disks_[selectedDrive_].writeProtected ? 0x80 : 0x00;
      }
      return 0x80; // No disk = write protected
    }
    break;

  case 0x0E: // Q7L - Read mode
    q7_ = false;
    break;

  case 0x0F: // Q7H - Write mode
    q7_ = true;
    break;
  }

  return 0x00;
}

void Disk2Controller::write(uint8_t reg, uint8_t value) {
  // Handle the soft switch first
  read(reg);

  // Handle data write when in write mode
  if (q7_ && q6_ && (reg == 0x0D)) {
    writeDiskData(value);
  }
}

void Disk2Controller::setPhase(int drive, int phase, bool on) {
  if (drive < 0 || drive >= NUM_DRIVES)
    return;
  if (!isDiskInserted(drive))
    return;

  DiskImage &disk = disks_[drive];

  if (phase < 0 || phase > 3)
    return;

  uint8_t phaseBit = 1 << phase;

  if (on) {
    disk.phaseStates |= phaseBit;
    updateHeadPosition(disk, phase);
  } else {
    disk.phaseStates &= ~phaseBit;
    // Turning off a phase doesn't cause stepping
  }
}

void Disk2Controller::updateHeadPosition(DiskImage &disk, int phase) {
  // The Disk II uses a 4-phase stepper motor
  // Each adjacent phase change moves the head by 2 quarter-tracks

  int phaseDiff = phase - disk.lastPhase;

  // Normalize to handle wrap-around
  if (phaseDiff == 3)
    phaseDiff = -1;
  if (phaseDiff == -3)
    phaseDiff = 1;

  constexpr int MAX_QUARTER_TRACK = (NUM_TRACKS * 4) - 1; // 139

  if (phaseDiff == 1) {
    // Stepping inward (toward higher track numbers)
    if (disk.quarterTrack < MAX_QUARTER_TRACK - 1) {
      disk.quarterTrack += 2;
    } else if (disk.quarterTrack < MAX_QUARTER_TRACK) {
      disk.quarterTrack = MAX_QUARTER_TRACK;
    }
  } else if (phaseDiff == -1) {
    // Stepping outward (toward track 0)
    if (disk.quarterTrack > 1) {
      disk.quarterTrack -= 2;
    } else if (disk.quarterTrack > 0) {
      disk.quarterTrack = 0;
    }
  }
  // phaseDiff of 0, 2, or -2 doesn't move the head

  disk.lastPhase = phase;
}

void Disk2Controller::ensureTrackNibblized(int drive, int track) {
  if (drive < 0 || drive >= NUM_DRIVES)
    return;
  if (track < 0 || track >= NUM_TRACKS)
    return;

  DiskImage &disk = disks_[drive];
  NibbleTrack &nt = disk.nibbleTracks[track];

  if (nt.valid)
    return; // Already nibblized

  nibblizeTrack(disk, track);
  nt.valid = true;
}

uint8_t Disk2Controller::readNibble(int drive) {
  if (drive < 0 || drive >= NUM_DRIVES)
    return 0xFF;

  DiskImage &disk = disks_[drive];
  if (!disk.loaded)
    return 0xFF;

  int track = disk.quarterTrack / 4;
  if (track < 0 || track >= NUM_TRACKS)
    return 0xFF;

  ensureTrackNibblized(drive, track);

  const NibbleTrack &nt = disk.nibbleTracks[track];
  if (!nt.valid || nt.nibbles.empty())
    return 0xFF;

  // Read nibble at current position
  uint8_t nibble = nt.nibbles[disk.nibblePosition];

  // Advance to next nibble
  disk.nibblePosition = (disk.nibblePosition + 1) % nt.nibbles.size();

  // All valid disk nibbles must have bit 7 set
  return nibble | 0x80;
}

void Disk2Controller::writeNibble(int drive, uint8_t value) {
  if (drive < 0 || drive >= NUM_DRIVES)
    return;

  DiskImage &disk = disks_[drive];
  if (!disk.loaded || disk.writeProtected)
    return;

  int track = disk.quarterTrack / 4;
  if (track < 0 || track >= NUM_TRACKS)
    return;

  ensureTrackNibblized(drive, track);

  NibbleTrack &nt = disk.nibbleTracks[track];
  if (!nt.valid || nt.nibbles.empty())
    return;

  nt.nibbles[disk.nibblePosition] = value;
  nt.dirty = true;
  disk.modified = true;

  disk.nibblePosition = (disk.nibblePosition + 1) % nt.nibbles.size();
}

uint8_t Disk2Controller::readDiskData() {
  if (!isMotorOn() || !isDiskInserted(selectedDrive_)) {
    return 0;
  }

  uint64_t currentCycle = currentCycle_;
  uint64_t &lastCycle = lastReadCycle_[selectedDrive_];

  bool newNibbleReady =
      (lastCycle == 0) || (currentCycle >= lastCycle + CYCLES_PER_NIBBLE);

  if (newNibbleReady) {
    dataLatch_ = readNibble(selectedDrive_);
    lastCycle = currentCycle;
    latchValid_ = true;
  }

  if (latchValid_) {
    latchValid_ = false;
    return dataLatch_;
  } else {
    return dataLatch_ & 0x7F;
  }
}

void Disk2Controller::writeDiskData(uint8_t value) {
  if (!isMotorOn() || !isDiskInserted(selectedDrive_)) {
    return;
  }

  if (disks_[selectedDrive_].writeProtected) {
    return;
  }

  writeNibble(selectedDrive_, value);
  dataLatch_ = value;
}

void Disk2Controller::update(int cycles) { currentCycle_ += cycles; }

bool Disk2Controller::insertDisk(int drive, const uint8_t *data, size_t size,
                                 const std::string &filename) {
  if (drive < 0 || drive >= NUM_DRIVES || !data || size == 0) {
    return false;
  }

  if (size == 143360) {
    // Standard DSK/DO/PO format
    bool proDosOrder = false;

    std::string lowerName = filename;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);

    if (lowerName.find(".po") != std::string::npos) {
      proDosOrder = true;
    } else if (lowerName.find(".dsk") != std::string::npos ||
               lowerName.find(".do") != std::string::npos) {
      proDosOrder = false;
    } else {
      // Content-based detection
      if (detectProDOS(data, size)) {
        proDosOrder = true;
      }
    }

    return loadDSK(drive, data, size, proDosOrder);
  }

  // Check for WOZ format
  if (size >= 12 && data[0] == 'W' && data[1] == 'O' && data[2] == 'Z') {
    return loadWOZ(drive, data, size);
  }

  return false;
}

void Disk2Controller::ejectDisk(int drive) {
  if (drive < 0 || drive >= NUM_DRIVES)
    return;

  disks_[drive] = DiskImage{};
  drives_[drive].diskInserted = false;
  lastReadCycle_[drive] = 0;
}

bool Disk2Controller::isDiskInserted(int drive) const {
  if (drive < 0 || drive >= NUM_DRIVES)
    return false;
  return disks_[drive].loaded;
}

DriveState Disk2Controller::getDriveState(int drive) const {
  DriveState state;
  if (drive < 0 || drive >= NUM_DRIVES) {
    return state;
  }

  const DiskImage &disk = disks_[drive];

  // Sync state from DiskImage to DriveState
  state.motorOn = (drive == selectedDrive_) ? motorOn_ : false;
  state.writeMode = q7_;
  state.currentTrack = disk.quarterTrack; // Quarter-track position
  state.currentPhase = disk.lastPhase;
  state.headPosition = static_cast<int>(disk.nibblePosition);
  state.dataLatch = dataLatch_;
  state.diskInserted = disk.loaded;

  return state;
}

bool Disk2Controller::isDiskModified(int drive) const {
  if (drive < 0 || drive >= NUM_DRIVES)
    return false;
  return disks_[drive].modified;
}

const uint8_t *Disk2Controller::getDiskData(int drive, size_t *size) const {
  if (drive < 0 || drive >= NUM_DRIVES || disks_[drive].sectorData.empty()) {
    *size = 0;
    return nullptr;
  }

  *size = disks_[drive].sectorData.size();
  return disks_[drive].sectorData.data();
}

bool Disk2Controller::detectProDOS(const uint8_t *data, size_t size) {
  if (size < 143360)
    return false;

  const uint8_t *block2 = data + 0x400;

  if ((block2[4] & 0xF0) != 0xF0)
    return false;

  int nameLen = block2[4] & 0x0F;
  if (nameLen == 0 || nameLen > 15)
    return false;

  for (int i = 0; i < nameLen; i++) {
    uint8_t c = block2[5 + i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) {
      return false;
    }
  }

  return true;
}

bool Disk2Controller::detectDOS33(const uint8_t *data, size_t size) {
  if (size < 143360)
    return false;

  const uint8_t *vtoc = data + 0x11000;

  if (vtoc[3] != 0x03)
    return false;
  if (vtoc[6] == 0 || vtoc[6] == 255)
    return false;
  if (vtoc[0x34] != 35)
    return false;
  if (vtoc[0x35] != 16)
    return false;

  return true;
}

bool Disk2Controller::loadDSK(int drive, const uint8_t *data, size_t size,
                              bool proDosOrder) {
  DiskImage &disk = disks_[drive];

  // Reset disk state
  disk = DiskImage{};

  // Store raw sector data
  disk.sectorData.resize(size);
  std::memcpy(disk.sectorData.data(), data, size);

  disk.format = proDosOrder ? DiskImage::Format::PO : DiskImage::Format::DSK;
  disk.writeProtected = false;
  disk.modified = false;
  disk.loaded = true;
  disk.volumeNumber = 254;

  // Reset head position
  disk.quarterTrack = 0;
  disk.phaseStates = 0;
  disk.lastPhase = 0;
  disk.nibblePosition = 0;

  // Invalidate all nibble tracks (will be generated on demand)
  for (auto &track : disk.nibbleTracks) {
    track.valid = false;
    track.dirty = false;
    track.nibbles.clear();
  }

  drives_[drive].diskInserted = true;
  lastReadCycle_[drive] = 0;

  return true;
}

void Disk2Controller::nibblizeTrack(DiskImage &disk, int track) {
  NibbleTrack &nt = disk.nibbleTracks[track];
  nt.nibbles.clear();
  nt.nibbles.reserve(TRACK_SIZE_NIBBLES);

  bool proDosOrder = (disk.format == DiskImage::Format::PO);
  const auto &physToLog =
      proDosOrder ? PRODOS_PHYSICAL_TO_LOGICAL : DOS_PHYSICAL_TO_LOGICAL;

  // Gap 1 (sync bytes)
  int gap1Size = 48;
  for (int i = 0; i < gap1Size; i++) {
    nt.nibbles.push_back(0xFF);
  }

  // Write each sector in physical order
  for (int physicalSector = 0; physicalSector < SECTORS_PER_TRACK;
       physicalSector++) {
    // Map physical sector to logical sector
    int logicalSector = physToLog[physicalSector];

    // Get sector data
    const uint8_t *sector =
        disk.sectorData.data() +
        (track * SECTORS_PER_TRACK + logicalSector) * BYTES_PER_SECTOR;

    // Sync bytes before address field
    for (int i = 0; i < 6; i++) {
      nt.nibbles.push_back(0xFF);
    }

    // Address field prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0x96);

    // Volume, track, sector, checksum (4-and-4 encoded)
    uint8_t volume = disk.volumeNumber;

    // 4-and-4 encode: split byte into odd and even bits
    auto encode44 = [](uint8_t val) -> std::pair<uint8_t, uint8_t> {
      uint8_t odd = ((val >> 1) & 0x55) | 0xAA;
      uint8_t even = (val & 0x55) | 0xAA;
      return {odd, even};
    };

    auto [volOdd, volEven] = encode44(volume);
    nt.nibbles.push_back(volOdd);
    nt.nibbles.push_back(volEven);

    auto [trkOdd, trkEven] = encode44(static_cast<uint8_t>(track));
    nt.nibbles.push_back(trkOdd);
    nt.nibbles.push_back(trkEven);

    auto [secOdd, secEven] = encode44(static_cast<uint8_t>(physicalSector));
    nt.nibbles.push_back(secOdd);
    nt.nibbles.push_back(secEven);

    uint8_t checksum = volume ^ track ^ physicalSector;
    auto [chkOdd, chkEven] = encode44(checksum);
    nt.nibbles.push_back(chkOdd);
    nt.nibbles.push_back(chkEven);

    // Address field epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 2
    for (int i = 0; i < 6; i++) {
      nt.nibbles.push_back(0xFF);
    }

    // Data field prologue
    nt.nibbles.push_back(0xD5);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xAD);

    // Encode sector data using 6-and-2
    std::array<uint8_t, 343> encoded{};
    encode62(sector, encoded.data());

    for (int i = 0; i < 343; i++) {
      nt.nibbles.push_back(encoded[i]);
    }

    // Data field epilogue
    nt.nibbles.push_back(0xDE);
    nt.nibbles.push_back(0xAA);
    nt.nibbles.push_back(0xEB);

    // Gap 3
    for (int i = 0; i < 27; i++) {
      nt.nibbles.push_back(0xFF);
    }
  }

  // Fill remaining with sync bytes
  while (nt.nibbles.size() < TRACK_SIZE_NIBBLES) {
    nt.nibbles.push_back(0xFF);
  }

  // Truncate if over
  if (nt.nibbles.size() > TRACK_SIZE_NIBBLES) {
    nt.nibbles.resize(TRACK_SIZE_NIBBLES);
  }
}

void Disk2Controller::encode62(const uint8_t *data, uint8_t *encoded) {
  // 6-and-2 encoding for Apple II disk sectors
  // Takes 256 bytes and produces 343 bytes (342 + checksum)

  std::array<uint8_t, 342> buffer{};

  // Step 1: Build auxiliary buffer (first 86 bytes)
  // Contains the lower 2 bits of each data byte, packed and reversed
  for (int i = 0; i < 86; i++) {
    uint8_t val = 0;

    int idx1 = 255 - i;
    int idx2 = 255 - i - 86;
    int idx3 = 255 - i - 172;

    if (idx1 >= 0 && idx1 < 256) {
      val |= ((data[idx1] & 0x01) << 1) | ((data[idx1] & 0x02) >> 1);
    }
    if (idx2 >= 0 && idx2 < 256) {
      val |= ((data[idx2] & 0x01) << 3) | ((data[idx2] & 0x02) << 1);
    }
    if (idx3 >= 0 && idx3 < 256) {
      val |= ((data[idx3] & 0x01) << 5) | ((data[idx3] & 0x02) << 3);
    }

    buffer[i] = val;
  }

  // Step 2: Build primary buffer (next 256 bytes)
  for (int i = 0; i < 256; i++) {
    buffer[86 + i] = data[i] >> 2;
  }

  // Step 3: XOR encode and convert to GCR nibbles
  uint8_t prev = 0;
  for (int i = 0; i < 342; i++) {
    uint8_t xorVal = buffer[i] ^ prev;
    encoded[i] = GCR_ENCODE_TABLE[xorVal & 0x3F];
    prev = buffer[i];
  }

  // Checksum nibble
  encoded[342] = GCR_ENCODE_TABLE[prev & 0x3F];
}

bool Disk2Controller::loadWOZ(int drive, const uint8_t *data, size_t size) {
  if (size < 12 || data[0] != 'W' || data[1] != 'O' || data[2] != 'Z') {
    return false;
  }

  bool woz2 = (data[3] == '2');

  DiskImage &disk = disks_[drive];
  disk = DiskImage{};
  disk.format = DiskImage::Format::WOZ;
  disk.writeProtected = true;
  disk.loaded = true;

  // Reset head position
  disk.quarterTrack = 0;
  disk.phaseStates = 0;
  disk.lastPhase = 0;
  disk.nibblePosition = 0;

  // Find chunks
  size_t offset = 12;
  const uint8_t *tmap = nullptr;
  const uint8_t *trks = nullptr;
  size_t trksSize = 0;

  while (offset + 8 <= size) {
    uint32_t chunkId;
    std::memcpy(&chunkId, data + offset, 4);
    uint32_t chunkSize;
    std::memcpy(&chunkSize, data + offset + 4, 4);

    if (chunkId == 0x50414D54) { // "TMAP"
      tmap = data + offset + 8;
    } else if (chunkId == 0x534B5254) { // "TRKS"
      trks = data + offset + 8;
      trksSize = chunkSize;
    }

    offset += 8 + chunkSize;
  }

  if (!tmap || !trks) {
    return false;
  }

  disk.wozTracks.resize(NUM_TRACKS);
  disk.wozBitCounts.resize(NUM_TRACKS, 0);

  if (woz2) {
    for (int track = 0; track < NUM_TRACKS; track++) {
      uint8_t tmapEntry = tmap[track * 4];
      if (tmapEntry == 0xFF)
        continue;

      if (tmapEntry * 8 + 8 > trksSize)
        continue;

      uint16_t startBlock;
      uint16_t blockCount;
      uint32_t bitCount;
      std::memcpy(&startBlock, trks + tmapEntry * 8, 2);
      std::memcpy(&blockCount, trks + tmapEntry * 8 + 2, 2);
      std::memcpy(&bitCount, trks + tmapEntry * 8 + 4, 4);

      size_t dataOffset = 256 + (startBlock * 512);
      size_t dataSize = blockCount * 512;

      if (dataOffset + dataSize <= size) {
        disk.wozTracks[track].resize(dataSize);
        std::memcpy(disk.wozTracks[track].data(), data + dataOffset, dataSize);
        disk.wozBitCounts[track] = bitCount;
      }
    }
  } else {
    // WOZ1
    for (int track = 0; track < NUM_TRACKS; track++) {
      uint8_t tmapEntry = tmap[track * 4];
      if (tmapEntry == 0xFF)
        continue;

      size_t trkOffset = tmapEntry * 6656;
      if (trkOffset + 6656 > trksSize)
        continue;

      disk.wozTracks[track].resize(6656);
      std::memcpy(disk.wozTracks[track].data(), trks + trkOffset, 6656);

      uint16_t bitCount;
      std::memcpy(&bitCount, trks + trkOffset + 6646, 2);
      disk.wozBitCounts[track] = bitCount;
    }
  }

  drives_[drive].diskInserted = true;
  lastReadCycle_[drive] = 0;

  return true;
}

} // namespace a2e
