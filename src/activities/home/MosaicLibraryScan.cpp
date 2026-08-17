#include "MosaicLibraryScan.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <cstring>

namespace MosaicLibraryScan {

namespace {

// Pack FAT's two directory-entry words into one sortable value. Returns 0 when
// the entry has no create time, which the sort treats as "unknown" rather than
// as the earliest possible date.
MosaicLibraryScan::CreatedAt readCreatedAt(FsFile& file) {
  uint16_t date = 0;
  uint16_t time = 0;
  if (!file.getCreateDateTime(&date, &time)) {
    return 0;
  }
  return (static_cast<MosaicLibraryScan::CreatedAt>(date) << 16) | time;
}

}  // namespace

std::vector<std::string> scanBookPaths(const std::string& libraryPath, Fingerprint* outFingerprint,
                                       std::vector<CreatedAt>* outCreatedAt) {
  std::vector<std::string> paths;
  if (outFingerprint) *outFingerprint = Fingerprint{};
  if (outCreatedAt) outCreatedAt->clear();
  std::vector<std::string> stack;
  stack.push_back(libraryPath);
  char nameBuf[512];

  while (!stack.empty()) {
    const std::string dirPath = std::move(stack.back());
    stack.pop_back();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    dir.rewindDirectory();

    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      const bool isDir = file.isDirectory();
      file.getName(nameBuf, sizeof(nameBuf));

      if (nameBuf[0] == '.' || strcmp(nameBuf, "System Volume Information") == 0 ||
          strcmp(nameBuf, "finished_books") == 0) {
        file.close();
        continue;
      }

      std::string full = dirPath;
      if (full.empty() || full.back() != '/') full += "/";
      full += nameBuf;

      if (isDir) {
        stack.push_back(full);
      } else {
        std::string_view filename{nameBuf};
        if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
          paths.push_back(full);
          if (outFingerprint) {
            // Size comes from the directory entry already open here — no extra read.
            outFingerprint->fileCount++;
            outFingerprint->totalBytes += file.fileSize64();
          }
          if (outCreatedAt) {
            // Same entry, same reason: the create time is already in hand.
            outCreatedAt->push_back(readCreatedAt(file));
          }
        }
      }
      file.close();
    }
    dir.close();
  }

  return paths;
}

}  // namespace MosaicLibraryScan
