#include "video.hpp"
#include <algorithm>
#include <cstring>

namespace a2e {

Video::Video(MMU &mmu) : mmu_(mmu) {
  // Initialize framebuffer to black
  std::memset(framebuffer_.data(), 0, framebuffer_.size());
}

VideoMode Video::getCurrentMode() const {
  const auto &sw = mmu_.getSoftSwitches();

  if (sw.text) {
    return sw.col80 ? VideoMode::TEXT_80 : VideoMode::TEXT_40;
  }

  if (sw.hires) {
    // DHR requires: AN3 OFF (!an3), 80COL on, HIRES on
    if (sw.col80 && !sw.an3) {
      return VideoMode::DOUBLE_HIRES;
    }
    return VideoMode::HIRES;
  }

  // Double LoRes: AN3 OFF (!an3), 80COL on
  if (sw.col80 && !sw.an3) {
    return VideoMode::DOUBLE_LORES;
  }
  return VideoMode::LORES;
}

// ============================================================================
// Raster rendering infrastructure
// ============================================================================

VideoSwitchState Video::captureVideoState() const {
  const auto &sw = mmu_.getSoftSwitches();
  return {sw.text, sw.mixed, sw.page2, sw.hires,
          sw.col80, sw.altCharSet, sw.store80, sw.an3};
}

void Video::onVideoSwitchChanged() {
  VideoSwitchState newState = captureVideoState();

  // Compare against last logged state to avoid redundant entries
  const VideoSwitchState &lastState =
      (switchChangeCount_ > 0) ? switchChanges_[switchChangeCount_ - 1].state
                                : frameStartState_;

  if (std::memcmp(&newState, &lastState, sizeof(VideoSwitchState)) == 0) {
    return; // No actual change
  }

  if (switchChangeCount_ >= MAX_SWITCH_CHANGES) {
    return; // Log full, drop this change
  }

  uint32_t cycleOffset = 0;
  if (cycleCallback_) {
    uint64_t currentCycle = cycleCallback_();
    cycleOffset = static_cast<uint32_t>(currentCycle - frameStartCycle_);
  }

  switchChanges_[switchChangeCount_] = {cycleOffset, newState};
  switchChangeCount_++;
}

void Video::beginNewFrame(uint64_t cycleStart) {
  frameStartCycle_ = cycleStart;
  frameStartState_ = captureVideoState();
  switchChangeCount_ = 0;
}

// ============================================================================
// Character ROM offset helper (deduplicates 40-col and 80-col logic)
// ============================================================================

Video::CharROMInfo Video::getCharROMInfo(uint8_t ch, bool inverse, bool flash,
                                          const VideoSwitchState &vs) const {
  uint16_t romOffset;
  bool needsXor = false;

  if (vs.altCharSet) {
    uint8_t charIndex;
    if (ch >= 0x40 && ch < 0x60) {
      charIndex = ch;
      needsXor = true;
      inverse = false;
    } else if (ch >= 0x60 && ch < 0x80) {
      charIndex = ch;
      needsXor = true;
    } else if (ch < 0x40) {
      charIndex = ch;
      needsXor = false;
    } else {
      if (ch < 0xA0) {
        charIndex = ch & 0x1F;
      } else if (ch < 0xC0) {
        charIndex = (ch & 0x1F) + 32;
      } else if (ch < 0xE0) {
        charIndex = ch & 0x1F;
      } else {
        charIndex = (ch & 0x1F) + 96;
      }
      needsXor = false;
      inverse = false;
    }
    romOffset = charIndex * 8;
  } else {
    uint8_t charIndex;
    if (ch < 0x20) {
      charIndex = ch;
    } else if (ch < 0x40) {
      charIndex = ch;
    } else if (ch < 0x60) {
      charIndex = ch & 0x1F;
    } else if (ch < 0x80) {
      charIndex = (ch & 0x1F) + 32;
    } else if (ch < 0xA0) {
      charIndex = ch & 0x1F;
      inverse = false;
    } else if (ch < 0xC0) {
      charIndex = (ch & 0x1F) + 32;
      inverse = false;
    } else if (ch < 0xE0) {
      charIndex = ch & 0x1F;
      inverse = false;
    } else {
      charIndex = (ch & 0x1F) + 96;
      inverse = false;
    }
    romOffset = charIndex * 8;
    needsXor = false;
  }

  // Apply UK character set offset if enabled
  if (ukCharSet_) {
    romOffset += 0x1000;
  }

  // Handle flash - toggle inverse state when flash is active
  if (flash && flashState_ && !vs.altCharSet) {
    inverse = !inverse;
  }

  return {romOffset, needsXor, inverse};
}

// ============================================================================
// Per-character-line rendering
// ============================================================================

void Video::renderCharacterLine(int col, int textRow, int charLine,
                                 uint8_t ch, bool inverse, bool flash,
                                 const VideoSwitchState &vs, bool is80col) {
  CharROMInfo info = getCharROMInfo(ch, inverse, flash, vs);

  uint32_t fgColor, bgColor;
  if (monochrome_) {
    fgColor = getMonochromeColor(true);
    bgColor = getMonochromeColor(false);
  } else {
    fgColor = 0xFFFFFFFF;
    bgColor = 0xFF000000;
  }

  if (info.inverse) {
    std::swap(fgColor, bgColor);
  }

  uint8_t rowData = mmu_.readCharROM(info.romOffset + charLine);
  if (info.needsXor) {
    rowData ^= 0xFF;
  }

  int screenY = textRow * 16 + charLine * 2;

  if (is80col) {
    // 80-col: col is 0-79, each character is 7 pixels wide (560 total)
    int screenX = col * 7;
    for (int charCol = 0; charCol < 7; charCol++) {
      bool pixelOn = (rowData & (1 << charCol)) != 0;
      uint32_t color = pixelOn ? fgColor : bgColor;
      int px = screenX + charCol;
      setPixel(px, screenY, color);
      setPixel(px, screenY + 1, color);
    }
  } else {
    // 40-col: col is 0-39, each character is 14 pixels wide (560 total)
    int screenX = col * 14;
    for (int charCol = 0; charCol < 7; charCol++) {
      bool pixelOn = (rowData & (1 << charCol)) != 0;
      uint32_t color = pixelOn ? fgColor : bgColor;
      int px = screenX + charCol * 2;
      setPixel(px, screenY, color);
      setPixel(px + 1, screenY, color);
      setPixel(px, screenY + 1, color);
      setPixel(px + 1, screenY + 1, color);
    }
  }
}

// ============================================================================
// Per-scanline segment renderers
// Each renders a column range [startCol, endCol) on a single scanline.
// Columns are byte positions 0-40 matching the hardware's per-cycle reads.
// ============================================================================

void Video::renderText40Scanline(int scanline, int startCol, int endCol,
                                  const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int charLine = scanline % 8;
  if (textRow >= 24) return;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t ch;
    if (vs.page2 && !vs.store80) {
      ch = mmu_.readRAM(addr + 0x0400, false);
    } else {
      ch = mmu_.readRAM(addr, false);
    }

    bool inverse = (ch & 0xC0) == 0x00;
    bool flash = (ch & 0xC0) == 0x40;

    renderCharacterLine(col, textRow, charLine, ch, inverse, flash, vs, false);
  }
}

void Video::renderText80Scanline(int scanline, int startCol, int endCol,
                                  const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int charLine = scanline % 8;
  if (textRow >= 24) return;

  uint16_t pageOffset = (vs.page2 && !vs.store80) ? 0x0400 : 0x0000;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    // Aux memory character (even columns in display)
    uint8_t auxCh = mmu_.readRAM(addr + pageOffset, true);
    bool auxInverse = (auxCh & 0xC0) == 0x00;
    bool auxFlash = (auxCh & 0xC0) == 0x40;
    renderCharacterLine(col * 2, textRow, charLine, auxCh, auxInverse, auxFlash, vs, true);

    // Main memory character (odd columns in display)
    uint8_t mainCh = mmu_.readRAM(addr + pageOffset, false);
    bool mainInverse = (mainCh & 0xC0) == 0x00;
    bool mainFlash = (mainCh & 0xC0) == 0x40;
    renderCharacterLine(col * 2 + 1, textRow, charLine, mainCh, mainInverse, mainFlash, vs, true);
  }
}

void Video::renderLoResScanline(int scanline, int startCol, int endCol,
                                 const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int lineInRow = scanline % 8;
  if (textRow >= 24) return;

  int screenY = scanline * 2;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t colorByte;
    if (vs.page2 && !vs.store80) {
      colorByte = mmu_.readRAM(addr + 0x0400, false);
    } else {
      colorByte = mmu_.readRAM(addr, false);
    }

    uint8_t colorIndex = (lineInRow < 4) ? (colorByte & 0x0F)
                                          : ((colorByte >> 4) & 0x0F);

    uint32_t color = getLoResColor(colorIndex);
    int screenX = col * 14;

    for (int px = 0; px < 14; px++) {
      setPixel(screenX + px, screenY, color);
      setPixel(screenX + px, screenY + 1, color);
    }
  }
}

void Video::renderHiResScanline(int scanline, int startCol, int endCol,
                                 const VideoSwitchState &vs) {
  if (scanline >= 192) return;

  // Build dot/highBit arrays for the columns in our range.
  // Full 280-element arrays are zero-initialized so dots outside
  // our segment read as off — correct behavior at mode boundaries.
  uint8_t dots[280] = {0};
  uint8_t highBits[280] = {0};

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getHiResAddress(scanline, col);

    uint8_t dataByte;
    if (vs.page2 && !vs.store80) {
      dataByte = mmu_.readRAM(addr + 0x2000, false);
    } else {
      dataByte = mmu_.readRAM(addr, false);
    }

    bool highBit = (dataByte & 0x80) != 0;

    for (int bit = 0; bit < 7; bit++) {
      int dotX = col * 7 + bit;
      dots[dotX] = (dataByte & (1 << bit)) ? 1 : 0;
      highBits[dotX] = highBit ? 1 : 0;
    }
  }

  int screenY = scanline * 2;
  int dotStart = startCol * 7;
  int dotEnd = endCol * 7;

  if (monochrome_) {
    for (int x = dotStart; x < dotEnd; x++) {
      uint32_t color = getMonochromeColor(dots[x] != 0);
      int screenX = x * 2;
      setPixel(screenX, screenY, color);
      setPixel(screenX + 1, screenY, color);
      setPixel(screenX, screenY + 1, color);
      setPixel(screenX + 1, screenY + 1, color);
    }
  } else {
    for (int x = dotStart; x < dotEnd; x++) {
      uint32_t color;
      bool highBit = highBits[x] != 0;
      bool dotOn = dots[x] != 0;

      bool prevOn = (x > 0) && dots[x - 1];
      bool nextOn = (x < 279) && dots[x + 1];

      if (!dotOn) {
        bool prev2On = (x > 1) && dots[x - 2];
        bool next2On = (x < 278) && dots[x + 2];

        if (prevOn && nextOn && !prev2On && !next2On) {
          if (highBits[x - 1] == highBits[x + 1]) {
            bool neighborEven = ((x - 1) & 1) == 0;
            bool neighborHighBit = highBits[x - 1];
            if (neighborEven) {
              color = neighborHighBit ? HIRES_COLORS[4] : HIRES_COLORS[2];
            } else {
              color = neighborHighBit ? HIRES_COLORS[5] : HIRES_COLORS[1];
            }
          } else {
            color = HIRES_COLORS[0];
          }
        } else {
          color = HIRES_COLORS[0];
        }
      } else if (prevOn || nextOn) {
        color = HIRES_COLORS[3]; // White
      } else {
        bool evenColumn = (x & 1) == 0;
        if (evenColumn) {
          color = highBit ? HIRES_COLORS[4] : HIRES_COLORS[2];
        } else {
          color = highBit ? HIRES_COLORS[5] : HIRES_COLORS[1];
        }
      }

      int screenX = x * 2;
      setPixel(screenX, screenY, color);
      setPixel(screenX + 1, screenY, color);
      setPixel(screenX, screenY + 1, color);
      setPixel(screenX + 1, screenY + 1, color);
    }
  }
}

void Video::renderDoubleLoResScanline(int scanline, int startCol, int endCol,
                                       const VideoSwitchState &vs) {
  int textRow = scanline / 8;
  int lineInRow = scanline % 8;
  if (textRow >= 24) return;

  int screenY = scanline * 2;

  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getTextAddress(textRow, col);

    uint8_t auxByte = mmu_.readRAM(addr, true);
    uint8_t mainByte = mmu_.readRAM(addr, false);

    uint8_t auxColor = (lineInRow < 4) ? (auxByte & 0x0F)
                                        : ((auxByte >> 4) & 0x0F);
    uint8_t mainColor = (lineInRow < 4) ? (mainByte & 0x0F)
                                         : ((mainByte >> 4) & 0x0F);

    uint32_t auxRGB = monochrome_ ? getMonochromeColor(auxColor != 0) : DLGR_COLORS[auxColor];
    uint32_t mainRGB = monochrome_ ? getMonochromeColor(mainColor != 0) : DLGR_COLORS[mainColor];

    int screenX = col * 14;

    // Aux pixels (left half, 7 pixels wide)
    for (int px = 0; px < 7; px++) {
      setPixel(screenX + px, screenY, auxRGB);
      setPixel(screenX + px, screenY + 1, auxRGB);
    }

    // Main pixels (right half, 7 pixels wide)
    for (int px = 7; px < 14; px++) {
      setPixel(screenX + px, screenY, mainRGB);
      setPixel(screenX + px, screenY + 1, mainRGB);
    }
  }
}

void Video::renderDoubleHiResScanline(int scanline, int startCol, int endCol,
                                       const VideoSwitchState &vs) {
  static const uint32_t DHGR_COLORS[16] = {
    0xFF000000, 0xFF9F1B48, 0xFF496500, 0xFFD87300,
    0xFF197544, 0xFF818181, 0xFF3CCC00, 0xFFBCD600,
    0xFF4832EB, 0xFFD643FF, 0xFF818181, 0xFFFB8FBC,
    0xFF3692FF, 0xFFB89EFF, 0xFF6CE6B8, 0xFFF1F1F1
  };

  if (scanline >= 192) return;

  uint16_t pageOffset = (vs.page2 && !vs.store80) ? 0x2000 : 0;

  // Read bytes for columns in our range (zero-init for edge handling)
  uint8_t line[80] = {0};
  for (int col = startCol; col < endCol; col++) {
    uint16_t addr = getHiResAddress(scanline, col) + pageOffset;
    line[col * 2] = mmu_.readRAM(addr, true);
    line[col * 2 + 1] = mmu_.readRAM(addr, false);
  }

  // Extract dots for our column range
  uint8_t dots[564] = {0};
  int dotStart = startCol * 14;
  int dotEnd = std::min(endCol * 14, 560);

  for (int i = dotStart; i < dotEnd; i++) {
    int byteIdx = i / 7;
    int bitIdx = i % 7;
    dots[i] = (line[byteIdx] >> bitIdx) & 1;
  }

  int screenY = scanline * 2;

  if (monochrome_) {
    for (int i = dotStart; i < dotEnd; i++) {
      uint32_t color = getMonochromeColor(dots[i] != 0);
      setPixel(i, screenY, color);
      setPixel(i, screenY + 1, color);
    }
  } else {
    for (int i = dotStart; i < dotEnd; i++) {
      int alignedBase = (i / 4) * 4;
      uint8_t colorIdx = (dots[alignedBase] << 3) |
                          (dots[alignedBase + 1] << 2) |
                          (dots[alignedBase + 2] << 1) |
                          dots[alignedBase + 3];

      uint32_t color = DHGR_COLORS[colorIdx];
      setPixel(i, screenY, color);
      setPixel(i, screenY + 1, color);
    }
  }
}

// ============================================================================
// Scanline segment dispatcher
// ============================================================================

void Video::renderScanlineSegment(int scanline, int startCol, int endCol,
                                   const VideoSwitchState &vs) {
  if (scanline >= 192 || startCol >= endCol) return;

  // Mixed mode: scanlines 160-191 always render as text
  if (vs.mixed && scanline >= 160 && !vs.text) {
    if (vs.col80) {
      renderText80Scanline(scanline, startCol, endCol, vs);
    } else {
      renderText40Scanline(scanline, startCol, endCol, vs);
    }
    return;
  }

  if (vs.text) {
    if (vs.col80) {
      renderText80Scanline(scanline, startCol, endCol, vs);
    } else {
      renderText40Scanline(scanline, startCol, endCol, vs);
    }
  } else if (vs.hires) {
    if (vs.col80 && !vs.an3) {
      renderDoubleHiResScanline(scanline, startCol, endCol, vs);
    } else {
      renderHiResScanline(scanline, startCol, endCol, vs);
    }
  } else {
    if (vs.col80 && !vs.an3) {
      renderDoubleLoResScanline(scanline, startCol, endCol, vs);
    } else {
      renderLoResScanline(scanline, startCol, endCol, vs);
    }
  }
}

// ============================================================================
// Frame rendering
// ============================================================================

void Video::renderFrame() {
  // Update flash state
  flashCounter_++;
  if (flashCounter_ >= FLASH_RATE) {
    flashCounter_ = 0;
    flashState_ = !flashState_;
  }

  // Apple IIe horizontal timing: each 65-cycle scanline starts with
  // 25 cycles of horizontal blanking, then 40 cycles of visible display.
  // Cycles 0-24: hblank, cycles 25-64: visible columns 0-39.
  static constexpr int HBLANK_CYCLES = 25;

  if (switchChangeCount_ == 0) {
    // Fast path: single mode for entire frame
    for (int s = 0; s < 192; s++) {
      renderScanlineSegment(s, 0, 40, frameStartState_);
    }
  } else {
    // Slow path: walk changes at cycle granularity, splitting within scanlines
    VideoSwitchState currentState = frameStartState_;
    int changeIdx = 0;

    for (int scanline = 0; scanline < 192; scanline++) {
      uint32_t scanlineStartCycle = scanline * CYCLES_PER_SCANLINE;
      uint32_t visibleStartCycle = scanlineStartCycle + HBLANK_CYCLES;
      uint32_t scanlineEndCycle = scanlineStartCycle + CYCLES_PER_SCANLINE;

      // Phase 1: Consume hblank changes (cycles 0-24) and any earlier changes
      while (changeIdx < switchChangeCount_) {
        uint32_t changeCycle = switchChanges_[changeIdx].cycleOffset;
        if (changeCycle >= visibleStartCycle) {
          break; // Change is in visible area or a later scanline
        }
        currentState = switchChanges_[changeIdx].state;
        changeIdx++;
      }

      // Phase 2: Process visible-area changes (cycles 25-64 → columns 0-39)
      int col = 0;
      while (changeIdx < switchChangeCount_) {
        uint32_t changeCycle = switchChanges_[changeIdx].cycleOffset;
        if (changeCycle >= scanlineEndCycle) {
          break; // Belongs to a later scanline
        }

        int changeCol = static_cast<int>(changeCycle - visibleStartCycle);
        if (changeCol > 40) changeCol = 40;

        if (changeCol > col) {
          renderScanlineSegment(scanline, col, changeCol, currentState);
          col = changeCol;
        }

        currentState = switchChanges_[changeIdx].state;
        changeIdx++;
      }

      // Render remaining visible columns
      if (col < 40) {
        renderScanlineSegment(scanline, col, 40, currentState);
      }
    }
  }

  frameDirty_ = true;
}

// ============================================================================
// Pixel and color helpers
// ============================================================================

void Video::setPixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
    return;
  }

  size_t offset = (y * SCREEN_WIDTH + x) * 4;
  framebuffer_[offset + 0] = (color >> 16) & 0xFF; // R
  framebuffer_[offset + 1] = (color >> 8) & 0xFF;  // G
  framebuffer_[offset + 2] = color & 0xFF;         // B
  framebuffer_[offset + 3] = (color >> 24) & 0xFF; // A
}

uint32_t Video::getLoResColor(uint8_t colorIndex) const {
  if (monochrome_) {
    return (colorIndex > 0) ? getMonochromeColor(true)
                            : getMonochromeColor(false);
  }
  return LORES_COLORS[colorIndex & 0x0F];
}

uint32_t Video::getMonochromeColor(bool on) const {
  if (!on) {
    return 0xFF000000; // Black
  }

  if (greenPhosphor_) {
    return 0xFF33FF33; // Green phosphor
  }
  return 0xFFFFFFFF; // White
}

uint16_t Video::getTextAddress(int row, int col) const {
  return 0x0400 + TEXT_ROW_OFFSETS[row] + col;
}

uint16_t Video::getHiResAddress(int row, int col) const {
  int block = row / 8;
  int line = row % 8;
  return 0x2000 + TEXT_ROW_OFFSETS[block] + line * 0x400 + col;
}

} // namespace a2e
