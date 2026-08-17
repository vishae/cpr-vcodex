#include "MosaicLibraryIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

namespace MosaicLibraryIndex {

namespace {

constexpr char kIndexDir[] = "/.crosspoint";
constexpr char kIndexPath[] = "/.crosspoint/mosaic_index.bin";
constexpr char kIndexTempPath[] = "/.crosspoint/mosaic_index.bin.tmp";
// v2 (CGV-003): Entry gained createdAt. A v1 index is rejected by load() like any
// other unknown version, so upgrading sends the user through CGV-010's "no index
// yet" prompt — which runs full bulk cover generation. That one-off cost is the
// price of the date-added sort and belongs in the release note.
constexpr uint8_t kFormatVersion = 2;
// Sanity bound so a corrupt count field can't drive a huge reserve on a device
// with 320 KB of RAM. Far above any realistic X4 library.
constexpr uint32_t kMaxEntries = 20000;

}  // namespace

bool load(Index& out) {
  if (!Storage.exists(kIndexPath)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("MLI", kIndexPath, file)) {
    return false;
  }

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != kFormatVersion) {
    // Older/newer layout — treat as absent rather than guessing at the fields.
    file.close();
    return false;
  }

  serialization::readString(file, out.libraryPath);
  serialization::readPod(file, out.fingerprint.fileCount);
  serialization::readPod(file, out.fingerprint.totalBytes);

  uint32_t entryCount = 0;
  serialization::readPod(file, entryCount);
  if (entryCount > kMaxEntries) {
    // Corrupt length field — reject rather than reserving against it.
    LOG_ERR("MLI", "Index entry count %u implausible, ignoring", entryCount);
    file.close();
    return false;
  }

  out.entries.clear();
  out.entries.reserve(entryCount);
  for (uint32_t i = 0; i < entryCount; i++) {
    // A truncated file reads back zeros rather than failing, which would yield a
    // silently short library; stop and reject instead.
    if (file.available() <= 0) {
      LOG_ERR("MLI", "Index truncated at entry %u of %u, ignoring", i, entryCount);
      file.close();
      return false;
    }
    Entry entry;
    serialization::readString(file, entry.path);
    serialization::readString(file, entry.title);
    serialization::readString(file, entry.author);
    serialization::readString(file, entry.series);
    serialization::readPod(file, entry.seriesIndex);
    serialization::readPod(file, entry.createdAt);
    out.entries.push_back(std::move(entry));
  }
  file.close();

  return true;
}

bool save(const Index& index) {
  if (!Storage.ensureDirectoryExists(kIndexDir)) {
    return false;
  }

  FsFile file;
  if (!Storage.openFileForWrite("MLI", kIndexTempPath, file)) {
    return false;
  }

  serialization::writePod(file, kFormatVersion);
  serialization::writeString(file, index.libraryPath);
  serialization::writePod(file, index.fingerprint.fileCount);
  serialization::writePod(file, index.fingerprint.totalBytes);

  const uint32_t entryCount = static_cast<uint32_t>(index.entries.size());
  serialization::writePod(file, entryCount);
  for (const auto& entry : index.entries) {
    serialization::writeString(file, entry.path);
    serialization::writeString(file, entry.title);
    serialization::writeString(file, entry.author);
    serialization::writeString(file, entry.series);
    serialization::writePod(file, entry.seriesIndex);
    serialization::writePod(file, entry.createdAt);
  }
  file.flush();
  file.close();

  // Rename over the live file only once the new one is complete on disk.
  Storage.remove(kIndexPath);
  if (!Storage.rename(kIndexTempPath, kIndexPath)) {
    LOG_ERR("MLI", "Could not rename temp index into place");
    Storage.remove(kIndexTempPath);
    return false;
  }
  return true;
}

}  // namespace MosaicLibraryIndex
