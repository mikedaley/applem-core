#include "video.hpp"
#include <cstring>

namespace a2e {

// Helper function to blend two ARGB colors
static uint32_t blendColors(uint32_t c1, uint32_t c2, float factor) {
  uint8_t a1 = (c1 >> 24) & 0xFF;
  uint8_t r1 = (c1 >> 16) & 0xFF;
  uint8_t g1 = (c1 >> 8) & 0xFF;
  uint8_t b1 = c1 & 0xFF;

  uint8_t r2 = (c2 >> 16) & 0xFF;
  uint8_t g2 = (c2 >> 8) & 0xFF;
  uint8_t b2 = c2 & 0xFF;

  uint8_t r = static_cast<uint8_t>(r1 * (1.0f - factor) + r2 * factor);
  uint8_t g = static_cast<uint8_t>(g1 * (1.0f - factor) + g2 * factor);
  uint8_t b = static_cast<uint8_t>(b1 * (1.0f - factor) + b2 * factor);

  return (a1 << 24) | (r << 16) | (g << 8) | b;
}

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
    // DHR requires: AN3 OFF (!an3), 80COL on
    if (sw.col80 && !sw.an3) {
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
    // Double LoRes: AN3 OFF (!an3), 80COL on
    if (sw.col80 && !sw.an3) {
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

      // Determine page offset - PAGE2 without 80STORE displays page 2
      uint16_t pageOffset = (sw.page2 && !sw.store80) ? 0x0400 : 0x0000;

      if (sw.col80) {
        // 80-column mode - interleaved main/aux memory
        uint8_t mainCh = mmu_.readRAM(addr + pageOffset, false);
        uint8_t auxCh = mmu_.readRAM(addr + pageOffset, true);

        bool mainInverse = (mainCh & 0xC0) == 0x00;
        bool mainFlash = (mainCh & 0xC0) == 0x40;
        bool auxInverse = (auxCh & 0xC0) == 0x00;
        bool auxFlash = (auxCh & 0xC0) == 0x40;

        renderCharacter80(col * 2, row, auxCh, auxInverse, auxFlash);
        renderCharacter80(col * 2 + 1, row, mainCh, mainInverse, mainFlash);
      } else {
        // 40-column mode
        uint8_t ch = mmu_.readRAM(addr + pageOffset, false);
        bool inverse = (ch & 0xC0) == 0x00;
        bool flash = (ch & 0xC0) == 0x40;
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

      // Determine page offset - PAGE2 without 80STORE displays page 2
      uint16_t pageOffset = (sw.page2 && !sw.store80) ? 0x0400 : 0x0000;

      // Main memory character (odd columns in display)
      uint8_t mainCh = mmu_.readRAM(addr + pageOffset, false);

      // Aux memory character (even columns in display)
      uint8_t auxCh = mmu_.readRAM(addr + pageOffset, true);

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
      // NTSC artifact color mode
      //
      // Per Apple IIe Technical Reference:
      // - Dots in even screen columns produce violet (high=0) or blue (high=1)
      // - Dots in odd screen columns produce green (high=0) or orange (high=1)
      // - Adjacent ON dots combine to produce white
      //
      // Simple and efficient rule matching classic emulators:
      // - 2+ adjacent ON dots = white
      // - 1 isolated ON dot = artifact color
      // - OFF dots = black (with optional fringing)

      for (int x = 0; x < 280; x++) {
        uint32_t color;
        bool highBit = highBits[x] != 0;
        bool dotOn = dots[x] != 0;
        bool evenColumn = (x & 1) == 0;

        // Check immediate neighbors (O(1) per pixel)
        bool prevOn = (x > 0) && dots[x - 1];
        bool nextOn = (x < 279) && dots[x + 1];

        if (!dotOn) {
          // OFF dot = black (NTSC fringing is handled in the shader)
          color = HIRES_COLORS[0];
        } else {
          // ON dot - white if adjacent to another ON, otherwise artifact color
          if (prevOn || nextOn) {
            // Part of a run of 2+ adjacent ON dots = white
            color = HIRES_COLORS[3];
          } else {
            // Isolated ON dot = artifact color based on column and high bit
            if (evenColumn) {
              color = highBit ? HIRES_COLORS[4] : HIRES_COLORS[2]; // Blue/Violet
            } else {
              color = highBit ? HIRES_COLORS[5] : HIRES_COLORS[1]; // Orange/Green
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
    0xFF9F1B48, // 1  = 0001 = Magenta
    0xFF496500, // 2  = 0010 = Brown
    0xFFD87300, // 3  = 0011 = Orange
    0xFF197544, // 4  = 0100 = Dark Green
    0xFF818181, // 5  = 0101 = Grey 1
    0xFF3CCC00, // 6  = 0110 = Light Green
    0xFFBCD600, // 7  = 0111 = Yellow
    0xFF4832EB, // 8  = 1000 = Dark Blue
    0xFFD643FF, // 9  = 1001 = Purple
    0xFF818181, // 10 = 1010 = Grey 2
    0xFFFB8FBC, // 11 = 1011 = Pink
    0xFF3692FF, // 12 = 1100 = Medium Blue
    0xFFB89EFF, // 13 = 1101 = Light Blue
    0xFF6CE6B8, // 14 = 1110 = Aqua
    0xFFF1F1F1  // 15 = 1111 = White
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
      // Color mode: clean DHGR output using phase-aligned 4-dot windows
      // NTSC fringing effects are applied in the CRT shader for better quality
      for (int i = 0; i < 560; i++) {
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
}

void Video::renderCharacter(int col, int row, uint8_t ch, bool inverse,
                            bool flash) {
  const auto &sw = mmu_.getSoftSwitches();

  // Apple IIe character ROM layout:
  // The ROM contains 128 unique character patterns (indices 0-127).
  // Screen codes map to character indices via the lower 7 bits.
  // The display mode (inverse/flash/normal) is determined by the high bits
  // and controls color rendering, not which pattern is fetched.
  //
  // Screen code ranges:
  // - $00-$3F: Inverse characters - charIndex = ch (0-63)
  // - $40-$5F: Flash uppercase - charIndex = ch & $3F (0-31, same as inverse uppercase)
  // - $60-$7F: Flash lowercase - charIndex = ch (96-127)
  // - $80-$FF: Normal characters - charIndex = ch & $7F (0-127)
  //
  // ALTCHARSET ($C00F) selects alternate character set with MouseText at $40-$5F.

  // Apple IIe Enhanced character ROM (342-0273-A) layout:
  // This 8KB ROM contains US and UK character sets. The first 2KB is the US set:
  //   $000-$3FF (0-1023): Primary character set, NON-inverted data
  //     - Indices 0-63: Characters for screen codes $00-$3F and $40-$7F
  //     - Indices 64-95: Inverted/unused region (skip this)
  //     - Indices 96-127: Lowercase characters for $E0-$FF
  //   $400-$7FF (1024-2047): Alternate character set (MouseText), INVERTED data
  //
  // Screen code to character mapping (using indices 0-63 and 96-127 only):
  //   $00-$1F: Inverse uppercase @ A-Z etc → index 0-31
  //   $20-$3F: Inverse symbols/digits → index 32-63
  //   $40-$5F: Flash uppercase → index 0-31 (same as inverse)
  //   $60-$7F: Flash symbols → index 32-63
  //   $80-$9F: Normal uppercase → index 0-31
  //   $A0-$BF: Normal symbols/digits → index 32-63
  //   $C0-$DF: Normal uppercase → index 0-31
  //   $E0-$FF: Normal lowercase → index 96-127

  uint16_t romOffset;
  bool needsXor = false;

  if (sw.altCharSet) {
    // ALTCHARSET mode: MouseText replaces flash characters at $40-$5F
    // MouseText characters are stored at indices 64-95 (offset 512-767), INVERTED
    // All other characters use primary charset mapping
    uint8_t charIndex;
    if (ch >= 0x40 && ch < 0x60) {
      // $40-$5F: MouseText characters at indices 64-95
      charIndex = ch;  // $40-$5F maps to indices 64-95 directly
      needsXor = true;
      inverse = false;
    } else if (ch >= 0x60 && ch < 0x80) {
      // $60-$7F: MouseText continuation at indices 96-127? Or symbols?
      // Actually these show as inverse MouseText in ALTCHARSET mode
      charIndex = ch;  // Direct mapping
      needsXor = true;
    } else if (ch < 0x40) {
      // $00-$3F: Inverse characters, same as primary
      charIndex = ch;
      needsXor = false;
    } else {
      // $80-$FF: Normal characters, same mapping as primary
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
    // Primary character set at offset 0-1023, data is NON-inverted
    // Map screen codes to avoid indices 64-95 (which have bad data)
    uint8_t charIndex;
    if (ch < 0x20) {
      charIndex = ch;           // $00-$1F → 0-31
    } else if (ch < 0x40) {
      charIndex = ch;           // $20-$3F → 32-63
    } else if (ch < 0x60) {
      charIndex = ch & 0x1F;    // $40-$5F → 0-31
    } else if (ch < 0x80) {
      charIndex = (ch & 0x1F) + 32;  // $60-$7F → 32-63
    } else if (ch < 0xA0) {
      charIndex = ch & 0x1F;    // $80-$9F → 0-31
      inverse = false;
    } else if (ch < 0xC0) {
      charIndex = (ch & 0x1F) + 32;  // $A0-$BF → 32-63
      inverse = false;
    } else if (ch < 0xE0) {
      charIndex = ch & 0x1F;    // $C0-$DF → 0-31
      inverse = false;
    } else {
      charIndex = (ch & 0x1F) + 96;  // $E0-$FF → 96-127
      inverse = false;
    }
    romOffset = charIndex * 8;
    needsXor = false;  // Primary set data is NOT inverted
  }

  // Apply UK character set offset if enabled (UK chars in second 4KB of ROM)
  if (ukCharSet_) {
    romOffset += 0x1000;  // 4KB offset for UK character set
  }

  // Handle flash - toggle inverse state when flash is active
  // Note: When altCharSet is enabled, flash characters display as normal (no flash)
  if (flash && flashState_ && !sw.altCharSet) {
    inverse = !inverse;
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
    uint8_t rowData = mmu_.readCharROM(romOffset + charRow);

    // XOR with 0xFF if data is stored inverted
    if (needsXor) {
      rowData ^= 0xFF;
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
  const auto &sw = mmu_.getSoftSwitches();

  // Same ROM addressing as 40-column mode
  uint16_t romOffset;
  bool needsXor = false;

  if (sw.altCharSet) {
    // ALTCHARSET mode: MouseText replaces flash characters at $40-$5F
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
    // Primary character set at offset 0-1023, data is NON-inverted
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

  // Apply UK character set offset if enabled (UK chars in second 4KB of ROM)
  if (ukCharSet_) {
    romOffset += 0x1000;  // 4KB offset for UK character set
  }

  // Handle flash
  if (flash && flashState_ && !sw.altCharSet) {
    inverse = !inverse;
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
    uint8_t rowData = mmu_.readCharROM(romOffset + charRow);

    if (needsXor) {
      rowData ^= 0xFF;
    }

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
