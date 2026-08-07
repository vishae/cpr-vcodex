#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * On-demand bulk metadata + cover-thumbnail generation for the Cover Grid
 * mosaic browser (CGV-008). Runs the same per-book work
 * MosaicBrowserActivity::indexBook() does lazily on grid entry, but for every
 * book in the library up front — so a bulk book upload can be pre-indexed at
 * a convenient time instead of eating the cost while actually browsing.
 *
 * Launched from Settings > Apps > Cover Grid > "Generate all covers".
 */
class MosaicMetadataGenerateActivity final : public Activity {
 public:
  enum class State { GENERATING, DONE };

  explicit MosaicMetadataGenerateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MosaicMetadataGenerate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::GENERATING; }

 private:
  State state = State::DONE;
  std::vector<std::string> bookPaths;
  size_t currentIndex = 0;
  int generatedCount = 0;
  int skippedCount = 0;
  int coverW = 0;
  int coverH = 0;

  void generateNext();
};
