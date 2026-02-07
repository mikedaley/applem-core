/*
 * emulator.cpp - Core emulator coordinator tying together CPU, memory, video, audio, and peripherals
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "emulator.hpp"
#include "cards/disk2_card.hpp"
#include "cards/mockingboard_card.hpp"
#include "cards/thunderclock_card.hpp"
#include "cards/mouse_card.hpp"
#include <cstring>

// Include generated ROM data directly
#include "roms.cpp" // namespace roms

namespace a2e {

Emulator::Emulator() {
  mmu_ = std::make_unique<MMU>();
  video_ = std::make_unique<Video>(*mmu_);
  audio_ = std::make_unique<Audio>();
  keyboard_ = std::make_unique<Keyboard>();

  // Create cards, keep raw pointers, then insert into slots
  auto disk = std::make_unique<Disk2Card>();
  auto mb = std::make_unique<MockingboardCard>();
  disk_ = disk.get();
  mockingboard_ = mb.get();

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

  // Wire video subsystem callbacks
  video_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
  mmu_->setVideoSwitchCallback([this]() { video_->onVideoSwitchChanged(); });

  // Wire watchpoint callbacks (MMU -> Emulator)
  mmu_->setWatchpointCallbacks(
    [this](uint16_t addr, uint8_t val) { onWatchpointRead(addr, val); },
    [this](uint16_t addr, uint8_t val) { onWatchpointWrite(addr, val); });

  // Set up Mockingboard callbacks
  mockingboard_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
  mockingboard_->setIRQCallback([this]() { cpu_->irq(); });

  // Set up level-triggered IRQ polling for VIA/mouse interrupts
  cpu_->setIRQStatusCallback([this]() {
    bool active = mockingboard_ ? mockingboard_->isIRQActive() : false;
    if (mouse_) active = active || mouse_->isIRQActive();
    return active;
  });

  // Set up disk timing callback - allows disk reads to get accurate cycle count
  // during instruction execution (before disk_->update() is called)
  disk_->setCycleCallback([this]() { return cpu_->getTotalCycles(); });

  // Insert cards into slots (transfers ownership to MMU)
  mmu_->insertCard(6, std::move(disk));
  mmu_->insertCard(4, std::move(mb));

  // Audio gets raw pointer
  audio_->setMockingboard(mockingboard_);
}

Emulator::~Emulator() = default;

void Emulator::init() {
  // Load system and character ROMs into MMU
  mmu_->loadROM(roms::ROM_SYSTEM, roms::ROM_SYSTEM_SIZE,
                roms::ROM_CHAR, roms::ROM_CHAR_SIZE);

  // Load Disk II ROM into the card
  disk_->loadROM(roms::ROM_DISK2, roms::ROM_DISK2_SIZE);

  reset();
}

void Emulator::reset() {
  mmu_->reset();
  cpu_->resetCycleCount(); // Clear cycle counter for fresh power-on state
  cpu_->reset();
  audio_->reset();
  if (disk_) disk_->reset();
  keyboard_->reset();
  if (mockingboard_) mockingboard_->reset();

  // Clear Apple button states
  setButton(0, false);
  setButton(1, false);

  keyboardLatch_ = 0;
  keyDown_ = false;
  speedMultiplier_ = 1;
  lastFrameCycle_ = 0;
  frameReady_ = false;
  breakpointHit_ = false;
  tempBreakpointActive_ = false;
  tempBreakpoint_ = 0;
  tempBreakpointHit_ = false;
  // Keep beam breakpoints across reset (same as regular breakpoints)
  for (auto& bp : beamBreakpoints_) {
    bp.lastFireFrame = UINT64_MAX;
    bp.lastFireScanline = -1;
  }
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  beamBreakHitScanline_ = -1;
  beamBreakHitHPos_ = -1;
  paused_ = false;

  video_->beginNewFrame(0);
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
  if (disk_) disk_->stopMotor();

  breakpointHit_ = false;
  watchpointHit_ = false;
  tempBreakpointActive_ = false;
  tempBreakpoint_ = 0;
  tempBreakpointHit_ = false;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  beamBreakHitScanline_ = -1;
  beamBreakHitHPos_ = -1;
  // Keep breakpoints but reset per-breakpoint fire tracking
  for (auto& bp : beamBreakpoints_) {
    bp.lastFireFrame = UINT64_MAX;
    bp.lastFireScanline = -1;
  }
  paused_ = false;
}

void Emulator::setPaused(bool paused) {
  if (!paused && paused_ && breakpointHit_) {
    skipBreakpointOnce_ = true;
  }
  if (!paused && paused_ && basicBreakpointHit_) {
    // Skip this BASIC line until we move to a different line
    skipBasicBreakpointLine_ = basicBreakLine_;
  }
  breakpointHit_ = false;
  basicBreakpointHit_ = false;
  watchpointHit_ = false;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  // Reset frame sample counter when unpausing to prevent backlog
  if (!paused && paused_) {
    samplesGenerated_ = 0;
  }
  paused_ = paused;
}

void Emulator::runCycles(int cycles) {
  if (paused_)
    return;

  uint64_t startCycles = cpu_->getTotalCycles();
  uint64_t targetCycles = startCycles + cycles;

  while (cpu_->getTotalCycles() < targetCycles) {
    // Check breakpoints (user breakpoints and temp breakpoint)
    {
      uint16_t pc = cpu_->getPC();

      // Check temp breakpoint (step over / step out)
      if (tempBreakpointActive_ && pc == tempBreakpoint_) {
        tempBreakpointHit_ = true;
        clearTempBreakpoint();
        breakpointHit_ = true;
        breakpointAddress_ = pc;
        paused_ = true;
        return;
      }

      // Check user breakpoints
      if (!breakpoints_.empty()) {
        if (skipBreakpointOnce_) {
          skipBreakpointOnce_ = false;
        } else if (breakpoints_.count(pc) && !disabledBreakpoints_.count(pc)) {
          breakpointHit_ = true;
          breakpointAddress_ = pc;
          paused_ = true;
          return;
        }
      }
    }

    // Track BASIC program running state by monitoring ROM entry points.
    // $D912 (RUN command) = program starting, $D43C (RESTART) = returning to ] prompt.
    // This is definitive because it hooks into the ROM's own execution flow.
    {
      uint16_t pc = cpu_->getPC();
      if (pc == 0xD912 && !basicProgramRunning_) {
        basicProgramRunning_ = true;
      } else if (pc == 0xD43C && basicProgramRunning_) {
        basicProgramRunning_ = false;
      }
    }

    // Check BASIC stepping and breakpoints
    // CURLIN+1 ($76) = $FF means direct/immediate mode (only high byte matters,
    // matching how the ROM checks it at NEWSTT $D7DC: LDX CURLIN+1 / INX / BEQ)
    // Use readRAM to bypass ALTZP switch - BASIC always uses main RAM for zero page
    uint8_t curlinHi = mmu_->readRAM(0x76, false);
    bool basicDirectMode = (curlinHi == 0xFF);
    uint16_t curlin = mmu_->readRAM(0x75, false) | (static_cast<uint16_t>(curlinHi) << 8);

    // Clear skip line when returning to direct mode
    if (basicDirectMode && skipBasicBreakpointLine_ != 0xFFFF) {
      skipBasicBreakpointLine_ = 0xFFFF;
    }

    if (!basicDirectMode) {
      // BASIC line stepping - pause when CURLIN changes
      if (basicStepMode_ == BasicStepMode::Line) {
        if (curlin != basicStepFromLine_) {
          basicStepMode_ = BasicStepMode::None;
          basicBreakpointHit_ = true;
          basicBreakLine_ = curlin;
          paused_ = true;
          return;
        }
      }

      // BASIC statement stepping - pause when TXTPTR crosses a colon (statement separator)
      if (basicStepMode_ == BasicStepMode::Statement) {
        uint16_t txtptr = mmu_->readRAM(0x7A, false) | (mmu_->readRAM(0x7B, false) << 8);

        // Check if line changed (definitely new statement)
        if (curlin != basicStepFromLine_) {
          basicStepMode_ = BasicStepMode::None;
          basicBreakpointHit_ = true;
          basicBreakLine_ = curlin;
          paused_ = true;
          return;
        }

        // Check if TXTPTR has crossed the next colon position
        // This directly detects when we've moved to a new statement
        if (basicStepNextColon_ > 0 && txtptr > basicStepNextColon_) {
          basicStepMode_ = BasicStepMode::None;
          basicBreakpointHit_ = true;
          basicBreakLine_ = curlin;
          paused_ = true;
          return;
        }

        // If there's no next colon (last statement on line), we'll pause when line changes
        // But also pause if TXTPTR has reached end of line (byte 0x00)
        if (basicStepNextColon_ == 0 && basicStepLineStart_ > 0 && txtptr > basicStepFromTxtptr_) {
          // Check if we've reached the end of line marker
          uint8_t currentByte = mmu_->readRAM(txtptr, false);
          if (currentByte == 0x00) {
            // At end of line - will move to next line on next instruction
            // Let the line change check handle it
          }
        }
      }

      // Clear skip-line when we move to a different line
      if (skipBasicBreakpointLine_ != 0xFFFF && curlin != skipBasicBreakpointLine_) {
        skipBasicBreakpointLine_ = 0xFFFF;
      }

      // Check BASIC line breakpoints
      if (!basicBreakpoints_.empty() && basicBreakpoints_.count(curlin)) {
        // Skip if we're stepping or if we're still on the skip line
        if (basicStepMode_ != BasicStepMode::None || curlin == skipBasicBreakpointLine_) {
          // Don't clear skip line here - keep skipping until line changes
        } else {
          basicBreakpointHit_ = true;
          basicBreakLine_ = curlin;
          paused_ = true;
          return;
        }
      }
    }

    // Record trace before execution
    if (traceEnabled_) recordTrace();

    // Track cycles before instruction
    uint64_t cyclesBefore = cpu_->getTotalCycles();

    // Profile: record PC before execution
    uint16_t profilePC = profileEnabled_ ? cpu_->getPC() : 0;

    // Execute one instruction
    cpu_->executeInstruction();

    // Update disk controller with actual instruction cycles
    uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
    if (disk_) disk_->update(static_cast<int>(cyclesUsed));

    // Accumulate cycle profiling
    if (profileEnabled_) {
      profileCycles_[profilePC] += static_cast<uint32_t>(cyclesUsed);
    }

    // Update Mockingboard timers BEFORE next instruction
    // This ensures timer IRQs fire before the CPU can disable them
    if (mockingboard_) mockingboard_->update(static_cast<int>(cyclesUsed));

    // Update mouse card for VBL interrupt detection
    if (mouse_) mouse_->update(static_cast<int>(cyclesUsed));

    // Progressive rendering: render scanlines up to current cycle
    video_->renderUpToCycle(cpu_->getTotalCycles());

    // Check for frame boundary
    uint64_t currentCycle = cpu_->getTotalCycles();
    if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
      // Advance by exactly CYCLES_PER_FRAME to stay aligned with VBL detection
      // ($C019 uses cycles % CYCLES_PER_FRAME). Using currentCycle would drift
      // by a few cycles each frame, desynchronizing raster effects.
      lastFrameCycle_ += CYCLES_PER_FRAME;
      video_->renderFrame();                   // Uses this frame's change log
      video_->beginNewFrame(lastFrameCycle_);   // Reset log, aligned to frame boundary
      frameReady_ = true;
    }

    // Check watchpoint hit (set by MMU callbacks during execution)
    if (watchpointHit_) return;

    // Check beam breakpoints
    if (!beamBreakpoints_.empty()) {
      uint64_t fc = cpu_->getTotalCycles() - lastFrameCycle_;
      if (fc >= CYCLES_PER_FRAME) fc %= CYCLES_PER_FRAME;
      int16_t sl = static_cast<int16_t>(fc / 65);
      int16_t hp = static_cast<int16_t>(fc % 65);
      for (auto& bp : beamBreakpoints_) {
        if (!bp.enabled) continue;
        bool scanOk = (bp.scanline < 0) || (sl == bp.scanline);
        bool hPosOk = (bp.hPos < 0) || (hp >= bp.hPos);
        bool valid = (bp.scanline >= 0 || bp.hPos >= 0);
        if (!scanOk || !hPosOk || !valid) continue;

        // For wildcard-scanline breakpoints (HBLANK, Column), fire once per scanline.
        // For specific-scanline breakpoints (VBL, Scanline, ScanCol), fire once per frame.
        bool alreadyFired;
        if (bp.scanline < 0) {
          alreadyFired = (lastFrameCycle_ == bp.lastFireFrame && sl == bp.lastFireScanline);
        } else {
          alreadyFired = (lastFrameCycle_ == bp.lastFireFrame);
        }
        if (!alreadyFired) {
          beamBreakHit_ = true;
          beamBreakHitId_ = bp.id;
          beamBreakHitScanline_ = sl;
          beamBreakHitHPos_ = hp;
          bp.lastFireFrame = lastFrameCycle_;
          bp.lastFireScanline = sl;
          paused_ = true;
          return;
        }
      }
    }
  }
}

void Emulator::setSpeedMultiplier(int multiplier) {
  if (multiplier < 1) multiplier = 1;
  if (multiplier > 8) multiplier = 8;
  speedMultiplier_ = multiplier;
}

int Emulator::generateStereoAudioSamples(float *buffer, int sampleCount) {
  // Calculate cycles needed for this audio buffer, scaled by speed multiplier
  int cyclesToRun = static_cast<int>(sampleCount * CYCLES_PER_SAMPLE * speedMultiplier_);

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
  if (!disk_) return false;
  return disk_->insertDisk(drive, data, size, filename ? filename : "");
}

bool Emulator::insertBlankDisk(int drive) {
  if (!disk_) return false;
  return disk_->insertBlankDisk(drive);
}

void Emulator::ejectDisk(int drive) {
  if (disk_) disk_->ejectDisk(drive);
}

const uint8_t *Emulator::getDiskData(int drive, size_t *size) const {
  if (!disk_) return nullptr;
  return disk_->getDiskData(drive, size);
}

const uint8_t *Emulator::exportDiskData(int drive, size_t *size) {
  if (!disk_) return nullptr;
  return disk_->exportDiskData(drive, size);
}

const char *Emulator::getDiskFilename(int drive) const {
  if (!disk_) return nullptr;
  const auto *image = disk_->getDiskImage(drive);
  if (!image) {
    return nullptr;
  }
  return image->getFilename().c_str();
}

// ============================================================================
// Beam Position
// ============================================================================

int Emulator::getFrameCycle() const {
  return static_cast<int>(cpu_->getTotalCycles() % CYCLES_PER_FRAME);
}

int Emulator::getBeamScanline() const {
  return getFrameCycle() / CYCLES_PER_SCANLINE;
}

int Emulator::getBeamHPos() const {
  return getFrameCycle() % CYCLES_PER_SCANLINE;
}

int Emulator::getBeamColumn() const {
  int hPos = getBeamHPos();
  return hPos >= 25 ? hPos - 25 : -1;
}

bool Emulator::isInVBL() const {
  return getBeamScanline() >= 192;
}

bool Emulator::isInHBLANK() const {
  return getBeamHPos() < 25;
}

// ============================================================================
// Step Over / Step Out
// ============================================================================

uint16_t Emulator::stepOver() {
  clearTempBreakpoint();
  uint16_t pc = cpu_->getPC();
  uint8_t opcode = mmu_->peek(pc);

  if (opcode == 0x20) {
    // JSR - set temp breakpoint at instruction after JSR (PC + 3)
    uint16_t returnAddr = (pc + 3) & 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else if (opcode == 0x00) {
    // BRK - treat like JSR but with PC+2 as return address
    uint16_t returnAddr = (pc + 2) & 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else {
    // Not a JSR/BRK, just single step
    stepInstruction();
    return 0;
  }
}

uint16_t Emulator::stepOut() {
  clearTempBreakpoint();
  uint8_t sp = cpu_->getSP();
  uint8_t pcl = mmu_->peek(0x0100 + ((sp + 1) & 0xFF));
  uint8_t pch = mmu_->peek(0x0100 + ((sp + 2) & 0xFF));
  // RTS adds 1 to the address
  uint16_t returnAddr = ((pch << 8) | pcl) + 1;

  if (returnAddr > 0 && returnAddr <= 0xFFFF) {
    returnAddr &= 0xFFFF;
    tempBreakpoint_ = returnAddr;
    tempBreakpointActive_ = true;
    setPaused(false);
    return returnAddr;
  } else {
    // Invalid return address, just step
    stepInstruction();
    return 0;
  }
}

void Emulator::clearTempBreakpoint() {
  if (tempBreakpointActive_) {
    tempBreakpointActive_ = false;
    tempBreakpoint_ = 0;
    tempBreakpointHit_ = false;
  }
}

// ============================================================================
// Breakpoints
// ============================================================================

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

// ============================================================================
// BASIC Breakpoints
// ============================================================================

void Emulator::addBasicBreakpoint(uint16_t lineNumber) {
  basicBreakpoints_.insert(lineNumber);
}

void Emulator::removeBasicBreakpoint(uint16_t lineNumber) {
  basicBreakpoints_.erase(lineNumber);
}

void Emulator::clearBasicBreakpoints() {
  basicBreakpoints_.clear();
  basicBreakpointHit_ = false;
}

void Emulator::clearBasicBreakpointHit() {
  basicBreakpointHit_ = false;
  // Don't clear skipBasicBreakpointLine_ here - let it be cleared naturally
  // when CURLIN changes. This allows Run to work from a breakpoint by:
  // 1. setPaused(false) sets skip line
  // 2. clearBasicBreakpointHit() clears step mode but keeps skip
  // 3. Program continues, types RUN, skip cleared when line changes
  basicStepMode_ = BasicStepMode::None;
}

void Emulator::stepBasicLine() {
  // Get current BASIC line (use readRAM to bypass ALTZP)
  uint16_t curlin = mmu_->readRAM(0x75, false) | (mmu_->readRAM(0x76, false) << 8);

  // Set up line stepping mode - will pause when CURLIN changes
  basicStepFromLine_ = curlin;
  basicStepLineStart_ = 0;  // Not used for line stepping, but reset for cleanliness
  basicStepNextColon_ = 0;   // Not used for line stepping
  basicStepMode_ = BasicStepMode::Line;

  // Clear any hit flags, reset sample counter to prevent backlog, and resume
  basicBreakpointHit_ = false;
  samplesGenerated_ = 0;
  paused_ = false;
  basicBreakLine_ = 0;
}

void Emulator::stepBasicStatement() {
  // Get current line and TXTPTR (use readRAM to bypass ALTZP)
  uint16_t curlin = mmu_->readRAM(0x75, false) | (mmu_->readRAM(0x76, false) << 8);
  uint16_t txtptr = mmu_->readRAM(0x7A, false) | (mmu_->readRAM(0x7B, false) << 8);

  // Find line start for the current line
  basicStepLineStart_ = findCurrentLineStart(curlin);

  // Set up statement stepping mode
  basicStepFromLine_ = curlin;
  basicStepFromTxtptr_ = txtptr;

  // Determine current statement index
  if (basicStepLineStart_ == 0 || txtptr < basicStepLineStart_) {
    basicStepFromStmtIndex_ = 0;
  } else {
    basicStepFromStmtIndex_ = countColonsBetween(basicStepLineStart_, txtptr);
  }

  // Find the next colon position after current TXTPTR
  // This is the position we need to cross to reach the next statement
  basicStepNextColon_ = findNextColonAfter(basicStepLineStart_, txtptr);

  basicStepMode_ = BasicStepMode::Statement;

  // Clear any hit flags and resume
  basicBreakpointHit_ = false;
  paused_ = false;
  basicBreakLine_ = 0;
}

uint16_t Emulator::getBasicTxtptr() const {
  // Read TXTPTR respecting current ALTZP state - BASIC writes to whichever
  // bank is active, so we need to read from the same bank
  return mmu_->peek(0x7A) | (mmu_->peek(0x7B) << 8);
}

int Emulator::getBasicStatementIndex() {
  // Use readRAM to bypass ALTZP - BASIC always uses main RAM for zero page
  uint16_t curlin = mmu_->readRAM(0x75, false) | (mmu_->readRAM(0x76, false) << 8);
  uint16_t txtptr = mmu_->readRAM(0x7A, false) | (mmu_->readRAM(0x7B, false) << 8);

  // Find line start for the current line
  uint16_t lineStart = findCurrentLineStart(curlin);

  // If TXTPTR hasn't entered the current line's text area yet, we're at statement 0
  if (lineStart == 0 || txtptr < lineStart) {
    return 0;
  }

  return countColonsBetween(lineStart, txtptr);
}

int Emulator::countColonsBetween(uint16_t lineStart, uint16_t txtptr) {
  // If TXTPTR is at or before line start, we're at statement 0
  if (lineStart == 0 || txtptr <= lineStart) return 0;

  // Count colons from line start to TXTPTR, respecting strings
  // Use readRAM to ensure we read from main RAM where BASIC program is stored
  int colonCount = 0;
  bool inQuote = false;
  bool inRem = false;

  for (uint16_t a = lineStart; a < txtptr; a++) {
    uint8_t byte = mmu_->readRAM(a, false);

    if (byte == 0) break;  // End of line

    if (inRem) continue;  // Skip everything after REM

    if (byte == 0x22) {  // Quote
      inQuote = !inQuote;
      continue;
    }

    if (inQuote) continue;  // Skip string contents

    if (byte == 0xB2) {  // REM token
      inRem = true;
      continue;
    }

    if (byte == 0x3A) {  // Colon
      colonCount++;
    }
  }

  return colonCount;
}

uint16_t Emulator::findNextColonAfter(uint16_t lineStart, uint16_t afterPos) {
  // Find the address of the next colon after afterPos within the line
  // Returns 0 if no colon found (i.e., afterPos is in the last statement)
  if (lineStart == 0) return 0;

  // Start searching from afterPos (or lineStart if afterPos is before it)
  uint16_t searchStart = (afterPos >= lineStart) ? afterPos : lineStart;
  bool inQuote = false;
  bool inRem = false;

  // First, establish quote/REM state at searchStart by scanning from lineStart
  for (uint16_t a = lineStart; a < searchStart; a++) {
    uint8_t byte = mmu_->readRAM(a, false);
    if (byte == 0) return 0;  // Already past end of line
    if (inRem) continue;
    if (byte == 0x22) inQuote = !inQuote;
    if (!inQuote && byte == 0xB2) inRem = true;
  }

  // Now search for the next colon
  for (uint16_t a = searchStart; a < searchStart + 256; a++) {  // Limit search
    uint8_t byte = mmu_->readRAM(a, false);

    if (byte == 0) return 0;  // End of line, no more colons

    if (inRem) continue;

    if (byte == 0x22) {
      inQuote = !inQuote;
      continue;
    }

    if (inQuote) continue;

    if (byte == 0xB2) {
      inRem = true;
      continue;
    }

    if (byte == 0x3A) {  // Found a colon!
      return a;
    }
  }

  return 0;  // No colon found
}

uint16_t Emulator::findCurrentLineStart(uint16_t lineNumber) {
  // Get TXTTAB (start of BASIC program)
  // Use readRAM to bypass ALTZP - BASIC always uses main RAM for zero page
  uint16_t txttab = mmu_->readRAM(0x67, false) | (mmu_->readRAM(0x68, false) << 8);

  if (lineNumber == 0xFFFF) return 0;  // Not running

  // Find the specified line in the program
  uint16_t addr = txttab;

  while (addr < 0xC000) {  // Reasonable upper bound
    uint16_t nextPtr = mmu_->readRAM(addr, false) | (mmu_->readRAM(addr + 1, false) << 8);
    if (nextPtr == 0) break;  // End of program

    uint16_t lineNum = mmu_->readRAM(addr + 2, false) | (mmu_->readRAM(addr + 3, false) << 8);
    if (lineNum == lineNumber) {
      return addr + 4;  // Start of tokenized text (after nextPtr and lineNum)
    }
    addr = nextPtr;
  }

  return 0;  // Line not found
}

// ============================================================================
// Watchpoints
// ============================================================================

void Emulator::addWatchpoint(uint16_t startAddr, uint16_t endAddr, WatchpointType type) {
  watchpoints_.push_back({startAddr, endAddr, type, true});
  watchpointsActive_ = true;
  mmu_->setWatchpointsActive(true);
}

void Emulator::removeWatchpoint(uint16_t startAddr) {
  watchpoints_.erase(
    std::remove_if(watchpoints_.begin(), watchpoints_.end(),
      [startAddr](const Watchpoint& wp) { return wp.startAddr == startAddr; }),
    watchpoints_.end());
  watchpointsActive_ = !watchpoints_.empty();
  mmu_->setWatchpointsActive(watchpointsActive_);
}

void Emulator::clearWatchpoints() {
  watchpoints_.clear();
  watchpointsActive_ = false;
  mmu_->setWatchpointsActive(false);
}

void Emulator::onWatchpointRead(uint16_t address, uint8_t value) {
  if (!watchpointsActive_ || watchpointHit_) return;
  for (const auto& wp : watchpoints_) {
    if (!wp.enabled) continue;
    if ((wp.type & WP_READ) && address >= wp.startAddr && address <= wp.endAddr) {
      watchpointHit_ = true;
      watchpointAddress_ = address;
      watchpointValue_ = value;
      watchpointIsWrite_ = false;
      paused_ = true;
      return;
    }
  }
}

void Emulator::onWatchpointWrite(uint16_t address, uint8_t value) {
  if (!watchpointsActive_ || watchpointHit_) return;
  for (const auto& wp : watchpoints_) {
    if (!wp.enabled) continue;
    if ((wp.type & WP_WRITE) && address >= wp.startAddr && address <= wp.endAddr) {
      watchpointHit_ = true;
      watchpointAddress_ = address;
      watchpointValue_ = value;
      watchpointIsWrite_ = true;
      paused_ = true;
      return;
    }
  }
}

// ============================================================================
// Beam Breakpoints
// ============================================================================

int32_t Emulator::addBeamBreakpoint(int16_t scanline, int16_t hPos) {
  if (beamBreakpoints_.size() >= MAX_BEAM_BREAKPOINTS) return -1;
  int32_t id = beamBreakNextId_++;
  beamBreakpoints_.push_back({scanline, hPos, true, id, UINT64_MAX, -1});
  return id;
}

void Emulator::removeBeamBreakpoint(int32_t id) {
  beamBreakpoints_.erase(
    std::remove_if(beamBreakpoints_.begin(), beamBreakpoints_.end(),
      [id](const BeamBreakpoint& bp) { return bp.id == id; }),
    beamBreakpoints_.end());
}

void Emulator::enableBeamBreakpoint(int32_t id, bool enabled) {
  for (auto& bp : beamBreakpoints_) {
    if (bp.id == id) {
      bp.enabled = enabled;
      return;
    }
  }
}

void Emulator::clearAllBeamBreakpoints() {
  beamBreakpoints_.clear();
  beamBreakNextId_ = 1;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;
  beamBreakHitScanline_ = -1;
  beamBreakHitHPos_ = -1;
}

// ============================================================================
// Trace Log
// ============================================================================

void Emulator::recordTrace() {
  if (traceBuffer_.empty()) {
    traceBuffer_.resize(10000);
  }

  auto& entry = traceBuffer_[traceHead_];
  entry.pc = cpu_->getPC();
  entry.opcode = mmu_->peek(entry.pc);
  entry.a = cpu_->getA();
  entry.x = cpu_->getX();
  entry.y = cpu_->getY();
  entry.sp = cpu_->getSP();
  entry.p = cpu_->getP();

  // Read operands
  entry.instrLen = 1;
  entry.operand1 = 0;
  entry.operand2 = 0;

  // Determine instruction length from opcode
  static const uint8_t instrLengths[256] = {
    1,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    3,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    1,2,1,1,1,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
    1,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,2,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,2,2,2,2,1,3,1,1,3,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
    2,2,1,1,2,2,2,2,1,2,1,1,3,3,3,3,2,2,2,1,1,2,2,2,1,3,1,1,1,3,3,3,
  };

  entry.instrLen = instrLengths[entry.opcode];
  if (entry.instrLen >= 2) entry.operand1 = mmu_->peek(entry.pc + 1);
  if (entry.instrLen >= 3) entry.operand2 = mmu_->peek(entry.pc + 2);

  entry.cycle = static_cast<uint32_t>(cpu_->getTotalCycles());
  entry.padding = 0;

  traceHead_ = (traceHead_ + 1) % traceBuffer_.size();
  if (traceCount_ < traceBuffer_.size()) traceCount_++;
}

void Emulator::stepInstruction() {
  breakpointHit_ = false;
  watchpointHit_ = false;
  beamBreakHit_ = false;
  beamBreakHitId_ = -1;

  // Record trace before execution
  if (traceEnabled_) recordTrace();

  // Track cycles before instruction
  uint64_t cyclesBefore = cpu_->getTotalCycles();

  // Profile: record PC before execution
  uint16_t profilePC = profileEnabled_ ? cpu_->getPC() : 0;

  cpu_->executeInstruction();

  // Update disk controller with actual instruction cycles
  uint64_t cyclesUsed = cpu_->getTotalCycles() - cyclesBefore;
  if (disk_) disk_->update(static_cast<int>(cyclesUsed));

  // Accumulate cycle profiling
  if (profileEnabled_) {
    profileCycles_[profilePC] += static_cast<uint32_t>(cyclesUsed);
  }

  // Update Mockingboard timers
  if (mockingboard_) mockingboard_->update(static_cast<int>(cyclesUsed));

  // Update mouse card
  if (mouse_) mouse_->update(static_cast<int>(cyclesUsed));

  // Progressive rendering: render scanlines up to current cycle
  video_->renderUpToCycle(cpu_->getTotalCycles());

  // Check for frame boundary
  uint64_t currentCycle = cpu_->getTotalCycles();
  if (currentCycle - lastFrameCycle_ >= CYCLES_PER_FRAME) {
    lastFrameCycle_ += CYCLES_PER_FRAME;
    video_->renderFrame();
    video_->beginNewFrame(lastFrameCycle_);
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
// Mouse Input
// ============================================================================

void Emulator::mouseMove(int dx, int dy) {
  if (mouse_) {
    mouse_->addDelta(dx, dy);
  }
}

void Emulator::mouseButton(bool pressed) {
  if (mouse_) {
    mouse_->setMouseButton(pressed);
  }
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

  // Check the slot array for all cards
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
  if (strcmp(name, "Mouse") == 0) {
    return "mouse";
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

  // Before any slot change, clean up existing special card pointers.
  // insertCard() destroys the old card, so dangling pointers must be cleared.
  ExpansionCard* existing = mmu_->getCard(slot);
  if (existing) {
    const char* existingName = existing->getName();
    if (strcmp(existingName, "Mockingboard") == 0 && slot == 4) {
      // Move Mockingboard to storage so it can be restored later
      if (!mbStorage_) {
        mbStorage_ = mmu_->removeCard(4);
        mockingboard_ = nullptr;
        audio_->setMockingboard(nullptr);
      }
    } else if (strcmp(existingName, "Disk II") == 0 && slot == 6) {
      if (!diskStorage_) {
        diskStorage_ = mmu_->removeCard(6);
        disk_ = nullptr;
      }
    } else if (strcmp(existingName, "Mouse") == 0) {
      mouse_ = nullptr;
      mmu_->removeCard(slot);
    } else {
      mmu_->removeCard(slot);
    }
  }

  // Handle empty slot - cleanup above already removed the card
  if (strcmp(cardId, "empty") == 0) {
    return true;
  }

  // Handle Disk II card
  if (strcmp(cardId, "disk2") == 0) {
    if (slot != 6) {
      return false;
    }
    // Re-insert from storage
    if (diskStorage_) {
      disk_ = static_cast<Disk2Card*>(diskStorage_.get());
      mmu_->insertCard(6, std::move(diskStorage_));
    }
    return true;
  }

  // Handle Mockingboard card
  if (strcmp(cardId, "mockingboard") == 0) {
    if (slot != 4) {
      return false;
    }
    // Re-insert from storage
    if (mbStorage_) {
      mockingboard_ = static_cast<MockingboardCard*>(mbStorage_.get());
      mmu_->insertCard(4, std::move(mbStorage_));
      audio_->setMockingboard(mockingboard_);
    }
    return true;
  }

  // Handle Thunderclock card
  if (strcmp(cardId, "thunderclock") == 0) {
    auto card = std::make_unique<ThunderclockCard>();
    mmu_->insertCard(slot, std::move(card));
    return true;
  }

  // Handle Mouse card
  if (strcmp(cardId, "mouse") == 0) {
    auto card = std::make_unique<MouseCard>();
    card->setSlotNumber(slot);
    card->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
    card->setIRQCallback([this]() { cpu_->irq(); });
    mouse_ = card.get();
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

  return mmu_->isSlotEmpty(slot);
}

// ============================================================================
// State Serialization
// ============================================================================

// State format version - increment when format changes
static constexpr uint32_t STATE_VERSION = 7;  // LSS 8-phase clock
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
  stateBuffer_.push_back(disk.getSequencerState());
  stateBuffer_.push_back(disk.getBusData());
  stateBuffer_.push_back(disk.getLSSClock());

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
  size_t mbSize = 0;
  if (mockingboard_) {
    mbSize = mockingboard_->serialize(mbState, sizeof(mbState));
  }
  writeLE16(stateBuffer_, static_cast<uint16_t>(mbSize));
  if (mbSize > 0) {
    stateBuffer_.insert(stateBuffer_.end(), mbState, mbState + mbSize);
  }

  // Expansion card states (slots 1-7, excluding 4 and 6 which are handled above)
  // First, count how many cards have state to save
  uint8_t cardCount = 0;
  for (uint8_t slot = 1; slot <= 7; slot++) {
    if (slot == 4 || slot == 6) continue;  // Handled by legacy system
    ExpansionCard* card = mmu_->getCard(slot);
    if (card && card->getStateSize() > 0) {
      cardCount++;
    }
  }
  stateBuffer_.push_back(cardCount);

  // Save each card's state
  for (uint8_t slot = 1; slot <= 7; slot++) {
    if (slot == 4 || slot == 6) continue;
    ExpansionCard* card = mmu_->getCard(slot);
    if (card && card->getStateSize() > 0) {
      // Slot number
      stateBuffer_.push_back(slot);

      // Card type identifier (use name for identification)
      const char* name = card->getName();
      uint8_t cardType = 0;
      if (strcmp(name, "Thunderclock") == 0) cardType = 1;
      if (strcmp(name, "Mouse") == 0) cardType = 2;
      stateBuffer_.push_back(cardType);

      // Card state
      size_t stateSize = card->getStateSize();
      writeLE16(stateBuffer_, static_cast<uint16_t>(stateSize));
      size_t currentSize = stateBuffer_.size();
      stateBuffer_.resize(currentSize + stateSize);
      card->serialize(stateBuffer_.data() + currentSize, stateSize);
    }
  }

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
  if (offset + 9 > size) return false;
  bool motorOn = data[offset++] != 0;
  int selectedDrive = data[offset++];
  bool q6 = data[offset++] != 0;
  bool q7 = data[offset++] != 0;
  uint8_t phaseStates = data[offset++];
  uint8_t dataLatch = data[offset++];
  uint8_t seqState = data[offset++];
  uint8_t busDataVal = data[offset++];
  uint8_t lssClockVal = data[offset++];

  // Restore disk controller state (including motor for mid-load restores)
  if (disk_) {
    disk_->setMotorOn(motorOn);
    disk_->setSelectedDrive(selectedDrive);
    disk_->setQ6(q6);
    disk_->setQ7(q7);
    disk_->setPhaseStates(phaseStates);
    disk_->setDataLatch(dataLatch);
    disk_->setSequencerState(seqState);
    disk_->setBusData(busDataVal);
    disk_->setLSSClock(lssClockVal);
  }

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
      if (disk_) {
        disk_->insertDisk(drive, diskData, diskSize, filename);

        // Restore track position
        auto *image = disk_->getMutableDiskImage(drive);
        if (image) {
          image->setQuarterTrack(quarterTrack);
        }
      }
    } else {
      // Skip filename length field (will be 0)
      if (offset + 2 <= size) {
        offset += 2;
      }
      // Eject any existing disk
      if (disk_) disk_->ejectDisk(drive);
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
      if (mockingboard_) {
        mockingboard_->deserialize(data + offset, mbSize);
      }
      offset += mbSize;
    }
  }

  // Expansion card states
  if (offset + 1 <= size) {
    uint8_t cardCount = data[offset++];
    for (uint8_t i = 0; i < cardCount && offset + 4 <= size; i++) {
      uint8_t slot = data[offset++];
      uint8_t cardType = data[offset++];
      uint16_t stateSize = readLE16(data + offset);
      offset += 2;

      if (offset + stateSize > size) break;

      // Create card based on type if slot is empty
      if (slot >= 1 && slot <= 7 && slot != 4 && slot != 6) {
        ExpansionCard* existingCard = mmu_->getCard(slot);

        // Create appropriate card type if needed
        if (!existingCard) {
          switch (cardType) {
            case 1: {  // Thunderclock
              auto card = std::make_unique<ThunderclockCard>();
              mmu_->insertCard(slot, std::move(card));
              existingCard = mmu_->getCard(slot);
              break;
            }
            case 2: {  // Mouse
              auto card = std::make_unique<MouseCard>();
              card->setSlotNumber(slot);
              card->setCycleCallback([this]() { return cpu_->getTotalCycles(); });
              card->setIRQCallback([this]() { cpu_->irq(); });
              mouse_ = card.get();
              mmu_->insertCard(slot, std::move(card));
              existingCard = mmu_->getCard(slot);
              break;
            }
          }
        }

        // Restore card state
        if (existingCard) {
          existingCard->deserialize(data + offset, stateSize);
        }
      }

      offset += stateSize;
    }
  }

  frameReady_ = true;
  breakpointHit_ = false;
  paused_ = false;

  return true;
}

// ==========================================================================
// Screen text extraction
// ==========================================================================

// Apple II text screen row base addresses (non-linear memory layout)
static constexpr uint16_t TEXT_ROW_BASES[24] = {
  0x400, 0x480, 0x500, 0x580, 0x600, 0x680, 0x700, 0x780,
  0x428, 0x4A8, 0x528, 0x5A8, 0x628, 0x6A8, 0x728, 0x7A8,
  0x450, 0x4D0, 0x550, 0x5D0, 0x650, 0x6D0, 0x750, 0x7D0
};

int Emulator::screenCodeToAscii(uint8_t code) {
  if (code >= 0xE0) return code - 0x80;        // Normal lowercase
  if (code >= 0xC0) return code - 0x80;        // Normal uppercase / MouseText
  if (code >= 0xA0) return code - 0x80;        // Normal symbols/digits
  if (code >= 0x80) return code - 0x40;        // Normal uppercase @A-Z[\]^_
  if (code >= 0x60) return code - 0x40;        // Flash symbols
  if (code >= 0x40) return code;               // Flash uppercase
  if (code >= 0x20) return code;               // Inverse symbols/digits
  return code + 0x40;                          // Inverse uppercase
}

const char* Emulator::readScreenText(int startRow, int startCol,
                                     int endRow, int endCol) {
  screenTextBuffer_.clear();

  const auto& sw = mmu_->getSoftSwitches();
  if (!sw.text) {
    return screenTextBuffer_.c_str();
  }

  bool col80 = sw.col80;
  bool page2 = sw.page2;
  int cols = col80 ? 80 : 40;
  uint16_t page2Offset = page2 ? 0x400 : 0;

  // Clamp
  if (startRow < 0) startRow = 0;
  if (startCol < 0) startCol = 0;
  if (endRow > 23) endRow = 23;
  if (endCol >= cols) endCol = cols - 1;

  for (int row = startRow; row <= endRow; row++) {
    int colStart = (row == startRow) ? startCol : 0;
    int colEnd = (row == endRow) ? endCol : cols - 1;

    int lineEnd = colEnd; // Track last non-space for trimming

    // First pass: find last non-space character for trimming
    for (int col = colEnd; col >= colStart; col--) {
      uint8_t charCode;
      if (col80) {
        int memCol = col / 2;
        bool isAux = (col % 2) == 0;
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + memCol;
        charCode = isAux ? mmu_->peekAux(addr) : mmu_->peek(addr);
      } else {
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + col;
        charCode = mmu_->peek(addr);
      }
      int ascii = screenCodeToAscii(charCode);
      if (ascii != 0x20) {
        lineEnd = col;
        break;
      }
      if (col == colStart) {
        lineEnd = colStart - 1; // All spaces
      }
    }

    // Second pass: emit characters up to lineEnd
    for (int col = colStart; col <= lineEnd; col++) {
      uint8_t charCode;
      if (col80) {
        int memCol = col / 2;
        bool isAux = (col % 2) == 0;
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + memCol;
        charCode = isAux ? mmu_->peekAux(addr) : mmu_->peek(addr);
      } else {
        uint16_t addr = TEXT_ROW_BASES[row] + page2Offset + col;
        charCode = mmu_->peek(addr);
      }
      int ascii = screenCodeToAscii(charCode);
      screenTextBuffer_ += static_cast<char>(ascii);
    }

    if (row < endRow) {
      screenTextBuffer_ += '\n';
    }
  }

  return screenTextBuffer_.c_str();
}

} // namespace a2e
