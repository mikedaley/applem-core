/*
 * disk_converter.cpp - Convert a loaded disk image between save formats
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#include "disk_converter.hpp"
#include "dsk_disk_image.hpp"
#include "gcr_encoding.hpp"
#include "woz_disk_image.hpp"

#include <cstring>
#include <memory>

namespace a2e {

namespace {

// Distinguishing the concrete image type by format keeps this free of RTTI,
// which the WASM build has no other reason to carry.
bool isWoz(const DiskImage& image) {
  DiskImage::Format format = image.getFormat();
  return format == DiskImage::Format::WOZ1 || format == DiskImage::Format::WOZ2;
}

// Sector data as the file stores it, converted to DOS order if needed. WOZ
// images decode their tracks straight to DOS order already.
bool sectorsInDOSOrder(DiskImage& image, std::vector<uint8_t>& out) {
  size_t size = 0;
  const uint8_t* sectors = image.getSectorData(&size);
  if (!sectors || size < DiskConverter::DISK_SIZE) {
    return false;
  }

  if (!isWoz(image) && image.getFormat() == DiskImage::Format::PO) {
    DiskConverter::reorderSectors(sectors, size, false, out);
    return true;
  }

  out.assign(sectors, sectors + size);
  return true;
}

} // namespace

void DiskConverter::reorderSectors(const uint8_t* src, size_t size,
                                   bool toProDOSOrder,
                                   std::vector<uint8_t>& out) {
  out.assign(src, src + size);

  int tracks = static_cast<int>(size / (SECTORS_PER_TRACK * BYTES_PER_SECTOR));
  for (int track = 0; track < tracks; track++) {
    for (int sector = 0; sector < SECTORS_PER_TRACK; sector++) {
      // The mapping is its own inverse, so one table drives both directions
      int mapped = GCR::PRODOS_TO_DOS_SECTOR[sector];
      // Going to ProDOS order, sector N of the output holds the DOS-order
      // sector the table names; coming back, that same pairing runs the other
      // way round.
      int from = toProDOSOrder ? mapped : sector;
      int to = toProDOSOrder ? sector : mapped;
      size_t srcOffset = (track * SECTORS_PER_TRACK + from) * BYTES_PER_SECTOR;
      size_t dstOffset = (track * SECTORS_PER_TRACK + to) * BYTES_PER_SECTOR;
      std::memcpy(out.data() + dstOffset, src + srcOffset, BYTES_PER_SECTOR);
    }
  }
}

DiskSaveFormat DiskConverter::nativeFormat(const DiskImage& image) {
  switch (image.getFormat()) {
    case DiskImage::Format::WOZ1:
    case DiskImage::Format::WOZ2:
      return DiskSaveFormat::WOZ;
    case DiskImage::Format::PO:
      return DiskSaveFormat::ProDOSOrder;
    default:
      return DiskSaveFormat::DOSOrder;
  }
}

bool DiskConverter::canConvert(DiskImage& image, DiskSaveFormat format) {
  if (!image.isLoaded()) return false;

  if (format == DiskSaveFormat::WOZ) {
    // A sector image always encodes; a WOZ is already one
    return true;
  }

  // Sector formats need decodable sectors, which a copy-protected WOZ has not
  size_t size = 0;
  const uint8_t* sectors = image.getSectorData(&size);
  return sectors != nullptr && size >= DISK_SIZE;
}

bool DiskConverter::convert(DiskImage& image, DiskSaveFormat format,
                            std::vector<uint8_t>& out) {
  out.clear();
  if (!image.isLoaded()) return false;

  if (format == DiskSaveFormat::WOZ) {
    if (isWoz(image)) {
      size_t size = 0;
      const uint8_t* data = image.exportData(&size);
      if (!data || size == 0) return false;
      out.assign(data, data + size);
      return true;
    }

    // Encode the sector image to GCR track by track, then wrap it in a WOZ.
    // The encoding is the image's own, so what a drive would read off the
    // converted file is what it reads off the original.
    std::vector<uint8_t> dosOrder;
    if (!sectorsInDOSOrder(image, dosOrder)) return false;

    // Both images are allocated on the heap deliberately: a DskDiskImage
    // carries its 140KB sector array inline, and the WASM build runs on a
    // 64KB stack, where one as a local overflows into whatever lies below.
    auto source = std::make_unique<DskDiskImage>();
    if (!source->loadAs(dosOrder.data(), dosOrder.size(), "convert.dsk",
                        DiskImage::Format::DO)) {
      return false;
    }

    std::vector<WozDiskImage::BitTrack> tracks(TRACKS);
    for (int t = 0; t < TRACKS; t++) {
      WozDiskImage::BitTrack bt;
      if (source->getTrackBits(t, bt.bits, bt.bit_count)) {
        tracks[t] = std::move(bt);
      }
    }

    auto woz = std::make_unique<WozDiskImage>();
    if (!woz->createFromTrackBits(tracks)) return false;

    size_t size = 0;
    const uint8_t* data = woz->exportData(&size);
    if (!data || size == 0) return false;
    out.assign(data, data + size);
    return true;
  }

  std::vector<uint8_t> dosOrder;
  if (!sectorsInDOSOrder(image, dosOrder)) return false;

  if (format == DiskSaveFormat::ProDOSOrder) {
    reorderSectors(dosOrder.data(), dosOrder.size(), true, out);
    return true;
  }

  out = std::move(dosOrder);
  return true;
}

} // namespace a2e
