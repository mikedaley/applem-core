/*
 * fs_write_status.hpp - Result codes shared by the filesystem writers
 *
 * Written by
 *  Mike Daley <michael_daley@icloud.com>
 */

#pragma once

namespace a2e {

/**
 * Outcome of a filesystem write.
 *
 * The values below the filesystem layer itself (NoDisk, WriteProtected,
 * UnsupportedImage) are produced by the callers that own the disk image, but
 * they share this enum so a single code and a single message table describe
 * the whole operation from the UI's point of view.
 */
enum class FsWriteStatus {
  OK = 0,
  NoDisk,
  WriteProtected,
  UnsupportedImage,
  ImageTooSmall,
  NotFormatted,
  InvalidName,
  FileLocked,
  FileTooLarge,
  DiskFull,
  DirectoryFull,
};

inline const char* fsWriteStatusMessage(FsWriteStatus status) {
  switch (status) {
    case FsWriteStatus::OK:               return "OK";
    case FsWriteStatus::NoDisk:           return "No disk in drive";
    case FsWriteStatus::WriteProtected:   return "Disk is write protected";
    case FsWriteStatus::UnsupportedImage: return "Image format cannot be written to";
    case FsWriteStatus::ImageTooSmall:    return "Disk image is too small";
    case FsWriteStatus::NotFormatted:     return "Disk is not DOS 3.3 or ProDOS formatted";
    case FsWriteStatus::InvalidName:      return "Invalid filename";
    case FsWriteStatus::FileLocked:       return "File is locked";
    case FsWriteStatus::FileTooLarge:     return "File is too large for this filesystem";
    case FsWriteStatus::DiskFull:         return "Disk full";
    case FsWriteStatus::DirectoryFull:    return "Directory full";
  }
  return "Unknown error";
}

} // namespace a2e
