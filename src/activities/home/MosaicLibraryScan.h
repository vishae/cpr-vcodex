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

// When outFingerprint is non-null, it's filled with the fingerprint of the books
// found (counted and sized as they're encountered).
std::vector<std::string> scanBookPaths(const std::string& libraryPath, Fingerprint* outFingerprint = nullptr);

}  // namespace MosaicLibraryScan
