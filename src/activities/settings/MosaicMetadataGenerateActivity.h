#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/home/MosaicLibraryIndex.h"
#include "activities/home/MosaicLibraryScan.h"

/**
 * On-demand bulk metadata + cover-thumbnail generation for the Cover Grid
 * mosaic browser (CGV-008). Runs the same per-book work
 * MosaicBrowserActivity::indexBook() does lazily on grid entry, but for every
 * book in the library up front — so a bulk book upload can be pre-indexed at
 * a convenient time instead of eating the cost while actually browsing.
 *
 * A completed run also persists the library index (CGV-010) from the metadata
 * it just parsed, which is what makes it the "Update now" target of the
 * stale / no-cache-yet prompt.
 *
 * Launched from Settings > Apps > Cover Grid > "Generate all covers", and from
 * that prompt.
 */
class MosaicMetadataGenerateActivity final : public Activity {
 public:
  // Two passes, deliberately separate. Covers are generated first with nothing
  // accumulating in RAM, then the index is built from the metadata caches that
  // pass just warmed. Building both at once made headroom shrink as the run
  // progressed, so low-memory skips clustered at the tail and recurred in the
  // same place on every re-run — re-running could never fill them in.
  enum class State { GENERATING, INDEXING, DONE };

  explicit MosaicMetadataGenerateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MosaicMetadataGenerate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::GENERATING; }

 private:
  State state = State::DONE;
  std::vector<std::string> bookPaths;
  std::string libraryPath;  // captured at onEnter for display on the DONE screen
  size_t currentIndex = 0;
  int generatedCount = 0;
  int skippedCount = 0;      // already had a thumbnail
  int lowMemorySkipped = 0;  // refused by the heap floor (BUG-006)
  int coverW = 0;
  int coverH = 0;

  // Accumulated during the run and written out once it completes (CGV-010).
  MosaicLibraryScan::Fingerprint fingerprint;
  std::vector<MosaicLibraryIndex::Entry> indexEntries;

  void generateNext();
  void indexNext();
  void saveIndex() const;
};
