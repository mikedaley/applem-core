#include "emulator.hpp"
#include <cstring>

// Include generated ROM data directly
#include "roms.cpp" // namespace roms

namespace a2e {

Emulator::Emulator() {
  mmu_ = std::make_unique<MMU>();
  disk_ = std::make_unique<Disk2Controller>();
  video_ = std::make_unique<Video>(*mmu_);
  audio_ = std::make_unique<Audio>();
  keyboard_ = std::make_unique<Keyboard>();

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
  mmu_->setSpeakerCallback([this]() { toggleSpeaker(); });
  mmu_->setButtonCallback([this](int btn) { return getButtonState(btn); });
  mmu_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Connect disk controller to MMU
  mmu_->setDiskController(disk_.get());

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

} // namespace a2e
