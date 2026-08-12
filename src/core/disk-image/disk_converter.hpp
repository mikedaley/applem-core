/*
 * disk_converter.hpp - Convert a loaded disk image between save formats
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

#include "disk_image.hpp"

#include <cstdint>
#include <vector>

namespace a2e {

/**
 * The formats a disk can be saved as.
 *
 * DOSOrder and ProDOSOrder describe how sectors are laid out in the file, not
 * which filesystem is on the disk: a ProDOS volume is commonly distributed as
 * a DOS-ordered .dsk, and either filesystem can be stored either way. WOZ is a
 * bit stream rather than sectors, so it can hold what the other two cannot.
 */
enum class DiskSaveFormat {
  DOSOrder = 0,   // .dsk / .do — DOS 3.3 sector order
  ProDOSOrder = 1, // .po — ProDOS sector order
  WOZ = 2,         // .woz — WOZ 2.0 bit stream
};

class DiskConverter {
public:
  static constexpr int TRACKS = 35;
  static constexpr int SECTORS_PER_TRACK = 16;
  static constexpr int BYTES_PER_SECTOR = 256;
  static constexpr size_t DISK_SIZE = TRACKS * SECTORS_PER_TRACK * BYTES_PER_SECTOR;

  /**
   * Serialise a loaded image in the requested format.
   *
   * Converting a bit-stream image to a sector format decodes its tracks, which
   * only works for a normally formatted disk — a copy-protected WOZ cannot be
   * represented as sectors and the conversion fails rather than producing a
   * plausible-looking file with holes in it.
   *
   * @param image  The image to read from
   * @param format Format to produce
   * @param out    Output: the file bytes
   * @return true on success
   */
  static bool convert(DiskImage& image, DiskSaveFormat format,
                      std::vector<uint8_t>& out);

  /**
   * Whether an image can be saved in a format, without doing the work twice
   * over. Used to grey out choices the disk cannot honour.
   */
  static bool canConvert(DiskImage& image, DiskSaveFormat format);

  /**
   * The format an image would be saved in if left alone — what the file it
   * came from was.
   */
  static DiskSaveFormat nativeFormat(const DiskImage& image);

  /**
   * Re-lay sectors between DOS and ProDOS order. The two orders are inverses
   * of one another, so one function serves both directions.
   */
  static void reorderSectors(const uint8_t* src, size_t size, bool toProDOSOrder,
                             std::vector<uint8_t>& out);
};

} // namespace a2e
