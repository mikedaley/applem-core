#pragma once

#include "audio/audio.hpp"
#include "cpu/cpu6502.hpp"
#include "disk/disk2.hpp"
#include "input/keyboard.hpp"
#include "mockingboard/mockingboard.hpp"
#include "mmu/mmu.hpp"
#include "types.hpp"
#include "video/video.hpp"
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace a2e {

class Emulator {
public:
  Emulator();
  ~Emulator();

  // Initialization
  void init();
  void reset();     // Cold reset - clears memory
  void warmReset(); // Warm reset - CPU only, preserves memory

  // Execution
  void runCycles(int cycles);
  int generateAudioSamples(float *buffer, int sampleCount);

  // Audio-driven frame synchronization
  // Returns number of complete frames worth of samples generated since last
  // call
  int consumeFrameSamples();

  // Frame management
  bool isFrameReady() const { return frameReady_; }
  void clearFrameReady() { frameReady_ = false; }
  const uint8_t *getFramebuffer() const;
  size_t getFramebufferSize() const { return FRAMEBUFFER_SIZE; }

  // Input - raw browser keycodes (preferred)
  int handleRawKeyDown(int browserKeycode, bool shift, bool ctrl, bool alt,
                       bool meta, bool capsLock);
  void handleRawKeyUp(int browserKeycode, bool shift, bool ctrl, bool alt,
                      bool meta);

  // Input - direct Apple II keycodes (for paste functionality)
  void keyDown(int keycode);
  void keyUp(int keycode);
  void setButton(int button, bool pressed);  // Set button state (0=Open Apple, 1=Closed Apple, 2=Button2)
  void setPaddleValue(int paddle, int value);  // Set paddle value (0-3, value 0-255)
  int getPaddleValue(int paddle) const;  // Get paddle value (0-3)
  bool isKeyboardReady() const { return (keyboardLatch_ & 0x80) == 0; }  // True if strobe cleared

  // Disk management
  bool insertDisk(int drive, const uint8_t *data, size_t size,
                  const char *filename);
  bool insertBlankDisk(int drive);
  void ejectDisk(int drive);
  const uint8_t *getDiskData(int drive, size_t *size) const;
  const uint8_t *exportDiskData(int drive, size_t *size);
  const char *getDiskFilename(int drive) const;

  // Debugger interface
  void addBreakpoint(uint16_t address);
  void removeBreakpoint(uint16_t address);
  void enableBreakpoint(uint16_t address, bool enabled);
  bool isBreakpointHit() const { return breakpointHit_; }
  uint16_t getBreakpointAddress() const { return breakpointAddress_; }

  // CPU state access
  uint16_t getPC() const { return cpu_->getPC(); }
  uint8_t getSP() const { return cpu_->getSP(); }
  uint8_t getA() const { return cpu_->getA(); }
  uint8_t getX() const { return cpu_->getX(); }
  uint8_t getY() const { return cpu_->getY(); }
  uint8_t getP() const { return cpu_->getP(); }
  uint64_t getTotalCycles() const { return cpu_->getTotalCycles(); }

  // Pause/resume
  bool isPaused() const { return paused_; }
  void setPaused(bool paused) { paused_ = paused; }

  // Single step
  void stepInstruction();

  // Memory access
  uint8_t readMemory(uint16_t address) const;
  uint8_t peekMemory(uint16_t address) const; // Non-side-effecting read for debugger
  void writeMemory(uint16_t address, uint8_t value);

  // Disassembly
  const char *disassembleAt(uint16_t address);

  // Soft switch state (64-bit packed state)
  uint64_t getSoftSwitchState() const;

  // State serialization for save/restore
  // Returns pointer to state data and sets size. Caller does not own the pointer.
  const uint8_t *exportState(size_t *size);
  // Restores state from data. Returns true on success.
  bool importState(const uint8_t *data, size_t size);

  // Components access
  MMU &getMMU() { return *mmu_; }
  Video &getVideo() { return *video_; }
  Audio &getAudio() { return *audio_; }
  Disk2Controller &getDisk() { return *disk_; }
  Mockingboard &getMockingboard() { return *mockingboard_; }

private:
  // Memory callbacks for CPU
  uint8_t cpuRead(uint16_t address);
  void cpuWrite(uint16_t address, uint8_t value);

  // Keyboard handling
  uint8_t getKeyboardData();
  void clearKeyboardStrobe();

  // Speaker callback
  void toggleSpeaker();

  // Components
  std::unique_ptr<MMU> mmu_;
  std::unique_ptr<CPU6502> cpu_;
  std::unique_ptr<Video> video_;
  std::unique_ptr<Audio> audio_;
  std::unique_ptr<Disk2Controller> disk_;
  std::unique_ptr<Keyboard> keyboard_;
  std::unique_ptr<Mockingboard> mockingboard_;

  // Keyboard state
  uint8_t keyboardLatch_ = 0;
  bool keyDown_ = false;

  // Button state (Open Apple, Closed Apple, Button 2)
  bool buttonState_[3] = {false, false, false};
  uint8_t getButtonState(int button);

  // Frame timing
  uint64_t lastFrameCycle_ = 0;
  bool frameReady_ = false;

  // Audio-driven frame sync
  static constexpr int SAMPLES_PER_FRAME = 800; // 48000 Hz / 60 Hz
  int samplesGenerated_ = 0;

  // Debugger state
  std::set<uint16_t> breakpoints_;
  std::set<uint16_t> disabledBreakpoints_;
  bool breakpointHit_ = false;
  uint16_t breakpointAddress_ = 0;
  bool paused_ = false;

  // Disassembly buffer
  mutable std::string disasmBuffer_;

  // State serialization buffer
  mutable std::vector<uint8_t> stateBuffer_;
};

} // namespace a2e
