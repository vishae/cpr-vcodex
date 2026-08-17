#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Recursive scan of a Cover Grid library folder (CGV-001/CGV-004): returns
// every .epub/.xtc path found beneath libraryPath, skipping hidden/system
// folders and finished_books/. Shared between MosaicBrowserActivity and its
// bulk metadata/cover generator (CGV-008) so the two scans can't drift.
namespace MosaicLibraryScan {

// Change-detection fingerprint for the persisted library index (CGV-010).
// Accumulated from the directory entries the walk already reads — no file opens
// and no metadata parsing, so it costs nothing on top of the scan itself.
// Directory mtime was ruled out: FAT doesn't propagate it up from nested
// subfolders, so a book added under books/Author/Series/ would be invisible.
struct Fingerprint {
  uint32_t fileCount = 0;
  uint64_t totalBytes = 0;

  bool operator==(const Fingerprint& other) const {
    return fileCount == other.fileCount && totalBytes == other.totalBytes;
  }
  bool operator!=(const Fingerprint& other) const { return !(*this == other); }
};

// FAT create-timestamp for a book, packed as (date << 16) | time straight from the
// directory entry (CGV-003 date-added sort, CGV-005). FAT's date word packs
// year/month/day in descending significance and its time word hour/minute/second,
// so the combined value sorts chronologically without converting to an epoch —
// which is all the sort needs, and avoids a timezone question the device can't
// answer. 0 means the entry carried no create time; those books belong in the
// trailing bucket, not at the start of time.
using CreatedAt = uint32_t;

// When outFingerprint is non-null, it's filled with the fingerprint of the books
// found (counted and sized as they're encountered).
//
// When outCreatedAt is non-null, it's filled with one create-timestamp per
// returned path, in the same order. Like the fingerprint's size, this comes from
// the directory entry the walk already has open, so it costs no extra reads.
std::vector<std::string> scanBookPaths(const std::string& libraryPath, Fingerprint* outFingerprint = nullptr,
                                       std::vector<CreatedAt>* outCreatedAt = nullptr);

}  // namespace MosaicLibraryScan
