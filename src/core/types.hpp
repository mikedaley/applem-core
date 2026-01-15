#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace a2e {

// Memory size constants
constexpr size_t MAIN_RAM_SIZE = 64 * 1024; // 64KB main RAM
constexpr size_t AUX_RAM_SIZE = 64 * 1024;  // 64KB auxiliary RAM
constexpr size_t ROM_SIZE = 16 * 1024;      // 16KB ROM ($C000-$FFFF)
constexpr size_t CHAR_ROM_SIZE = 4 * 1024;  // 4KB character ROM
constexpr size_t DISK_ROM_SIZE = 256;       // 256 bytes Disk II ROM

// Display constants
constexpr int SCREEN_WIDTH = 560;  // 280 * 2 for double-width pixels
constexpr int SCREEN_HEIGHT = 384; // 192 * 2 for double-height pixels
constexpr int FRAMEBUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT * 4; // RGBA

// Timing constants
constexpr double CPU_CLOCK_HZ = 1023000.0; // 1.023 MHz
constexpr int AUDIO_SAMPLE_RATE = 48000;
constexpr double CYCLES_PER_SAMPLE =
    CPU_CLOCK_HZ / AUDIO_SAMPLE_RATE; // ~21.3125

constexpr int CYCLES_PER_SCANLINE = 65;
constexpr int SCANLINES_PER_FRAME = 262;
constexpr int CYCLES_PER_FRAME =
    CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME; // 17030

// Video mode flags
enum class VideoMode : uint8_t {
  TEXT_40 = 0,
  TEXT_80,
  LORES,
  HIRES,
  DOUBLE_LORES,
  DOUBLE_HIRES
};

// Soft switch state
struct SoftSwitches {
  // Display switches
  bool text = true;        // TEXT/GRAPHICS mode
  bool mixed = false;      // Mixed mode (4 lines text at bottom)
  bool page2 = false;      // PAGE1/PAGE2
  bool hires = false;      // LORES/HIRES
  bool col80 = false;      // 40/80 column mode
  bool altCharSet = false; // Primary/alternate character set

  // Memory switches
  bool store80 = false;   // 80STORE
  bool ramrd = false;     // RAMRD - aux RAM read
  bool ramwrt = false;    // RAMWRT - aux RAM write
  bool altzp = false;     // ALTZP - aux zero page/stack
  bool intcxrom = false;  // INTCXROM - internal slot ROM
  bool slotc3rom = false; // SLOTC3ROM - slot 3 ROM
  bool intc8rom = false;  // Internal $C800-$CFFF ROM active

  // Language card
  bool lcram = false;      // LC RAM enabled for read
  bool lcram2 = false;     // LC RAM bank 2
  bool lcwrite = false;    // LC RAM write-enabled
  bool lcprewrite = false; // LC pre-write state

  // Annunciators
  bool an0 = false;
  bool an1 = false;
  bool an2 = false;
  bool an3 = false;
};

// Disk drive state
struct DriveState {
  bool motorOn = false;
  bool writeMode = false;
  int currentTrack = 0; // 0-34
  int currentPhase = 0; // Stepper motor phase
  int headPosition = 0; // Bit position on track
  uint8_t dataLatch = 0;
  bool diskInserted = false;
};

// CPU status flags are defined in cpu/cpu6502.hpp

// Color palette for Apple II
constexpr std::array<uint32_t, 16> LORES_COLORS = {{
    0xFF000000, // 0: Black
    0xFFDD0033, // 1: Magenta
    0xFF000099, // 2: Dark Blue
    0xFFDD22DD, // 3: Purple
    0xFF007722, // 4: Dark Green
    0xFF555555, // 5: Grey 1
    0xFF2222FF, // 6: Medium Blue
    0xFF66AAFF, // 7: Light Blue
    0xFF885500, // 8: Brown
    0xFFFF6600, // 9: Orange
    0xFFAAAAAA, // 10: Grey 2
    0xFFFF9988, // 11: Pink
    0xFF11DD00, // 12: Light Green
    0xFFFFFF00, // 13: Yellow
    0xFF4FDC4A, // 14: Aqua
    0xFFFFFFFF  // 15: White
}};

// HiRes artifact colors (NTSC-accurate values)
// Group 1 (high bit = 0): Black, Green, Violet, White
// Group 2 (high bit = 1): Black, Orange, Blue, White
constexpr std::array<uint32_t, 6> HIRES_COLORS = {{
    0xFF000000, // 0: Black
    0xFF2FBC1A, // 1: Green (odd pixels, high bit = 0)
    0xFFD93CF0, // 2: Violet (even pixels, high bit = 0)
    0xFFFFFFFF, // 3: White
    0xFF0E5CE8, // 4: Blue (even pixels, high bit = 1)
    0xFFF25006  // 5: Orange (odd pixels, high bit = 1)
}};

// Double Hi-Res color translation table
// Maps raw 4-bit values to correct color indices due to DHGR's bit ordering
constexpr std::array<uint8_t, 16> DHGR_COLOR_TRANSLATE = {{
    0, 1, 8, 9, 4, 5, 12, 13, 2, 3, 10, 11, 6, 7, 14, 15
}};

} // namespace a2e
