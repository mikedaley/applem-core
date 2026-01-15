#include "video.hpp"
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
    if (sw.col80 && sw.an3) {
      return VideoMode::DOUBLE_HIRES;
    }
    return VideoMode::HIRES;
  }

  if (sw.col80 && sw.an3) {
    return VideoMode::DOUBLE_LORES;
  }
  return VideoMode::LORES;
}

void Video::renderFrame() {
  const auto &sw = mmu_.getSoftSwitches();

  // Update flash state
  flashCounter_++;
  if (flashCounter_ >= FLASH_RATE) {
    flashCounter_ = 0;
    flashState_ = !flashState_;
  }

  if (sw.text) {
    // Text mode
    if (sw.col80) {
      renderText80();
    } else {
      renderText40();
    }
  } else if (sw.hires) {
    // Hi-res graphics
    if (sw.col80 && sw.an3) {
      renderDoubleHiRes();
    } else {
      renderHiRes();
    }

    // Mixed mode - render text at bottom
    if (sw.mixed) {
      renderMixedMode();
    }
  } else {
    // Lo-res graphics
    if (sw.col80 && sw.an3) {
      renderDoubleLoRes();
    } else {
      renderLoRes();
    }

    // Mixed mode - render text at bottom
    if (sw.mixed) {
      renderMixedMode();
    }
  }

  frameDirty_ = true;
}

void Video::renderMixedMode() {
  const auto &sw = mmu_.getSoftSwitches();

  // Render bottom 4 lines (rows 20-23) as text
  for (int row = 20; row < 24; row++) {
    for (int col = 0; col < 40; col++) {
      uint16_t addr = getTextAddress(row, col);

      // Determine which memory page
      bool useAux = sw.store80 && sw.page2;
      uint8_t ch;
      if (useAux) {
        ch = mmu_.readRAM(addr, true);
      } else if (sw.page2 && !sw.store80) {
        ch = mmu_.readRAM(addr + 0x0400, false);
      } else {
        ch = mmu_.readRAM(addr, false);
      }

      // Determine character attributes
      bool inverse = (ch & 0xC0) == 0x00;
      bool flash = (ch & 0xC0) == 0x40;

      if (sw.col80) {
        // 80-column mode - also render aux character
        uint8_t auxCh = mmu_.readRAM(addr, true);
        bool auxInverse = (auxCh & 0xC0) == 0x00;
        bool auxFlash = (auxCh & 0xC0) == 0x40;
        renderCharacter80(col * 2, row, auxCh, auxInverse, auxFlash);
        renderCharacter80(col * 2 + 1, row, ch, inverse, flash);
      } else {
        renderCharacter(col, row, ch, inverse, flash);
      }
    }
  }
}

void Video::renderText40() {
  const auto &sw = mmu_.getSoftSwitches();

  for (int row = 0; row < 24; row++) {
    for (int col = 0; col < 40; col++) {
      uint16_t addr = getTextAddress(row, col);

      // Determine which memory page
      uint8_t ch;
      if (sw.page2 && !sw.store80) {
        ch = mmu_.readRAM(addr + 0x0400, false);
      } else {
        ch = mmu_.readRAM(addr, false);
      }

      // Character attributes
      bool inverse = (ch & 0xC0) == 0x00;
      bool flash = (ch & 0xC0) == 0x40;

      renderCharacter(col, row, ch, inverse, flash);
    }
  }
}

void Video::renderText80() {
  const auto &sw = mmu_.getSoftSwitches();

  for (int row = 0; row < 24; row++) {
    for (int col = 0; col < 40; col++) {
      uint16_t addr = getTextAddress(row, col);

      // Main memory character (odd columns in display)
      uint8_t mainCh;
      if (sw.page2 && !sw.store80) {
        mainCh = mmu_.readRAM(addr + 0x0400, false);
      } else {
        mainCh = mmu_.readRAM(addr, false);
      }

      // Aux memory character (even columns in display)
      uint8_t auxCh = mmu_.readRAM(addr, true);

      // Render aux character first (even column)
      bool auxInverse = (auxCh & 0xC0) == 0x00;
      bool auxFlash = (auxCh & 0xC0) == 0x40;
      renderCharacter80(col * 2, row, auxCh, auxInverse, auxFlash);

      // Render main character (odd column)
      bool mainInverse = (mainCh & 0xC0) == 0x00;
      bool mainFlash = (mainCh & 0xC0) == 0x40;
      renderCharacter80(col * 2 + 1, row, mainCh, mainInverse, mainFlash);
    }
  }
}

void Video::renderLoRes() {
  const auto &sw = mmu_.getSoftSwitches();

  for (int row = 0; row < 24; row++) {
    // In mixed mode, only render graphics for top 20 rows
    if (sw.mixed && row >= 20)
      break;

    for (int col = 0; col < 40; col++) {
      uint16_t addr = getTextAddress(row, col);

      uint8_t colorByte;
      if (sw.page2 && !sw.store80) {
        colorByte = mmu_.readRAM(addr + 0x0400, false);
      } else {
        colorByte = mmu_.readRAM(addr, false);
      }

      // Each byte contains two 4-bit colors (top and bottom halves)
      uint8_t topColor = colorByte & 0x0F;
      uint8_t bottomColor = (colorByte >> 4) & 0x0F;

      uint32_t topRGB = getLoResColor(topColor);
      uint32_t bottomRGB = getLoResColor(bottomColor);

      // Each lo-res "pixel" is 14x8 screen pixels (with 2x scaling: 14x16)
      int screenX = col * 14;
      int screenY = row * 16;

      // Top half
      for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 14; px++) {
          setPixel(screenX + px, screenY + py, topRGB);
        }
      }

      // Bottom half
      for (int py = 8; py < 16; py++) {
        for (int px = 0; px < 14; px++) {
          setPixel(screenX + px, screenY + py, bottomRGB);
        }
      }
    }
  }
}

void Video::renderHiRes() {
  const auto &sw = mmu_.getSoftSwitches();

  int maxRow = sw.mixed ? 160 : 192;

  for (int row = 0; row < maxRow; row++) {
    // Build scanline array: 280 dots across (40 bytes × 7 bits)
    uint8_t dots[280] = {0};
    uint8_t highBits[280] = {0}; // High bit expanded per dot for easy lookup

    for (int col = 0; col < 40; col++) {
      uint16_t addr = getHiResAddress(row, col);

      uint8_t dataByte;
      if (sw.page2 && !sw.store80) {
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

    int screenY = row * 2;

    if (monochrome_) {
      // Monochrome mode: render each dot individually
      for (int dotX = 0; dotX < 280; dotX++) {
        uint32_t color = getMonochromeColor(dots[dotX] != 0);
        int screenX = dotX * 2;
        setPixel(screenX, screenY, color);
        setPixel(screenX + 1, screenY, color);
        setPixel(screenX, screenY + 1, color);
        setPixel(screenX + 1, screenY + 1, color);
      }
    } else {
      // Color mode: process each byte separately for consistent colors
      // This avoids byte-boundary color shifting issues
      for (int byteIdx = 0; byteIdx < 40; byteIdx++) {
        int baseX = byteIdx * 7;
        bool highBit = highBits[baseX] != 0;

        // Get the 7 dots for this byte
        bool byteDots[7];
        for (int i = 0; i < 7; i++) {
          byteDots[i] = dots[baseX + i] != 0;
        }

        // Determine color for each dot based on local pattern
        for (int i = 0; i < 7; i++) {
          uint32_t color;
          int x = baseX + i;

          bool dotOn = byteDots[i];
          bool prevOn = (i > 0) ? byteDots[i - 1] : ((byteIdx > 0) && dots[baseX - 1]);
          bool nextOn = (i < 6) ? byteDots[i + 1] : ((byteIdx < 39) && dots[baseX + 7]);

          // Per Apple IIe Technical Reference (page 24):
          // - Dots in even screen columns can be black, purple, or blue
          // - Dots in odd screen columns can be black, green, or orange
          // Use absolute screen column (x), not bit position within byte (i)
          bool evenColumn = (x & 1) == 0;

          if (!dotOn) {
            // OFF dot - check if part of alternating color pattern
            if (prevOn && !((i > 1) ? byteDots[i - 2] : ((byteIdx > 0) && dots[baseX - 2]))) {
              // Previous is isolated ON, bleed its color
              bool prevEven = ((x - 1) & 1) == 0;
              if (prevEven) {
                color = highBit ? HIRES_COLORS[4] : HIRES_COLORS[2]; // Blue/Violet
              } else {
                color = highBit ? HIRES_COLORS[5] : HIRES_COLORS[1]; // Green/Orange
              }
            } else if (nextOn && !((i < 5) ? byteDots[i + 2] : ((byteIdx < 39) && dots[baseX + 8]))) {
              // Next is isolated ON, bleed its color
              bool nextEven = ((x + 1) & 1) == 0;
              if (nextEven) {
                color = highBit ? HIRES_COLORS[4] : HIRES_COLORS[2]; // Blue/Violet
              } else {
                color = highBit ? HIRES_COLORS[5] : HIRES_COLORS[1]; // Green/Orange
              }
            } else {
              color = HIRES_COLORS[0]; // Black
            }
          } else {
            // ON dot
            if (prevOn || nextOn) {
              // Part of white run (2+ adjacent ON)
              color = HIRES_COLORS[3]; // White
            } else {
              // Isolated ON dot - artifact color based on screen column
              if (evenColumn) {
                color = highBit ? HIRES_COLORS[4] : HIRES_COLORS[2]; // Blue/Violet
              } else {
                color = highBit ? HIRES_COLORS[5] : HIRES_COLORS[1]; // Green/Orange
              }
            }
          }

          // Draw 2x2 block
          int screenX = x * 2;
          setPixel(screenX, screenY, color);
          setPixel(screenX + 1, screenY, color);
          setPixel(screenX, screenY + 1, color);
          setPixel(screenX + 1, screenY + 1, color);
        }
      }
    }
  }
}

void Video::renderDoubleLoRes() {
  // Double lo-res: 80x48 with 16 colors
  const auto &sw = mmu_.getSoftSwitches();

  for (int row = 0; row < 24; row++) {
    if (sw.mixed && row >= 20)
      break;

    for (int col = 0; col < 40; col++) {
      uint16_t addr = getTextAddress(row, col);

      // Aux memory (even pixels)
      uint8_t auxByte = mmu_.readRAM(addr, true);
      // Main memory (odd pixels)
      uint8_t mainByte = mmu_.readRAM(addr, false);

      uint8_t auxTop = auxByte & 0x0F;
      uint8_t auxBottom = (auxByte >> 4) & 0x0F;
      uint8_t mainTop = mainByte & 0x0F;
      uint8_t mainBottom = (mainByte >> 4) & 0x0F;

      int screenX = col * 14;
      int screenY = row * 16;

      // Draw aux pixels (left half, 7 pixels wide)
      for (int py = 0; py < 8; py++) {
        for (int px = 0; px < 7; px++) {
          setPixel(screenX + px, screenY + py, getLoResColor(auxTop));
        }
      }
      for (int py = 8; py < 16; py++) {
        for (int px = 0; px < 7; px++) {
          setPixel(screenX + px, screenY + py, getLoResColor(auxBottom));
        }
      }

      // Draw main pixels (right half, 7 pixels wide)
      for (int py = 0; py < 8; py++) {
        for (int px = 7; px < 14; px++) {
          setPixel(screenX + px, screenY + py, getLoResColor(mainTop));
        }
      }
      for (int py = 8; py < 16; py++) {
        for (int px = 7; px < 14; px++) {
          setPixel(screenX + px, screenY + py, getLoResColor(mainBottom));
        }
      }
    }
  }
}

void Video::renderDoubleHiRes() {
  // Double hi-res: 560x192 monochrome or 140x192 with 16 colors
  // Reference: Apple IIe Technical Reference Manual, Chapter 2
  //
  // Memory layout: 80 bytes per line (40 aux + 40 main interleaved)
  // "Seven bits from a byte in auxiliary memory are displayed first,
  //  followed by seven bits from the corresponding byte on the motherboard"
  //
  // Color: "any four adjacent dots along a line" determine color
  // "Think of a four-dot-wide window moving across the screen"
  // The 4-bit value directly indexes the 16-color palette (Table 2-7)
  
  const auto &sw = mmu_.getSoftSwitches();
  int maxRow = sw.mixed ? 160 : 192;

  // DHGR color palette from Table 2-7 (different order than LORES!)
  // Index = 4-bit pattern from dots, value = RGB color
  static const uint32_t DHGR_COLORS[16] = {
    0xFF000000, // 0  = 0000 = Black
    0xFFDD0033, // 1  = 0001 = Magenta
    0xFF885500, // 2  = 0010 = Brown
    0xFFFF6600, // 3  = 0011 = Orange
    0xFF007722, // 4  = 0100 = Dark Green
    0xFF555555, // 5  = 0101 = Gray 1
    0xFF11DD00, // 6  = 0110 = Green
    0xFFFFFF00, // 7  = 0111 = Yellow
    0xFF000099, // 8  = 1000 = Dark Blue
    0xFFDD22DD, // 9  = 1001 = Purple
    0xFFAAAAAA, // 10 = 1010 = Gray 2
    0xFFFF9988, // 11 = 1011 = Pink
    0xFF2222FF, // 12 = 1100 = Medium Blue
    0xFF66AAFF, // 13 = 1101 = Light Blue
    0xFF4FDC4A, // 14 = 1110 = Aqua
    0xFFFFFFFF  // 15 = 1111 = White
  };

  for (int row = 0; row < maxRow; row++) {
    // Build interleaved byte array: aux0, main0, aux1, main1, ...
    uint8_t line[80];
    for (int col = 0; col < 40; col++) {
      uint16_t addr = getHiResAddress(row, col);
      line[col * 2] = mmu_.readRAM(addr, true);      // aux byte
      line[col * 2 + 1] = mmu_.readRAM(addr, false); // main byte
    }

    // Extract 560 dots from 80 bytes
    // Per Apple IIe Technical Reference: bit 0 (LSB) is the leftmost dot
    uint8_t dots[564];
    memset(dots, 0, sizeof(dots));
    for (int i = 0; i < 560; i++) {
      int byteIdx = i / 7;
      int bitIdx = i % 7;  // LSB first: bit 0 is leftmost dot
      dots[i] = (line[byteIdx] >> bitIdx) & 1;
    }

    int screenY = row * 2;

    if (monochrome_) {
      for (int i = 0; i < 560; i++) {
        uint32_t color = getMonochromeColor(dots[i] != 0);
        setPixel(i, screenY, color);
        setPixel(i, screenY + 1, color);
      }
    } else {
      // Color mode: 140 color pixels, each 4 dots wide
      // Each color pixel's value is the 4-bit pattern of its dots
      // Per Table 2-7: the bit pattern is written with leftmost dot as MSB
      for (int colorPixel = 0; colorPixel < 140; colorPixel++) {
        int base = colorPixel * 4;

        // 4-bit color value: dot0=bit3(MSB), dot1=bit2, dot2=bit1, dot3=bit0(LSB)
        // e.g., pattern 0001 (Magenta) = dots [0,0,0,1] = index 1
        uint8_t colorIdx = (dots[base] << 3) |
                          (dots[base + 1] << 2) |
                          (dots[base + 2] << 1) |
                          dots[base + 3];

        uint32_t color = DHGR_COLORS[colorIdx];

        // Each color pixel is 4 screen dots wide
        for (int px = 0; px < 4; px++) {
          setPixel(base + px, screenY, color);
          setPixel(base + px, screenY + 1, color);
        }
      }
    }
  }
}

void Video::renderCharacter(int col, int row, uint8_t ch, bool inverse,
                            bool flash) {
  const auto &sw = mmu_.getSoftSwitches();

  // Handle flash
  if (flash && flashState_) {
    inverse = !inverse;
  }

  // Get character index based on character code range
  // Apple IIe character ROM layout:
  //   $00-$3F: Uppercase, numbers, symbols (@, A-Z, etc.)
  //   $40-$7F: Lowercase (`, a-z, etc.)
  uint8_t charIndex;
  if (ch < 0x40) {
    // $00-$3F: Inverse characters - ROM positions $00-$3F
    charIndex = ch;
  } else if (ch < 0x60) {
    // $40-$5F: Flash uppercase - ROM positions $00-$1F
    charIndex = ch & 0x3F;
  } else if (ch < 0x80) {
    // $60-$7F: Flash lowercase - ROM positions $60-$7F (same as normal lowercase)
    charIndex = ch;
  } else {
    // $80-$FF: Normal characters - ROM positions $00-$7F
    charIndex = ch & 0x7F;
    inverse = false;
  }

  // Calculate screen position (each char is 14x16 pixels with 2x scaling)
  int screenX = col * 14;
  int screenY = row * 16;

  // Text colors
  uint32_t fgColor, bgColor;
  if (monochrome_) {
    fgColor = getMonochromeColor(true);
    bgColor = getMonochromeColor(false);
  } else {
    fgColor = 0xFFFFFFFF; // White
    bgColor = 0xFF000000; // Black
  }

  if (inverse) {
    std::swap(fgColor, bgColor);
  }

  // Render 8x8 character with 2x scaling
  for (int charRow = 0; charRow < 8; charRow++) {
    // Read character ROM
    uint8_t rowData = mmu_.readCharROM(charIndex * 8 + charRow);

    // Handle alternate character set - MouseText replaces inverse characters ($00-$3F)
    if (sw.altCharSet && ch < 0x40) {
      // MouseText characters
      rowData = mmu_.readCharROM(0x400 + ch * 8 + charRow);
    }

    for (int charCol = 0; charCol < 7; charCol++) {
      bool pixelOn = (rowData & (1 << charCol)) != 0;
      uint32_t color = pixelOn ? fgColor : bgColor;

      // Draw 2x2 block
      int px = screenX + charCol * 2;
      int py = screenY + charRow * 2;
      setPixel(px, py, color);
      setPixel(px + 1, py, color);
      setPixel(px, py + 1, color);
      setPixel(px + 1, py + 1, color);
    }
  }
}

void Video::renderCharacter80(int col80, int row, uint8_t ch, bool inverse,
                              bool flash) {
  // 80-column mode: 7x8 characters, single-width pixels

  if (flash && flashState_) {
    inverse = !inverse;
  }

  // Get character index based on character code range
  // Apple IIe character ROM layout:
  //   $00-$3F: Uppercase, numbers, symbols (@, A-Z, etc.)
  //   $40-$7F: Lowercase (`, a-z, etc.)
  uint8_t charIndex;
  if (ch < 0x40) {
    // $00-$3F: Inverse characters - ROM positions $00-$3F
    charIndex = ch;
  } else if (ch < 0x60) {
    // $40-$5F: Flash uppercase - ROM positions $00-$1F
    charIndex = ch & 0x3F;
  } else if (ch < 0x80) {
    // $60-$7F: Flash lowercase - ROM positions $60-$7F (same as normal lowercase)
    charIndex = ch;
  } else {
    // $80-$FF: Normal characters - ROM positions $00-$7F
    charIndex = ch & 0x7F;
    inverse = false;
  }

  int screenX = col80 * 7;
  int screenY = row * 16;

  uint32_t fgColor, bgColor;
  if (monochrome_) {
    fgColor = getMonochromeColor(true);
    bgColor = getMonochromeColor(false);
  } else {
    fgColor = 0xFFFFFFFF;
    bgColor = 0xFF000000;
  }

  if (inverse) {
    std::swap(fgColor, bgColor);
  }

  for (int charRow = 0; charRow < 8; charRow++) {
    uint8_t rowData = mmu_.readCharROM(charIndex * 8 + charRow);

    for (int charCol = 0; charCol < 7; charCol++) {
      bool pixelOn = (rowData & (1 << charCol)) != 0;
      uint32_t color = pixelOn ? fgColor : bgColor;

      // Single width, double height
      int px = screenX + charCol;
      int py = screenY + charRow * 2;
      setPixel(px, py, color);
      setPixel(px, py + 1, color);
    }
  }
}

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
    // Use brightness-based monochrome
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
  // Text/LoRes base address is $0400 (page 1) or $0800 (page 2)
  return 0x0400 + TEXT_ROW_OFFSETS[row] + col;
}

uint16_t Video::getHiResAddress(int row, int col) const {
  // HiRes base address is $2000 (page 1) or $4000 (page 2)
  // Address calculation: base + (row/8)*0x400 + (row%8)*0x80 + col
  int block = row / 8;
  int line = row % 8;
  return 0x2000 + TEXT_ROW_OFFSETS[block] + line * 0x400 + col;
}

} // namespace a2e
