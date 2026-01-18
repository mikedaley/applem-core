#pragma once

#include "../types.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace a2e {

// Forward declarations
class Disk2Controller;

class MMU {
public:
  using KeyboardCallback = std::function<uint8_t()>;
  using KeyStrobeCallback = std::function<void()>;
  using SpeakerCallback = std::function<void()>;
  using ButtonCallback = std::function<uint8_t(int)>; // Returns button state for button 0-2
  using CycleCallback = std::function<uint64_t()>;    // Returns current CPU cycle count

  MMU();

  // Memory access
  uint8_t read(uint16_t address);
  void write(uint16_t address, uint8_t value);

  // Non-side-effecting read for debugger/memory viewer
  uint8_t peek(uint16_t address) const;

  // Non-side-effecting read of auxiliary memory (for text selection in 80-col mode)
  uint8_t peekAux(uint16_t address) const;

  // Direct memory access (bypasses soft switches)
  uint8_t readRAM(uint16_t address, bool aux = false) const;
  void writeRAM(uint16_t address, uint8_t value, bool aux = false);

  // ROM loading - combined 16KB system ROM ($C000-$FFFF)
  void loadROM(const uint8_t *systemRom, size_t systemSize,
               const uint8_t *charRom, size_t charSize, const uint8_t *diskRom,
               size_t diskSize);

  // Character ROM access (for video)
  uint8_t readCharROM(uint16_t address) const;

  // Soft switch state
  const SoftSwitches &getSoftSwitches() const { return switches_; }

  // Callbacks
  void setKeyboardCallback(KeyboardCallback cb) {
    keyboardCallback_ = std::move(cb);
  }
  void setKeyStrobeCallback(KeyStrobeCallback cb) {
    keyStrobeCallback_ = std::move(cb);
  }
  void setSpeakerCallback(SpeakerCallback cb) {
    speakerCallback_ = std::move(cb);
  }
  void setButtonCallback(ButtonCallback cb) { buttonCallback_ = std::move(cb); }
  void setCycleCallback(CycleCallback cb) { cycleCallback_ = std::move(cb); }

  // Peripheral connections
  void setDiskController(Disk2Controller *disk) { diskController_ = disk; }

  // Reset
  void reset();

  // Memory access tracking for debugger heat map
  void enableTracking(bool enable) { trackingEnabled_ = enable; }
  bool isTrackingEnabled() const { return trackingEnabled_; }
  void clearTracking();
  void decayTracking(uint8_t amount = 1); // Reduce all counts for real-time visualization
  const uint8_t* getReadCounts() const { return readCounts_.data(); }
  const uint8_t* getWriteCounts() const { return writeCounts_.data(); }

private:
  // Soft switch handling
  uint8_t readSoftSwitch(uint16_t address);
  uint8_t peekSoftSwitch(uint16_t address) const;
  void writeSoftSwitch(uint16_t address, uint8_t value);

  // Floating bus - returns value video hardware is currently reading
  uint8_t getFloatingBusValue();

  // Language card logic
  uint8_t readLanguageCard(uint16_t address);
  void writeLanguageCard(uint16_t address, uint8_t value);
  uint8_t handleLanguageCardSwitch(uint8_t reg);
  void handleLanguageCardSwitchWrite(uint8_t reg);

  // Memory banks
  std::array<uint8_t, MAIN_RAM_SIZE> mainRAM_{};
  std::array<uint8_t, AUX_RAM_SIZE> auxRAM_{};

  // Language card RAM banks
  std::array<uint8_t, 0x1000> lcBank1_{};   // $D000-$DFFF bank 1
  std::array<uint8_t, 0x1000> lcBank2_{};   // $D000-$DFFF bank 2
  std::array<uint8_t, 0x2000> lcHighRAM_{}; // $E000-$FFFF

  // Auxiliary language card banks
  std::array<uint8_t, 0x1000> auxLcBank1_{};
  std::array<uint8_t, 0x1000> auxLcBank2_{};
  std::array<uint8_t, 0x2000> auxLcHighRAM_{};

  // ROM - combined 16KB system ROM ($C000-$FFFF)
  std::array<uint8_t, 0x4000> systemROM_{}; // $C000-$FFFF (16KB)
  std::array<uint8_t, CHAR_ROM_SIZE> charROM_{};
  std::array<uint8_t, DISK_ROM_SIZE> diskROM_{};

  // Soft switches
  SoftSwitches switches_;

  // Keyboard state
  uint8_t keyboardLatch_ = 0;

  // Paddle/joystick state
  std::array<uint8_t, 4> paddleValues_ = {128, 128, 128, 128}; // Centered by default
  uint64_t paddleTriggerCycle_ = 0;
  static constexpr int PADDLE_CYCLES_PER_UNIT = 11; // ~11 cycles per paddle unit

  // Callbacks
  KeyboardCallback keyboardCallback_;
  KeyStrobeCallback keyStrobeCallback_;
  SpeakerCallback speakerCallback_;
  ButtonCallback buttonCallback_;
  CycleCallback cycleCallback_;

  // Peripherals
  Disk2Controller *diskController_ = nullptr;

  // Memory access tracking for debugger heat map
  bool trackingEnabled_ = false;
  std::array<uint8_t, 65536> readCounts_{};
  std::array<uint8_t, 65536> writeCounts_{};
};

} // namespace a2e
