#pragma once

#include <string>
#include <vector>

// Recursive scan of a Cover Grid library folder (CGV-001/CGV-004): returns
// every .epub/.xtc path found beneath libraryPath, skipping hidden/system
// folders and finished_books/. Shared between MosaicBrowserActivity and its
// bulk metadata/cover generator (CGV-008) so the two scans can't drift.
namespace MosaicLibraryScan {
std::vector<std::string> scanBookPaths(const std::string& libraryPath);
}
