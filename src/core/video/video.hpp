/*
 * video.hpp - Video output generation
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "../mmu/mmu.hpp"
#include "../types.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace a2e {

class Video {
public:
  using CycleCallback = std::function<uint64_t()>;

  Video(MMU &mmu);

  // Render a complete frame to the framebuffer
  void renderFrame();

  // Force a full frame render from current memory using current video switch
  // state, independent of beam position or CPU cycle count. Useful for
  // debugger screen refresh after stepping.
  void forceRenderFrame();

  // Progressive rendering: render all scanlines up to the current CPU cycle
  void renderUpToCycle(uint64_t currentCycle);

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

  // UK character set (like the physical switch on UK Apple IIe)
  void setUKCharacterSet(bool uk) { ukCharSet_ = uk; }
  bool isUKCharacterSet() const { return ukCharSet_; }

  // Cycle callback for determining current position within frame
  void setCycleCallback(CycleCallback cb) { cycleCallback_ = std::move(cb); }

  // Called by MMU callback when a video-relevant soft switch changes
  void onVideoSwitchChanged();

  // Called at frame boundaries to reset the change log and snapshot state
  void beginNewFrame(uint64_t cycleStart);

private:
  // Per-scanline segment rendering (column range within a single scanline)
  // startCol/endCol are byte positions 0-40 (one per CPU cycle in visible area)
  void renderText40Scanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void renderText80Scanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void renderLoResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void renderHiResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void renderDoubleLoResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);
  void renderDoubleHiResScanline(int scanline, int startCol, int endCol, const VideoSwitchState& vs);

  // Dispatch a scanline segment to the correct mode renderer, handling mixed mode
  void renderScanlineSegment(int scanline, int startCol, int endCol, const VideoSwitchState& vs);

  // Render a single scanline using the switch change log (progressive rendering)
  void renderScanlineWithChanges(int scanline);

  // Character rendering — single character row (1 ROM line → 2 framebuffer rows)
  void renderCharacterLine(int col, int textRow, int charLine,
                           uint8_t ch, bool inverse, bool flash,
                           const VideoSwitchState& vs, bool is80col);

  // Character ROM offset helper (shared between 40-col and 80-col paths)
  struct CharROMInfo {
    uint16_t romOffset;
    bool needsXor;
    bool inverse;
  };
  CharROMInfo getCharROMInfo(uint8_t ch, bool inverse, bool flash,
                             const VideoSwitchState& vs) const;

  // Capture current video switch state from MMU
  VideoSwitchState captureVideoState() const;

  // Pixel helpers
  void setPixel(int x, int y, uint32_t color);

  // Color helpers
  uint32_t getLoResColor(uint8_t colorIndex) const;
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
  bool ukCharSet_ = false;  // UK character set switch

  // Cycle callback for position calculation
  CycleCallback cycleCallback_;

  // Per-scanline rendering: frame start cycle and switch change log
  uint64_t frameStartCycle_ = 0;
  VideoSwitchState frameStartState_{};

  static constexpr int MAX_SWITCH_CHANGES = 1024;
  std::array<VideoSwitchChange, MAX_SWITCH_CHANGES> switchChanges_;
  int switchChangeCount_ = 0;

  // Progressive per-scanline rendering state
  int lastRenderedScanline_ = -1;   // -1 means no scanlines rendered yet this frame
  int changeIdx_ = 0;               // Current position in switch change log
  VideoSwitchState currentRenderState_{}; // Current video state for progressive rendering

  // Lookup tables
  static constexpr std::array<int, 24> TEXT_ROW_OFFSETS = {
      {0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
       0x028, 0x0A8, 0x128, 0x1A8, 0x228, 0x2A8, 0x328, 0x3A8,
       0x050, 0x0D0, 0x150, 0x1D0, 0x250, 0x2D0, 0x350, 0x3D0}};
};

} // namespace a2e
