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

  // Create CPU with memory callbacks
  cpu_ = std::make_unique<CPU6502>(
      [this](uint16_t addr) { return cpuRead(addr); },
      [this](uint16_t addr, uint8_t val) { cpuWrite(addr, val); },
      CPUVariant::CMOS_65C02);

  // Set up MMU callbacks
  mmu_->setKeyboardCallback([this]() { return getKeyboardData(); });
  mmu_->setKeyStrobeCallback([this]() { clearKeyboardStrobe(); });
  mmu_->setSpeakerCallback([this]() { toggleSpeaker(); });

  // Connect disk controller to MMU
  mmu_->setDiskController(disk_.get());
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

void Emulator::keyDown(int keycode) {
  // JavaScript already sends the correct Apple II ASCII value
  // Just set the keyboard latch with high bit set
  keyboardLatch_ = (keycode & 0x7F) | 0x80;
  keyDown_ = true;
}

void Emulator::keyUp(int keycode) { keyDown_ = false; }

bool Emulator::insertDisk(int drive, const uint8_t *data, size_t size,
                          const char *filename) {
  return disk_->insertDisk(drive, data, size, filename ? filename : "");
}

void Emulator::ejectDisk(int drive) { disk_->ejectDisk(drive); }

const uint8_t *Emulator::getDiskData(int drive, size_t *size) const {
  return disk_->getDiskData(drive, size);
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

void Emulator::writeMemory(uint16_t address, uint8_t value) {
  mmu_->write(address, value);
}

const char *Emulator::disassembleAt(uint16_t address) {
  disasmBuffer_ = cpu_->disassembleAt(address);
  return disasmBuffer_.c_str();
}

uint32_t Emulator::getSoftSwitchState() const {
  const auto &sw = mmu_->getSoftSwitches();
  uint32_t state = 0;

  // Pack soft switch state into a 32-bit value
  if (sw.text)
    state |= (1 << 0);
  if (sw.mixed)
    state |= (1 << 1);
  if (sw.page2)
    state |= (1 << 2);
  if (sw.hires)
    state |= (1 << 3);
  if (sw.col80)
    state |= (1 << 4);
  if (sw.store80)
    state |= (1 << 5);
  if (sw.ramrd)
    state |= (1 << 6);
  if (sw.ramwrt)
    state |= (1 << 7);
  if (sw.altzp)
    state |= (1 << 8);
  if (sw.lcram)
    state |= (1 << 9);
  if (sw.lcram2)
    state |= (1 << 10);
  if (sw.lcwrite)
    state |= (1 << 11);
  if (sw.intcxrom)
    state |= (1 << 12);
  if (sw.slotc3rom)
    state |= (1 << 13);
  if (sw.altCharSet)
    state |= (1 << 14);
  if (sw.an0)
    state |= (1 << 15);
  if (sw.an1)
    state |= (1 << 16);
  if (sw.an2)
    state |= (1 << 17);
  if (sw.an3)
    state |= (1 << 18);

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
