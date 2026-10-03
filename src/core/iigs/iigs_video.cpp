/*
 * iigs_video.cpp - Super Hi-Res, and the picture the machine actually shows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "iigs_video.hpp"

#include "../machine/machine_profile.hpp"
#include "../mmu/mmu.hpp"
#include "../video/video.hpp"
#include "iigs_memory.hpp"

#include <array>
#include <cstring>

namespace a2e::iigs {

namespace {
// $C029, the new video register. Bit 7 is the one that matters here; bit 6
// linearises Super Hi-Res memory on a ROM 3 and bit 5 turns the Mega II's
// bank-switched memory off, neither of which is modelled.
constexpr uint16_t NEW_VIDEO = 0xC029;
constexpr uint8_t NEW_VIDEO_SUPER_HIRES = 0x80;

constexpr int PALETTE_BYTES = SHR_PALETTE_ENTRIES * 2;

// In 640 mode a byte is four two-bit pixels, and which four colours each of
// them may use depends on where it sits in the byte. That is how sixteen
// colours are made to serve a mode that can only count to four per pixel:
// the palette is read as four groups of four, and the groups rotate across the
// byte. Software draws with it by arranging the palette so neighbouring groups
// dither into each other.
constexpr int MODE_640_GROUPS[4] = {2, 3, 0, 1};

// The VGC's sixteen fixed colours, as $0RGB. Black, red, dark blue, purple,
// dark green, dark grey, medium blue, light blue, brown, orange, light grey,
// pink, light green, yellow, aqua, white — the Control Panel's list, in the
// order the register numbers them.
constexpr uint16_t VGC_COLOURS[16] = {
    0x0000, 0x0D03, 0x0009, 0x0D2D, 0x0072, 0x0555, 0x022F, 0x06AF,
    0x0852, 0x0F60, 0x0AAA, 0x0F98, 0x01D1, 0x0FF0, 0x04F9, 0x0FFF,
};
} // namespace

// The profile is the host's description of the frame and iigs_spec.hpp is
// the machine's; they must be the same picture.
static_assert(machineProfile(MachineId::AppleIIgs).display.pixelWidth == RASTER_WIDTH);
static_assert(machineProfile(MachineId::AppleIIgs).display.pixelHeight == RASTER_HEIGHT);
static_assert(machineProfile(MachineId::AppleIIgs).display.lineDoubling == RASTER_LINE_DOUBLING);
static_assert(machineProfile(MachineId::AppleIIgs).display.textLeft == PICTURE_LEFT);
static_assert(machineProfile(MachineId::AppleIIgs).display.textTop == MEGAII_TOP);
static_assert(machineProfile(MachineId::AppleIIgs).display.textWidth == SHR_PIXELS_PER_LINE);
static_assert(machineProfile(MachineId::AppleIIgs).display.textHeight ==
              machineProfile(MachineId::AppleIIe).display.pixelHeight);

IIgsVideo::IIgsVideo(Video &megaII, IIgsMemory &memory)
    : megaII_(megaII), memory_(memory) {
  const auto &display = machineProfile(MachineId::AppleIIgs).display;
  width_ = display.pixelWidth;
  height_ = display.pixelHeight;
  frame_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
  shown_ = frame_;
}

void IIgsVideo::renderSuperHiResPicture(uint8_t *out) {
  // The screen's frame stays as it is: this draws into a frame of its own,
  // the same size, and takes the picture out of it.
  std::vector<uint8_t> scratch(frame_.size());
  std::swap(frame_, scratch);
  renderSuperHiRes();
  for (int line = 0; line < SHR_LINES; line++) {
    std::memcpy(out + static_cast<size_t>(line) * SHR_PIXELS_PER_LINE * 4, pictureRow(line),
                static_cast<size_t>(SHR_PIXELS_PER_LINE) * 4);
  }
  std::swap(frame_, scratch);
}

uint8_t *IIgsVideo::pictureRow(int line) {
  return scanline(PICTURE_TOP + line * RASTER_LINE_DOUBLING) +
         static_cast<size_t>(PICTURE_LEFT) * 4;
}

void IIgsVideo::paletteColour(uint16_t entry, uint8_t &red, uint8_t &green,
                              uint8_t &blue) {
  // $0RGB, four bits a channel. Repeating the nibble rather than shifting it
  // is what makes $F white rather than nearly-white.
  const uint8_t r = (entry >> 8) & 0x0F;
  const uint8_t g = (entry >> 4) & 0x0F;
  const uint8_t b = entry & 0x0F;
  red = static_cast<uint8_t>((r << 4) | r);
  green = static_cast<uint8_t>((g << 4) | g);
  blue = static_cast<uint8_t>((b << 4) | b);
}

uint16_t IIgsVideo::vgcColour(uint8_t index) {
  return VGC_COLOURS[index & 0x0F];
}

uint32_t IIgsVideo::vgcColourARGB(uint8_t index) {
  uint8_t red = 0, green = 0, blue = 0;
  paletteColour(vgcColour(index), red, green, blue);
  return 0xFF000000u | (static_cast<uint32_t>(red) << 16) |
         (static_cast<uint32_t>(green) << 8) | blue;
}

bool IIgsVideo::superHiResEnabled() const {
  return (memory_.peek(NEW_VIDEO) & NEW_VIDEO_SUPER_HIRES) != 0;
}

void IIgsVideo::fillFrame(uint32_t colour) {
  const uint8_t red = static_cast<uint8_t>(colour >> 16);
  const uint8_t green = static_cast<uint8_t>(colour >> 8);
  const uint8_t blue = static_cast<uint8_t>(colour);
  for (size_t at = 0; at < frame_.size(); at += 4) {
    frame_[at + 0] = red;
    frame_[at + 1] = green;
    frame_[at + 2] = blue;
    frame_[at + 3] = 0xFF;
  }
}

uint8_t *IIgsVideo::scanline(int y) {
  return frame_.data() + static_cast<size_t>(y) * width_ * 4;
}

void IIgsVideo::putPixel(uint8_t *row, int x, uint16_t colour) {
  uint8_t red = 0, green = 0, blue = 0;
  paletteColour(colour, red, green, blue);
  uint8_t *pixel = row + static_cast<size_t>(x) * 4;
  pixel[0] = red;
  pixel[1] = green;
  pixel[2] = blue;
  pixel[3] = 0xFF;
}

const uint8_t *IIgsVideo::render() {
  if (superHiResEnabled()) {
    renderSuperHiRes();
  } else {
    renderMegaII();
  }
  shown_ = frame_;
  return shown_.data();
}

// ---------------------------------------------------------------------------
// The beam
// ---------------------------------------------------------------------------

namespace {
// The frame's 262 lines against the raster drawn: the picture's 200 and the
// border's 12 below it, then blanking, then the 12 of border above the next
// picture.
constexpr int FRAME_LINES = 262;
constexpr int BOTTOM_BORDER_END = SHR_LINES + BORDER_BOTTOM_LINES;
constexpr int TOP_BORDER_START = FRAME_LINES - BORDER_TOP_LINES;
// The //e's 192 lines sit in the middle of the picture's 200.
constexpr int MEGAII_FIRST_LINE = (SHR_LINES - MEGAII_LINES) / 2;
} // namespace

void IIgsVideo::drawLinesTo(int finished) {
  if (finished > FRAME_LINES) finished = FRAME_LINES;
  for (; linesDrawn_ < finished; linesDrawn_++) drawFrameLine(linesDrawn_);
}

void IIgsVideo::finishFrame() {
  drawLinesTo(FRAME_LINES);
  std::swap(frame_, shown_);
  linesDrawn_ = 0;
}

void IIgsVideo::drawFrameLine(int line) {
  int rasterLine;
  if (line < BOTTOM_BORDER_END) {
    rasterLine = BORDER_TOP_LINES + line;
  } else if (line >= TOP_BORDER_START) {
    rasterLine = line - TOP_BORDER_START;
  } else {
    return; // blanking: nothing is sent
  }
  // Border across the whole line, in the colour it is now; the picture, if
  // this line has one, over the middle of it.
  fillRasterLine(rasterLine, vgcColourARGB(memory_.borderColour()));
  if (line >= SHR_LINES) return;
  if (superHiResEnabled()) {
    drawSuperHiResLine(line);
  } else {
    drawMegaIILine(line);
  }
}

void IIgsVideo::fillRasterLine(int rasterLine, uint32_t colour) {
  const uint8_t red = static_cast<uint8_t>(colour >> 16);
  const uint8_t green = static_cast<uint8_t>(colour >> 8);
  const uint8_t blue = static_cast<uint8_t>(colour);
  for (int copy = 0; copy < RASTER_LINE_DOUBLING; copy++) {
    uint8_t *row = scanline(rasterLine * RASTER_LINE_DOUBLING + copy);
    for (int x = 0; x < width_; x++) {
      row[x * 4 + 0] = red;
      row[x * 4 + 1] = green;
      row[x * 4 + 2] = blue;
      row[x * 4 + 3] = 0xFF;
    }
  }
}

namespace {
// Eight pixels for every seven dots, linearly: a dot boundary that falls
// inside a pixel is shared between its two dots, as a beam would draw it.
// The weights repeat every eight pixels, so they are worked out once.
constexpr int STRETCH_PIXELS = SHR_PIXELS_PER_LINE;
constexpr int STRETCH_DOTS = 560;
static_assert(STRETCH_PIXELS * 7 == STRETCH_DOTS * 8);
struct Tap { int dot; int weightA; int weightB; };
const std::array<Tap, STRETCH_PIXELS> &stretchTaps() {
  static const auto taps = [] {
    std::array<Tap, STRETCH_PIXELS> t{};
    for (int x = 0; x < STRETCH_PIXELS; x++) {
      const int numerator = x * STRETCH_DOTS;  // dot position, times PIXELS
      const int dot = numerator / STRETCH_PIXELS;
      const int fraction = numerator % STRETCH_PIXELS;  // of PIXELS
      t[x] = {dot, STRETCH_PIXELS - fraction, fraction};
    }
    return t;
  }();
  return taps;
}

// One row of the Mega II's 560-dot frame, stretched over the picture's 640
// pixels of the raster row `out`.
void stretchMegaIIRow(const uint8_t *source, int row, uint8_t *out) {
  const uint8_t *in = source + static_cast<size_t>(row) * STRETCH_DOTS * 4;
  out += static_cast<size_t>(PICTURE_LEFT) * 4;
  const auto &taps = stretchTaps();
  for (int x = 0; x < STRETCH_PIXELS; x++) {
    const Tap &tap = taps[x];
    const uint8_t *a = in + static_cast<size_t>(tap.dot) * 4;
    const uint8_t *b = tap.dot + 1 < STRETCH_DOTS ? a + 4 : a;
    for (int c = 0; c < 3; c++) {
      out[x * 4 + c] = static_cast<uint8_t>(
          (a[c] * tap.weightA + b[c] * tap.weightB + STRETCH_PIXELS / 2) / STRETCH_PIXELS);
    }
    out[x * 4 + 3] = 0xFF;
  }
}
} // namespace

void IIgsVideo::renderMegaII() {
  // The //e's picture, the same width as Super Hi-Res's — 40 cycles of 14
  // dots stretched over 40 cycles of 16 pixels, because that is what the
  // monitor shows — and centred in its 200 lines, since it has 192. Around it
  // is border, in the colour the Control Panel set (the bottom nibble of
  // $C034), and so are the four lines of the picture area above and below
  // it, which is what keeps the top and bottom borders the same height.
  fillFrame(vgcColourARGB(memory_.borderColour()));

  const int sourceHeight = machineProfile(MachineId::AppleIIe).display.pixelHeight;
  const uint8_t *source = megaII_.frameInProgress();
  for (int y = 0; y < sourceHeight; y++) {
    stretchMegaIIRow(source, y, scanline(MEGAII_TOP + y));
  }
}

// One line of the //e's picture, both of its rows, from the Mega II's frame
// as it is now: the Mega II draws each line as its beam leaves it, which is
// before this line is reached.
void IIgsVideo::drawMegaIILine(int line) {
  const int y = line - MEGAII_FIRST_LINE;
  if (y < 0 || y >= MEGAII_LINES) return; // border, above and below the 192
  const uint8_t *source = megaII_.frameInProgress();
  for (int copy = 0; copy < RASTER_LINE_DOUBLING; copy++) {
    const int row = y * RASTER_LINE_DOUBLING + copy;
    stretchMegaIIRow(source, row, scanline(MEGAII_TOP + row));
  }
}

void IIgsVideo::renderSuperHiRes() {
  // Everything Super Hi-Res draws from is in bank $E1: the pixels at $2000, a
  // control byte for each line at $9D00, and sixteen palettes at $9E00. A
  // program running in fast RAM writes to bank $01 and shadowing brings it
  // here, which is the arrangement the whole machine is built around.
  // The border is drawn first and the picture over it: a IIgs sends border
  // colour wherever it is not sending picture, Super Hi-Res or not.
  fillFrame(vgcColourARGB(memory_.borderColour()));

  for (int line = 0; line < SHR_LINES; line++) drawSuperHiResLine(line);
}

// One line of Super Hi-Res from memory as it is now: its control byte, the
// palette that names, and its 160 bytes.
void IIgsVideo::drawSuperHiResLine(int line) {
  MMU &megaIIMemory = memory_.megaII();
  auto slowRead = [&megaIIMemory](uint16_t address) {
    return megaIIMemory.readRAM(address, true); // bank $E1 is the aux side
  };

  {
    const uint8_t control =
        slowRead(static_cast<uint16_t>(SHR_SCB_BASE + line));

    // The line's own palette, read once rather than per pixel: sixteen entries
    // of two bytes, low byte first.
    uint16_t palette[SHR_PALETTE_ENTRIES];
    const uint16_t paletteBase = static_cast<uint16_t>(
        SHR_PALETTE_BASE + (control & SCB_PALETTE_MASK) * PALETTE_BYTES);
    for (int entry = 0; entry < SHR_PALETTE_ENTRIES; entry++) {
      const uint16_t at = static_cast<uint16_t>(paletteBase + entry * 2);
      palette[entry] =
          static_cast<uint16_t>(slowRead(at) | (slowRead(at + 1) << 8));
    }

    uint8_t pixels[SHR_BYTES_PER_LINE];
    const uint16_t pixelBase =
        static_cast<uint16_t>(SHR_PIXEL_BASE + line * SHR_BYTES_PER_LINE);
    for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
      pixels[byte] = slowRead(static_cast<uint16_t>(pixelBase + byte));
    }

    if (control & SCB_MODE_640) {
      drawLine640(line, pixels, palette);
    } else {
      drawLine320(line, pixels, palette, (control & SCB_FILL_MODE) != 0);
    }

    // 200 lines over a 400-line picture: every line is drawn twice, which is
    // what the machine does and why a IIgs picture is 200 lines tall.
    std::memcpy(pictureRow(line) + static_cast<size_t>(width_) * 4, pictureRow(line),
                static_cast<size_t>(SHR_PIXELS_PER_LINE) * 4);
  }
}

void IIgsVideo::drawLine320(int line, const uint8_t *pixels,
                            const uint16_t *palette, bool fillMode) {
  uint8_t *row = pictureRow(line);
  uint8_t previous = 0;

  for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
    // Two pixels to a byte, high nibble first.
    for (int half = 0; half < 2; half++) {
      uint8_t index = half == 0 ? (pixels[byte] >> 4) : (pixels[byte] & 0x0F);

      // Fill mode: colour zero is not a colour, it is "the same as the pixel
      // before". It exists because it made horizontal runs cheap to draw, and
      // it means a line's leftmost pixel can never be transparent — there is
      // nothing to its left to copy.
      if (fillMode && index == 0) index = previous;
      previous = index;

      const uint16_t colour = palette[index];
      const int x = (byte * 2 + half) * 2; // 320 pixels across a 640 screen
      putPixel(row, x, colour);
      putPixel(row, x + 1, colour);
    }
  }
}

void IIgsVideo::drawLine640(int line, const uint8_t *pixels,
                            const uint16_t *palette) {
  uint8_t *row = pictureRow(line);

  for (int byte = 0; byte < SHR_BYTES_PER_LINE; byte++) {
    for (int position = 0; position < 4; position++) {
      const uint8_t value =
          (pixels[byte] >> ((3 - position) * 2)) & 0x03;
      // Which quarter of the palette this pixel draws from depends on where it
      // sits in the byte. See MODE_640_GROUPS.
      const int group = MODE_640_GROUPS[position];
      const uint16_t colour = palette[group * 4 + value];
      putPixel(row, byte * 4 + position, colour);
    }
  }
}

} // namespace a2e::iigs
