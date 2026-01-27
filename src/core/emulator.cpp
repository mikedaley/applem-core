#include "emulator.hpp"
#include "cards/disk2_card.hpp"
#include "cards/mockingboard_card.hpp"
#include "cards/thunderclock_card.hpp"
#include <cstring>

// Include generated ROM data directly
#include "roms.cpp" // namespace roms

namespace a2e {

Emulator::Emulator() {
  mmu_ = std::make_unique<MMU>();
  disk_ = std::make_unique<Disk2Card>();
  video_ = std::make_unique<Video>(*mmu_);
  audio_ = std::make_unique<Audio>();
  keyboard_ = std::make_unique<Keyboard>();
  mockingboard_ = std::make_unique<MockingboardCard>();

  // Create CPU with memory callbacks
  cpu_ = std::make_unique<CPU6502>(
      [this](uint16_t addr) { return cpuRead(addr); },
      [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
      CPUVariant::CMOS_65C02);

  // Set up keyboard callback to receive translated keys
  keyboard_->setKeyCallback([this](int key) { keyDown(key); });

  // Set up MMU callbacks
  mmu_->setKeyboardCallback([this]() { return getKeyboardData(); });
  mmu_->setKeyStrobeCallback([this]() { clearKeyboardStrobe(); });
  mmu_->setAnyKeyDownCallback([this]() { return keyDown_; });
  mmu_->setSpeakerCallback([this]() { toggleSpeaker(); });
  mmu_->setButtonCallback([this](int btn) { return getButtonState(btn); });
  mmu_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Connect disk controller to MMU
  mmu_->setDiskController(disk_.get());

  // Connect Mockingboard to MMU and audio
  mmu_->setMockingboard(mockingboard_.get());
  audio_->setMockingboard(mockingboard_.get());

  // Set up Mockingboard callbacks
  mockingboard_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
  mockingboard_->setIRQCallback([this]() { cpu_->irq(); });

  // Set up level-triggered IRQ polling for VIA interrupts
  // VIA IRQs stay asserted until acknowledged by reading T1CL
  cpu_->setIRQStatusCallback([this]() { return mockingboard_->isIRQActive(); });

  // Set up disk timing callback - allows disk reads to get accurate cycle count
  // during instruction execution (before disk_->update() is called)
  disk_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
}

Emulator::~Emulator() = default;

void Emulator::init() {
  // Load ROMs - using combined 16KB system ROM and Disk II ROM
  mmu_->loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE, roms::ROM_CHAR,
                roms::ROM_CHAR_SIZE, roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

  reset();
}

void Emulator::reset() {
  mmu_->reset();
  cpu_->resetCycleCount(); // Clear cycle counter for fresh power-on state
  cpu_->reset();
  audio_->reset();
  disk_->reset();
  keyboard_->reset();
  mockingboard_->reset();

  // Clear Apple button states
  setButton(0, false);
  setButton(1, false);

  keyboardLatch_ = 0;
  keyDown_ = false;
  lastFrameCycle_ = 0;
  frameReady_ = false;
  breakpointHit_ = false;
  paused_ = false;
}

void Emulator::warmReset() {
  // Warm reset - only reset CPU, preserve memory and disk state
  cpu_->reset();

  // Clear keyboard strobe
  keyboardLatch_ &= 0x7F;

  // Reset keyboard modifier states
  keyboard_->reset();
  setButton(0, false);
  setButton(1, false);

  // Stop disk motor (real Apple II behavior on Ctrl+Reset)
  disk_->stopMotor();

  breakpointHit_ = false;
  paused_ = false;
}

void Emulator::runCycles(int cycles) {
  if (paused_)
    return;

  uint64_t startCycles = cpu_->getTotalCycles();
  uint64_t targetCycles = startCycles + cycles;

  while (cpu_->getTotalCycles() < targetCycles) {
    // Check breakpoints
    if (!breakpoints_.empty()) {
      uint16_t pc = cpu_->getPC();
      if (breakpoints_.count(pc) && !disabledBreakpoints_.count(pc)) {
        breakpointHit_ = true;
        breakpointAddress_ = pc;
        paused_ = true;
        return;
      }
    }

    // Track cycles before instruction
    uint64_t cyclesBefore = cpu_->getTotalCycles();

    // Execute one instruction
    cpu_->executeInstruction();

    // Update disk controller with actual instruction cycles
    uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
    disk_->update(static_cast<int>(cyclesUsed));

    // Update Mockingboard timers BEFORE next instruction
    // This ensures timer IRQs fire before the CPU can disable them
    mockingboard_->update(static_cast<int>(cyclesUsed));

    // Check for frame boundary
    uint64_t currentCycle = cpu_->getTotalCycles();
    if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
      lastFrameCycle_ = currentCycle;
      video_->renderFrame();
      frameReady_ = true;
    }
  }
}

int Emulator::generateAudioSamples(float *buffer, int sampleCount) {
  // Calculate cycles needed for this audio buffer
  int cyclesToRun = static_cast<int>(sampleCount * CYCLES_PER_SAMPLE);

  // Run emulation for the required cycles
  runCycles(cyclesToRun);

  // Track samples for frame synchronization
  samplesGenerated_ += sampleCount;

  // Generate audio samples
  return audio_->generateSamples(buffer, sampleCount, cpu_->getTotalCycles());
}

int Emulator::generateStereoAudioSamples(float *buffer, int sampleCount) {
  // Calculate cycles needed for this audio buffer
  int cyclesToRun = static_cast<int>(sampleCount * CYCLES_PER_SAMPLE);

  // Run emulation for the required cycles
  runCycles(cyclesToRun);

  // Track samples for frame synchronization
  samplesGenerated_ += sampleCount;

  // Generate stereo audio samples (interleaved L/R)
  return audio_->generateStereoSamples(buffer, sampleCount, cpu_->getTotalCycles());
}

int Emulator::consumeFrameSamples() {
  // Returns number of complete frames worth of samples generated
  // 48000 Hz / 60 Hz = 800 samples per frame
  int frames = samplesGenerated_ / SAMPLES_PER_FRAME;
  samplesGenerated_ %= SAMPLES_PER_FRAME;
  return frames;
}

const uint8_t *Emulator::getFramebuffer() const {
  return video_->getFramebuffer();
}

int Emulator::handleRawKeyDown(int browserKeycode, bool shift, bool ctrl,
                                bool alt, bool meta, bool capsLock) {
  int result = keyboard_->handleKeyDown(browserKeycode, shift, ctrl, alt, meta, capsLock);

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple

  return result;
}

void Emulator::handleRawKeyUp(int browserKeycode, bool shift, bool ctrl,
                              bool alt, bool meta) {
  keyboard_->handleKeyUp(browserKeycode, shift, ctrl, alt, meta);

  // Update button state from modifier keys
  setButton(0, keyboard_->isOpenApplePressed());   // Open Apple
  setButton(1, keyboard_->isClosedApplePressed()); // Closed Apple
}

void Emulator::keyDown(int keycode) {
  // Direct Apple II keycode input (used for paste functionality)
  // Sets the keyboard latch with high bit set
  keyboardLatch_ = (keycode & 0x7F) | 0x80;
  keyDown_ = true;
}

void Emulator::keyUp(int keycode) {
  (void)keycode;
  keyDown_ = false;
}

void Emulator::setButton(int button, bool pressed) {
  if (button >= 0 && button < 3) {
    buttonState_[button] = pressed;
  }
}

void Emulator::setPaddleValue(int paddle, int value) {
  mmu_->setPaddleValue(paddle, static_cast<uint8_t>(value & 0xFF));
}

int Emulator::getPaddleValue(int paddle) const {
  return mmu_->getPaddleValue(paddle);
}

uint8_t Emulator::getButtonState(int button) {
  if (button >= 0 && button < 3 && buttonState_[button]) {
    return 0x80; // Bit 7 set = button pressed
  }
  return 0x00; // Button not pressed
}

bool Emulator::insertDisk(int drive, const uint8_t *data, size_t size,
                          const char *filename) {
  return disk_->insertDisk(drive, data, size, filename ? filename : "");
}

bool Emulator::insertBlankDisk(int drive) {
  return disk_->insertBlankDisk(drive);
}

void Emulator::ejectDisk(int drive) { disk_->ejectDisk(drive); }

const uint8_t *Emulator::getDiskData(int drive, size_t *size) const {
  return disk_->getDiskData(drive, size);
}

const uint8_t *Emulator::exportDiskData(int drive, size_t *size) {
  return disk_->exportDiskData(drive, size);
}

const char *Emulator::getDiskFilename(int drive) const {
  const auto *image = disk_->getDiskImage(drive);
  if (!image) {
    return nullptr;
  }
  return image->getFilename().c_str();
}

void Emulator::addBreakpoint(uint16_t address) { breakpoints_.insert(address); }

void Emulator::removeBreakpoint(uint16_t address) {
  breakpoints_.erase(address);
  disabledBreakpoints_.erase(address);
}

void Emulator::enableBreakpoint(uint16_t address, bool enabled) {
  if (enabled) {
    disabledBreakpoints_.erase(address);
  } else {
    if (breakpoints_.count(address)) {
      disabledBreakpoints_.insert(address);
    }
  }
}

void Emulator::stepInstruction() {
  breakpointHit_ = false;

  // Track cycles before instruction
  uint64_t cyclesBefore = cpu_->getTotalCycles();

  cpu_->executeInstruction();

  // Update disk controller with actual instruction cycles
  uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
  disk_->update(static_cast<int>(cyclesUsed));

  // Update Mockingboard timers
  mockingboard_->update(static_cast<int>(cyclesUsed));

  // Check for frame boundary
  uint64_t currentCycle = cpu_->getTotalCycles();
  if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
    lastFrameCycle_ = currentCycle;
    video_->renderFrame();
    frameReady_ = true;
  }
}

uint8_t Emulator::readMemory(uint16_t address) const {
  return const_cast<MMU *>(mmu_.get())->read(address);
}

uint8_t Emulator::peekMemory(uint16_t address) const {
  return mmu_->peek(address);
}

void Emulator::writeMemory(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

const char *Emulator::disassembleAt(uint16_t address) {
  disasmBuffer_ = cpu_->disassembleAt(address);
  return disasmBuffer_.c_str();
}

uint64_t Emulator::getSoftSwitchState() const {
  const auto &sw = mmu_->getSoftSwitches();
  uint64_t state = 0;

  // Pack soft switch state into a 64-bit value
  // Display switches (bits 0-5)
  if (sw.text)
    state |= (1ULL << 0);
  if (sw.mixed)
    state |= (1ULL << 1);
  if (sw.page2)
    state |= (1ULL << 2);
  if (sw.hires)
    state |= (1ULL << 3);
  if (sw.col80)
    state |= (1ULL << 4);
  if (sw.altCharSet)
    state |= (1ULL << 5);

  // Memory switches (bits 6-12)
  if (sw.store80)
    state |= (1ULL << 6);
  if (sw.ramrd)
    state |= (1ULL << 7);
  if (sw.ramwrt)
    state |= (1ULL << 8);
  if (sw.intcxrom)
    state |= (1ULL << 9);
  if (sw.altzp)
    state |= (1ULL << 10);
  if (sw.slotc3rom)
    state |= (1ULL << 11);
  if (sw.intc8rom)
    state |= (1ULL << 12);

  // Language card (bits 13-16)
  if (sw.lcram)
    state |= (1ULL << 13);
  if (sw.lcram2)
    state |= (1ULL << 14);
  if (sw.lcwrite)
    state |= (1ULL << 15);
  if (sw.lcprewrite)
    state |= (1ULL << 16);

  // Annunciators (bits 17-20)
  if (sw.an0)
    state |= (1ULL << 17);
  if (sw.an1)
    state |= (1ULL << 18);
  if (sw.an2)
    state |= (1ULL << 19);
  if (sw.an3)
    state |= (1ULL << 20);

  // I/O state (bits 21-23)
  if (sw.vblBar)
    state |= (1ULL << 21);
  if (sw.cassetteOut)
    state |= (1ULL << 22);
  if (sw.cassetteIn)
    state |= (1ULL << 23);

  // Buttons (bits 24-26)
  if (buttonState_[0])
    state |= (1ULL << 24);
  if (buttonState_[1])
    state |= (1ULL << 25);
  if (buttonState_[2])
    state |= (1ULL << 26);

  // Keyboard (bit 27)
  if (keyboardLatch_ & 0x80)
    state |= (1ULL << 27);

  // DHIRES (bit 28) - computed from AN3 off + 80COL + HIRES
  bool dhires = !sw.an3 && sw.col80 && sw.hires;
  if (dhires)
    state |= (1ULL << 28);

  // IOUDIS (bit 29)
  if (sw.ioudis)
    state |= (1ULL << 29);

  return state;
}

uint8_t Emulator::cpuRead(uint16_t address) { return mmu_->read(address); }

void Emulator::cpuWrite(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

uint8_t Emulator::getKeyboardData() { return keyboardLatch_; }

void Emulator::clearKeyboardStrobe() {
  keyboardLatch_ &= 0x7F; // Clear high bit
}

void Emulator::toggleSpeaker() {
  audio_->toggleSpeaker(cpu_->getTotalCycles());
}

// ============================================================================
// Slot Management
// ============================================================================

const char* Emulator::getSlotCardName(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return "invalid";
  }

  // Slot 3 is built-in 80-column
  if (slot == 3) {
    return "80col";
  }

  // Slot 6: Disk II (always present for now, TODO: make configurable)
  if (slot == 6) {
    return "disk2";
  }

  // Slot 4: Mockingboard - check if enabled
  if (slot == 4) {
    if (mockingboard_ && mockingboard_->isEnabled()) {
      return "mockingboard";
    }
    return "empty";
  }

  // Check the slot array for other cards
  ExpansionCard* card = mmu_->getCard(slot);
  if (!card) {
    return "empty";
  }

  // Identify card type by name
  const char* name = card->getName();
  if (strcmp(name, "Disk II") == 0) {
    return "disk2";
  }
  if (strcmp(name, "Mockingboard") == 0) {
    return "mockingboard";
  }
  if (strcmp(name, "Thunderclock") == 0) {
    return "thunderclock";
  }

  return "empty";
}

bool Emulator::setSlotCard(uint8_t slot, const char* cardId) {
  if (slot < 1 || slot > 7) {
    return false;
  }

  // Slot 3 is built-in 80-column and cannot be changed
  if (slot == 3) {
    return false;
  }

  // Handle empty slot
  if (strcmp(cardId, "empty") == 0) {
    // Remove peripherals from this slot
    if (slot == 6 && disk_) {
      mmu_->setDiskController(nullptr);
    }
    if (slot == 4 && mockingboard_) {
      // Disconnect from MMU and Audio
      mmu_->setMockingboard(nullptr);
      audio_->setMockingboard(nullptr);
      // Disable the Mockingboard so isEnabled() returns false
      mockingboard_->setEnabled(false);
    }
    mmu_->removeCard(slot);
    return true;
  }

  // Handle Disk II card
  if (strcmp(cardId, "disk2") == 0) {
    // If moving from legacy slot 6, first disconnect
    if (slot != 6 && disk_) {
      mmu_->setDiskController(nullptr);
    }

    // For now, we continue using legacy mode for disk controller
    // because the disk controller needs to share state with DiskManager
    if (slot == 6) {
      mmu_->setDiskController(disk_.get());
    } else {
      // Create a new Disk2Card for non-default slots
      // Note: This would create a separate controller, not shared with DiskManager
      // For MVP, we only support slot 6 for disk
      return false;
    }
    return true;
  }

  // Handle Mockingboard card
  if (strcmp(cardId, "mockingboard") == 0) {
    // Only slot 4 is supported for Mockingboard
    if (slot != 4) {
      return false;
    }

    // Connect Mockingboard to slot 4
    mmu_->setMockingboard(mockingboard_.get());
    audio_->setMockingboard(mockingboard_.get());
    mockingboard_->setEnabled(true);
    return true;
  }

  // Handle Thunderclock card
  if (strcmp(cardId, "thunderclock") == 0) {
    // Create and insert Thunderclock card
    auto card = std::make_unique<ThunderclockCard>();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  return false;
}

bool Emulator::isSlotEmpty(uint8_t slot) const {
  if (slot < 1 || slot > 7) {
    return true;
  }

  // Slot 3 is never empty (built-in 80-column)
  if (slot == 3) {
    return false;
  }

  // Slot 6: Disk II is always present for now
  if (slot == 6) {
    return false;
  }

  // Slot 4: Check if Mockingboard is enabled
  if (slot == 4) {
    return !(mockingboard_ && mockingboard_->isEnabled());
  }

  return mmu_->isSlotEmpty(slot);
}

// ============================================================================
// State Serialization
// ============================================================================

// State format version - increment when format changes
static constexpr uint32_t STATE_VERSION = 4;  // Added Mockingboard state
static constexpr uint32_t STATE_MAGIC = 0x53324541; // "A2ES" in little-endian

// Helper to write little-endian values
static void writeLE16(std::vector<uint8_t> &buf, uint16_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
}

static void writeLE32(std::vector<uint8_t> &buf, uint32_t val) {
  buf.push_back(val & 0xFF);
  buf.push_back((val >> 8) & 0xFF);
  buf.push_back((val >> 16) & 0xFF);
  buf.push_back((val >> 24) & 0xFF);
}

static void writeLE64(std::vector<uint8_t> &buf, uint64_t val) {
  for (int i = 0; i < 8; i++) {
    buf.push_back((val >> (i * 8)) & 0xFF);
  }
}

// Helper to read little-endian values
static uint16_t readLE16(const uint8_t *data) {
  return data[0] | (data[1] << 8);
}

static uint32_t readLE32(const uint8_t *data) {
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static uint64_t readLE64(const uint8_t *data) {
  uint64_t val = 0;
  for (int i = 0; i < 8; i++) {
    val |= static_cast<uint64_t>(data[i]) << (i * 8);
  }
  return val;
}

const uint8_t *Emulator::exportState(size_t *size) {
  stateBuffer_.clear();

  // Reserve approximate size (slightly over to avoid reallocations)
  // ~200KB total (128KB main/aux + 32KB LC RAM + overhead + disk images)
  stateBuffer_.reserve(500000);

  // Header
  writeLE32(stateBuffer_, STATE_MAGIC);
  writeLE32(stateBuffer_, STATE_VERSION);

  // CPU state
  stateBuffer_.push_back(cpu_->getA());
  stateBuffer_.push_back(cpu_->getX());
  stateBuffer_.push_back(cpu_->getY());
  stateBuffer_.push_back(cpu_->getSP());
  stateBuffer_.push_back(cpu_->getP());
  stateBuffer_.push_back(0); // Reserved
  writeLE16(stateBuffer_, cpu_->getPC());
  writeLE64(stateBuffer_, cpu_->getTotalCycles());

  // Memory - Main RAM (64KB)
  for (uint32_t addr = 0; addr < MAIN_RAM_SIZE; addr++) {
    stateBuffer_.push_back(mmu_->readRAM(addr, false));
  }

  // Memory - Aux RAM (64KB)
  for (uint32_t addr = 0; addr < AUX_RAM_SIZE; addr++) {
    stateBuffer_.push_back(mmu_->readRAM(addr, true));
  }

  // Language Card RAM - Main (16KB total: 4KB bank1 + 4KB bank2 + 8KB high)
  const uint8_t *lcb1 = mmu_->getLCBank1(false);
  const uint8_t *lcb2 = mmu_->getLCBank2(false);
  const uint8_t *lchi = mmu_->getLCHighRAM(false);
  stateBuffer_.insert(stateBuffer_.end(), lcb1, lcb1 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), lcb2, lcb2 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), lchi, lchi + 0x2000);

  // Language Card RAM - Aux (16KB total)
  const uint8_t *alcb1 = mmu_->getLCBank1(true);
  const uint8_t *alcb2 = mmu_->getLCBank2(true);
  const uint8_t *alchi = mmu_->getLCHighRAM(true);
  stateBuffer_.insert(stateBuffer_.end(), alcb1, alcb1 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), alcb2, alcb2 + 0x1000);
  stateBuffer_.insert(stateBuffer_.end(), alchi, alchi + 0x2000);

  // Soft switches - pack into bytes
  const auto &sw = mmu_->getSoftSwitches();
  uint32_t switches1 = 0;
  if (sw.text) switches1 |= (1 << 0);
  if (sw.mixed) switches1 |= (1 << 1);
  if (sw.page2) switches1 |= (1 << 2);
  if (sw.hires) switches1 |= (1 << 3);
  if (sw.col80) switches1 |= (1 << 4);
  if (sw.altCharSet) switches1 |= (1 << 5);
  if (sw.store80) switches1 |= (1 << 6);
  if (sw.ramrd) switches1 |= (1 << 7);
  if (sw.ramwrt) switches1 |= (1 << 8);
  if (sw.intcxrom) switches1 |= (1 << 9);
  if (sw.altzp) switches1 |= (1 << 10);
  if (sw.slotc3rom) switches1 |= (1 << 11);
  if (sw.intc8rom) switches1 |= (1 << 12);
  if (sw.lcram) switches1 |= (1 << 13);
  if (sw.lcram2) switches1 |= (1 << 14);
  if (sw.lcwrite) switches1 |= (1 << 15);
  if (sw.lcprewrite) switches1 |= (1 << 16);
  if (sw.an0) switches1 |= (1 << 17);
  if (sw.an1) switches1 |= (1 << 18);
  if (sw.an2) switches1 |= (1 << 19);
  if (sw.an3) switches1 |= (1 << 20);
  if (sw.ioudis) switches1 |= (1 << 21);
  writeLE32(stateBuffer_, switches1);

  // Keyboard latch
  stateBuffer_.push_back(keyboardLatch_);
  stateBuffer_.push_back(keyDown_ ? 1 : 0);

  // Button state
  stateBuffer_.push_back(buttonState_[0] ? 1 : 0);
  stateBuffer_.push_back(buttonState_[1] ? 1 : 0);
  stateBuffer_.push_back(buttonState_[2] ? 1 : 0);

  // Emulator timing
  writeLE64(stateBuffer_, lastFrameCycle_);
  writeLE32(stateBuffer_, samplesGenerated_);

  // Disk controller state
  auto &disk = *disk_;
  stateBuffer_.push_back(disk.isMotorOn() ? 1 : 0);
  stateBuffer_.push_back(static_cast<uint8_t>(disk.getSelectedDrive()));
  stateBuffer_.push_back(disk.getQ6() ? 1 : 0);
  stateBuffer_.push_back(disk.getQ7() ? 1 : 0);
  stateBuffer_.push_back(disk.getPhaseStates());
  stateBuffer_.push_back(disk.getDataLatch());

  // Per-drive state (track positions, disk image data, and filenames)
  for (int drive = 0; drive < 2; drive++) {
    if (disk.hasDisk(drive)) {
      const auto *image = disk.getDiskImage(drive);
      stateBuffer_.push_back(1); // Disk present
      writeLE16(stateBuffer_, static_cast<uint16_t>(image->getQuarterTrack()));

      // Export the disk image data
      size_t diskSize = 0;
      const uint8_t *diskData = disk.exportDiskData(drive, &diskSize);
      writeLE32(stateBuffer_, static_cast<uint32_t>(diskSize));
      if (diskData && diskSize > 0) {
        stateBuffer_.insert(stateBuffer_.end(), diskData, diskData + diskSize);
      }

      // Save filename
      const std::string &filename = image->getFilename();
      writeLE16(stateBuffer_, static_cast<uint16_t>(filename.length()));
      stateBuffer_.insert(stateBuffer_.end(), filename.begin(), filename.end());
    } else {
      stateBuffer_.push_back(0); // No disk
      writeLE16(stateBuffer_, 0);
      writeLE32(stateBuffer_, 0); // Zero disk size
      writeLE16(stateBuffer_, 0); // Zero filename length
    }
  }

  // Audio state (speaker)
  stateBuffer_.push_back(audio_->getSpeakerState() ? 1 : 0);

  // Mockingboard state
  uint8_t mbState[MockingboardCard::STATE_SIZE];
  size_t mbSize = mockingboard_->serialize(mbState, sizeof(mbState));
  writeLE16(stateBuffer_, static_cast<uint16_t>(mbSize));
  stateBuffer_.insert(stateBuffer_.end(), mbState, mbState + mbSize);

  *size = stateBuffer_.size();
  return stateBuffer_.data();
}

bool Emulator::importState(const uint8_t *data, size_t size) {
  // Minimum size check
  if (size < 8) {
    return false;
  }

  size_t offset = 0;

  // Check magic and version before resetting
  uint32_t magic = readLE32(data + offset);
  offset += 4;
  if (magic != STATE_MAGIC) {
    return false;
  }

  uint32_t version = readLE32(data + offset);
  offset += 4;
  if (version != STATE_VERSION) {
    return false;
  }

  // Reset emulator to clean state before importing
  // This ensures no old state is left floating around
  reset();

  // CPU state
  if (offset + 16 > size) return false;
  cpu_->setA(data[offset++]);
  cpu_->setX(data[offset++]);
  cpu_->setY(data[offset++]);
  cpu_->setSP(data[offset++]);
  cpu_->setP(data[offset++]);
  offset++; // Reserved

  cpu_->setPC(readLE16(data + offset));
  offset += 2;

  // Restore total cycles for accurate disk timing
  uint64_t totalCycles = readLE64(data + offset);
  offset += 8;
  cpu_->setTotalCycles(totalCycles);

  // Memory - Main RAM (64KB)
  if (offset + MAIN_RAM_SIZE > size) return false;
  for (uint32_t addr = 0; addr < MAIN_RAM_SIZE; addr++) {
    mmu_->writeRAM(addr, data[offset++], false);
  }

  // Memory - Aux RAM (64KB)
  if (offset + AUX_RAM_SIZE > size) return false;
  for (uint32_t addr = 0; addr < AUX_RAM_SIZE; addr++) {
    mmu_->writeRAM(addr, data[offset++], true);
  }

  // Language Card RAM - Main (16KB: 4KB + 4KB + 8KB)
  if (offset + 0x4000 > size) return false;
  mmu_->setLCBank1(data + offset, false);
  offset += 0x1000;
  mmu_->setLCBank2(data + offset, false);
  offset += 0x1000;
  mmu_->setLCHighRAM(data + offset, false);
  offset += 0x2000;

  // Language Card RAM - Aux (16KB)
  if (offset + 0x4000 > size) return false;
  mmu_->setLCBank1(data + offset, true);
  offset += 0x1000;
  mmu_->setLCBank2(data + offset, true);
  offset += 0x1000;
  mmu_->setLCHighRAM(data + offset, true);
  offset += 0x2000;

  // Soft switches
  if (offset + 4 > size) return false;
  uint32_t switches1 = readLE32(data + offset);
  offset += 4;

  // Restore soft switches by writing to appropriate addresses
  // TEXT/GRAPHICS
  mmu_->write((switches1 & (1 << 0)) ? 0xC051 : 0xC050, 0);
  // MIXED
  mmu_->write((switches1 & (1 << 1)) ? 0xC053 : 0xC052, 0);
  // PAGE1/PAGE2
  mmu_->write((switches1 & (1 << 2)) ? 0xC055 : 0xC054, 0);
  // LORES/HIRES
  mmu_->write((switches1 & (1 << 3)) ? 0xC057 : 0xC056, 0);
  // 80COL
  mmu_->write((switches1 & (1 << 4)) ? 0xC00D : 0xC00C, 0);
  // ALTCHARSET
  mmu_->write((switches1 & (1 << 5)) ? 0xC00F : 0xC00E, 0);
  // 80STORE
  mmu_->write((switches1 & (1 << 6)) ? 0xC001 : 0xC000, 0);
  // RAMRD
  mmu_->write((switches1 & (1 << 7)) ? 0xC003 : 0xC002, 0);
  // RAMWRT
  mmu_->write((switches1 & (1 << 8)) ? 0xC005 : 0xC004, 0);
  // INTCXROM
  mmu_->write((switches1 & (1 << 9)) ? 0xC007 : 0xC006, 0);
  // ALTZP
  mmu_->write((switches1 & (1 << 10)) ? 0xC009 : 0xC008, 0);
  // SLOTC3ROM
  mmu_->write((switches1 & (1 << 11)) ? 0xC00B : 0xC00A, 0);
  // INTC8ROM - this is set automatically by slot access
  // LCRAM, LCRAM2, LCWRITE - need special handling via language card switches
  // AN0-AN3
  mmu_->write((switches1 & (1 << 17)) ? 0xC059 : 0xC058, 0);
  mmu_->write((switches1 & (1 << 18)) ? 0xC05B : 0xC05A, 0);
  mmu_->write((switches1 & (1 << 19)) ? 0xC05D : 0xC05C, 0);
  mmu_->write((switches1 & (1 << 20)) ? 0xC05F : 0xC05E, 0);

  // Language card state restoration
  bool lcram = switches1 & (1 << 13);
  bool lcram2 = switches1 & (1 << 14);
  bool lcwrite = switches1 & (1 << 15);
  // Select the appropriate language card mode
  // lcram2 means bank 2 is selected (not bank 1)
  // $C080: bank2, read RAM, no write
  // $C081: bank2, read ROM, write (needs double read)
  // $C083: bank2, read RAM, write (needs double read)
  // $C088: bank1, read RAM, no write
  // etc.
  if (lcram) {
    if (lcram2) {
      if (lcwrite) {
        mmu_->read(0xC083);
        mmu_->read(0xC083); // Double read to enable write
      } else {
        mmu_->read(0xC080);
      }
    } else {
      if (lcwrite) {
        mmu_->read(0xC08B);
        mmu_->read(0xC08B);
      } else {
        mmu_->read(0xC088);
      }
    }
  } else {
    if (lcram2) {
      mmu_->read(0xC082);
    } else {
      mmu_->read(0xC08A);
    }
  }

  // Keyboard state
  if (offset + 5 > size) return false;
  keyboardLatch_ = data[offset++];
  keyDown_ = data[offset++] != 0;

  // Button state
  buttonState_[0] = data[offset++] != 0;
  buttonState_[1] = data[offset++] != 0;
  buttonState_[2] = data[offset++] != 0;

  // Emulator timing
  if (offset + 12 > size) return false;
  lastFrameCycle_ = readLE64(data + offset);
  offset += 8;
  samplesGenerated_ = static_cast<int>(readLE32(data + offset));
  offset += 4;

  // Disk controller state
  if (offset + 6 > size) return false;
  bool motorOn = data[offset++] != 0;
  int selectedDrive = data[offset++];
  bool q6 = data[offset++] != 0;
  bool q7 = data[offset++] != 0;
  uint8_t phaseStates = data[offset++];
  uint8_t dataLatch = data[offset++];

  // Restore disk controller state (including motor for mid-load restores)
  disk_->setMotorOn(motorOn);
  disk_->setSelectedDrive(selectedDrive);
  disk_->setQ6(q6);
  disk_->setQ7(q7);
  disk_->setPhaseStates(phaseStates);
  disk_->setDataLatch(dataLatch);

  // Per-drive state (disk images and track positions)
  for (int drive = 0; drive < 2; drive++) {
    if (offset + 3 > size) return false;
    bool hasDisk = data[offset++] != 0;
    uint16_t quarterTrack = readLE16(data + offset);
    offset += 2;

    // Read disk size
    if (offset + 4 > size) return false;
    uint32_t diskSize = readLE32(data + offset);
    offset += 4;

    if (hasDisk && diskSize > 0) {
      if (offset + diskSize > size) return false;

      // Read disk data offset for insertion
      const uint8_t *diskData = data + offset;
      offset += diskSize;

      // Read filename
      if (offset + 2 > size) return false;
      uint16_t filenameLen = readLE16(data + offset);
      offset += 2;

      std::string filename = "state.dsk";
      if (filenameLen > 0 && offset + filenameLen <= size) {
        filename = std::string(reinterpret_cast<const char*>(data + offset), filenameLen);
        offset += filenameLen;
      }

      // Insert the disk image from the saved state
      disk_->insertDisk(drive, diskData, diskSize, filename);

      // Restore track position
      auto *image = disk_->getMutableDiskImage(drive);
      if (image) {
        image->setQuarterTrack(quarterTrack);
      }
    } else {
      // Skip filename length field (will be 0)
      if (offset + 2 <= size) {
        offset += 2;
      }
      // Eject any existing disk
      disk_->ejectDisk(drive);
    }
  }

  // Audio state
  if (offset + 1 > size) return false;
  bool speakerState = data[offset++] != 0;
  (void)speakerState;

  // Mockingboard state
  if (offset + 2 <= size) {
    uint16_t mbSize = readLE16(data + offset);
    offset += 2;
    if (mbSize > 0 && offset + mbSize <= size) {
      mockingboard_->deserialize(data + offset, mbSize);
      offset += mbSize;
    }
  }

  frameReady_ = true;
  breakpointHit_ = false;
  paused_ = false;

  return true;
}

} // namespace a2e
