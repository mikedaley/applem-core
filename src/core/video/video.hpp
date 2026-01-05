#pragma once

#include "../mmu/mmu.hpp"
#include "../types.hpp"
#include <array>
#include <cstdint>

namespace a2e {

class Video {
public:
  Video(MMU &mmu);

  // Render a complete frame to the framebuffer
  void renderFrame();

  // Get the framebuffer (RGBA, 560x384)
  const uint8_t *getFramebuffer() const { return framebuffer_.data(); }
  uint8_t *getFramebuffer() { return framebuffer_.data(); }

  // Framebuffer size
  static constexpr size_t getFramebufferSize() { return FRAMEBUFFER_SIZE; }

  // Frame dirty flag
  bool isFrameDirty() const { return frameDirty_; }
  void clearFrameDirty() { frameDirty_ = false; }
  void setFrameDirty() { frameDirty_ = true; }

  // Display mode info
  VideoMode getCurrentMode() const;

  // Color settings
  void setMonochrome(bool mono) { monochrome_ = mono; }
  bool isMonochrome() const { return monochrome_; }

  void setGreenPhosphor(bool green) { greenPhosphor_ = green; }
  bool isGreenPhosphor() const { return greenPhosphor_; }

private:
  // Rendering methods for each mode
  void renderText40();
  void renderText80();
  void renderLoRes();
  void renderHiRes();
  void renderDoubleLoRes();
  void renderDoubleHiRes();

  // Mixed mode handling
  void renderMixedMode();

  // Character rendering
  void renderCharacter(int x, int y, uint8_t ch, bool inverse, bool flash);
  void renderCharacter80(int x, int y, uint8_t ch, bool inverse, bool flash);

  // Pixel helpers
  void setPixel(int x, int y, uint32_t color);
  void setPixelScaled(int x, int y, uint32_t color); // 2x scaling

  // Color helpers
  uint32_t getLoResColor(uint8_t colorIndex) const;
  uint32_t getHiResColor(int x, uint8_t pattern, bool highBit) const;
  uint32_t getMonochromeColor(bool on) const;

  // Text screen address calculation
  uint16_t getTextAddress(int row, int col) const;
  uint16_t getHiResAddress(int row, int col) const;

  // Reference to MMU for memory access
  MMU &mmu_;

  // Framebuffer (RGBA, 560x384)
  std::array<uint8_t, FRAMEBUFFER_SIZE> framebuffer_{};

  // Frame state
  bool frameDirty_ = true;

  // Flash state (toggles every ~16 frames)
  int flashCounter_ = 0;
  bool flashState_ = false;
  static constexpr int FLASH_RATE = 16;

  // Display options
  bool monochrome_ = false;
  bool greenPhosphor_ = false;

  // Lookup tables
  static constexpr std::array<int, 24> TEXT_ROW_OFFSETS = {
      {0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
       0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
       0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0}};
};

} // namespace a2e
