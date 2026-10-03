/*
 * iigs_video.hpp - Super Hi-Res, and the picture the machine actually shows
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "iigs_spec.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace a2e {
class Video;
}

namespace a2e::iigs {

class IIgsMemory;

/**
 * IIgsVideo - the machine's screen, whichever half of it is drawing
 *
 * A IIgs has two video systems and shows one of them. The Mega II's is a //e's,
 * drawn by the //e's own `Video` class from the //e's own memory. The other is
 * Super Hi-Res, which is this: 200 lines of pixels in bank $E1, each line
 * choosing its own width and its own sixteen colours out of four thousand and
 * ninety-six.
 *
 * $C029 bit 7 decides which is on screen, and that is the whole of the switch.
 *
 * **A IIgs screen is per-line, which is the thing to understand about it.**
 * There is no single "mode". Each of the 200 lines has a control byte of its
 * own saying whether it is 320 pixels wide or 640, which of the sixteen
 * palettes it draws from, and whether it fills. So one screen can be 320-wide
 * artwork above a 640-wide menu bar, with different colours in each, and
 * programs really did that.
 *
 * The picture is composited into the raster a monitor is sent, border and
 * all: 736x448, with the 640x200 picture (lines doubled) at (48, 24) and the
 * border colour from $C034 around it — the counts are iigs_spec.hpp's. The
 * Mega II's 560x384 goes in the same place, stretched to 640 wide, because a
 * text screen and a Super Hi-Res screen are the same width on the monitor,
 * and its last eight lines are border, because it is eight lines shorter.
 */
class IIgsVideo {
public:
  IIgsVideo(Video &megaII, IIgsMemory &memory);

  /**
   * Draw the whole screen from the machine as it is now, make that the frame
   * shown, and return it. RGBA, 736x448. For a picture wanted at once (a
   * paused machine redrawn, a test); a running machine draws its frames as
   * the beam goes, below.
   */
  const uint8_t *render();

  /**
   * The last frame the beam finished, which is what a host publishes. Never
   * a frame in progress: one drawn into while it is shown is two frames at
   * once, split where the beam had got to, and since a host does not publish
   * in step with the frame the split walks down the screen.
   */
  const uint8_t *frame() const { return shown_.data(); }

  /**
   * The beam's progress: `finished` lines of the 262-line frame are done.
   * Each line is drawn as the beam leaves it, from the machine as it is at
   * that moment, so a palette, a line's control byte, the border colour or
   * $C029 changed part-way down the screen is shown where it changed. Lines
   * 0-199 are the picture, 200-211 the border below it, and 250-261 the
   * border above the next picture, which the monitor shows above it.
   */
  void drawLinesTo(int finished);
  /** The frame is finished: the lines left are drawn and it is shown. */
  void finishFrame();
  /** Start drawing a frame from its top, as a reset does. */
  void restartFrame() { linesDrawn_ = 0; }

  size_t framebufferSize() const { return frame_.size(); }

  /**
   * The Super Hi-Res picture, whatever $C029 says, for a memory viewer: 640 x
   * 200 RGBA, a row a line, into `out`. Drawn by the same code as the screen
   * into a frame of its own, so the screen's is not touched.
   */
  void renderSuperHiResPicture(uint8_t *out);

  /** $C029 NEWVIDEO: bit 7 puts Super Hi-Res on screen. */
  bool superHiResEnabled() const;

  /**
   * A palette entry as a colour.
   *
   * Entries are $0RGB — four bits each, so 4096 colours — and each nibble is
   * expanded to eight bits by repeating it, which is what makes $F map to 255
   * rather than 240 and keeps white white.
   */
  static void paletteColour(uint16_t entry, uint8_t &red, uint8_t &green,
                            uint8_t &blue);

  /**
   * One of the VGC's sixteen fixed colours, as `$0RGB`.
   *
   * These are the colours the Control Panel offers for text, background and
   * border, and they are the machine's own rather than the Apple II palette
   * the NTSC decoder derives. A IIgs generates them digitally — there is no
   * subcarrier involved — so medium blue is `$22F` and looks it, where a
   * composite //e's blue is whatever a television makes of four dots.
   */
  static uint16_t vgcColour(uint8_t index);

  /** The same, packed as the 0xAARRGGBB the video pipeline passes around. */
  static uint32_t vgcColourARGB(uint8_t index);

  // Scanline control byte, per line.
  static constexpr uint8_t SCB_MODE_640 = 0x80;
  static constexpr uint8_t SCB_INTERRUPT = 0x40;
  static constexpr uint8_t SCB_FILL_MODE = 0x20;
  static constexpr uint8_t SCB_PALETTE_MASK = 0x0F;

private:
  void renderSuperHiRes();
  void renderMegaII();
  // One line of the frame, as the beam leaves it (see drawLinesTo).
  void drawFrameLine(int line);
  void drawSuperHiResLine(int line);
  void drawMegaIILine(int line);
  void fillRasterLine(int rasterLine, uint32_t colour);
  void fillFrame(uint32_t colour);
  void drawLine320(int line, const uint8_t *pixels, const uint16_t *palette,
                   bool fillMode);
  void drawLine640(int line, const uint8_t *pixels, const uint16_t *palette);
  uint8_t *pictureRow(int line); // the first picture pixel of a doubled line
  uint8_t *scanline(int y);
  void putPixel(uint8_t *row, int x, uint16_t colour);

  Video &megaII_;
  IIgsMemory &memory_;
  // The frame being drawn, and the last one finished, which is the one shown.
  std::vector<uint8_t> frame_;
  std::vector<uint8_t> shown_;
  int linesDrawn_ = 0; // lines of the frame being drawn that are done
  int width_ = 0;
  int height_ = 0;
};

} // namespace a2e::iigs
