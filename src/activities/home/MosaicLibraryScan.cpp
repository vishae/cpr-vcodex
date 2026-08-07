#include "MosaicLibraryScan.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <cstring>

namespace MosaicLibraryScan {

std::vector<std::string> scanBookPaths(const std::string& libraryPath) {
  std::vector<std::string> paths;
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
        }
      }
      file.close();
    }
    dir.close();
  }

  return paths;
}

}  // namespace MosaicLibraryScan
