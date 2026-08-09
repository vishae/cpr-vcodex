#include "MosaicMetadataGenerateActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <esp_heap_caps.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/home/MosaicGrid.h"
#include "activities/home/MosaicGridMetrics.h"
#include "activities/home/MosaicLibraryIndex.h"
#include "activities/home/MosaicLibraryScan.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kCacheDir[] = "/.crosspoint";
}  // namespace

void MosaicMetadataGenerateActivity::onEnter() {
  Activity::onEnter();

  const auto coverSize = MosaicGridMetrics::computeCoverSize(renderer);
  coverW = coverSize.width;
  coverH = coverSize.height;

  libraryPath = SETTINGS.libraryFolder;
  bookPaths = MosaicLibraryScan::scanBookPaths(libraryPath, &fingerprint);
  indexEntries.clear();
  currentIndex = 0;
  generatedCount = 0;
  skippedCount = 0;
  lowMemorySkipped = 0;
  state = bookPaths.empty() ? State::DONE : State::GENERATING;

  requestUpdate();
}

// Pass 1: covers only. Nothing is retained between books, so the heap available
// to each decompression is the same for the last book as for the first.
void MosaicMetadataGenerateActivity::generateNext() {
  if (currentIndex >= bookPaths.size()) {
    currentIndex = 0;
    state = State::INDEXING;
    requestUpdate();
    return;
  }

  const std::string& path = bookPaths[currentIndex++];

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, kCacheDir);
    if (epub.loadMetadataOnly()) {
      const std::string coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(coverBmpPath, coverW, coverH);
      if (!Storage.exists(thumb.c_str())) {
        // Same floor the grid applies (BUG-006): refuse a generation that can't
        // finish rather than let a failed allocation abort the firmware. This
        // path has more headroom than the grid — no book lists are loaded — but
        // it was the last unguarded caller.
        if (heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) >= MosaicGrid::COVER_GENERATION_HEAP_FLOOR) {
          epub.generateThumbBmp(coverW, coverH);
          generatedCount++;
        } else {
          LOG_INF("MOSAIC", "Bulk generate skipped %s: largest free block %u below floor", path.c_str(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
          lowMemorySkipped++;
        }
      } else {
        skippedCount++;
      }
    }
  }

  if (currentIndex >= bookPaths.size()) {
    currentIndex = 0;
    state = State::INDEXING;
  }
  requestUpdate();
}

// Pass 2: build the index from the metadata caches pass 1 just warmed, so these
// reads are cache hits rather than OPF parses. Every scanned book gets an entry,
// even a .xtc with no metadata to read — the browser distrusts an index that
// doesn't know a scanned book.
void MosaicMetadataGenerateActivity::indexNext() {
  if (currentIndex >= bookPaths.size()) {
    state = State::DONE;
    saveIndex();
    requestUpdate();
    return;
  }

  const std::string& path = bookPaths[currentIndex++];
  MosaicLibraryIndex::Entry entry;
  entry.path = path;

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, kCacheDir);
    if (epub.loadMetadataOnly()) {
      entry.title = epub.getTitle();
      entry.author = epub.getAuthor();
      entry.series = epub.getSeries();
      entry.seriesIndex = epub.getSeriesIndex();
    }
  }
  indexEntries.push_back(std::move(entry));

  if (currentIndex >= bookPaths.size()) {
    state = State::DONE;
    saveIndex();
  }
  requestUpdate();
}

// Persist what this run computed as the Cover Grid's library index (CGV-010),
// so the grouping open that follows costs a folder walk and nothing more.
// Only called on a completed run — a part-generated index stamped with the
// full-library fingerprint would look fresh while missing books.
void MosaicMetadataGenerateActivity::saveIndex() const {
  MosaicLibraryIndex::Index index;
  index.libraryPath = libraryPath;
  index.fingerprint = fingerprint;
  index.entries = indexEntries;
  MosaicLibraryIndex::save(index);
}

void MosaicMetadataGenerateActivity::loop() {
  if (state == State::GENERATING || state == State::INDEXING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      // Abandoned part-way: no index is written, since one stamped with the
      // full-library fingerprint would look fresh while missing books.
      state = State::DONE;
      requestUpdate();
      return;
    }
    if (state == State::GENERATING) {
      generateNext();
    } else {
      indexNext();
    }
    return;
  }

  // Released, not pressed: SettingsActivity reads a Back *release* as "go to the
  // top of this category", so exiting on the press left the release to land
  // there and move the selection off the item just used.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void MosaicMetadataGenerateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GENERATE_METADATA));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::GENERATING || state == State::INDEXING) {
    const int total = static_cast<int>(bookPaths.size());
    const int pct = total > 0 ? static_cast<int>((currentIndex * 100) / total) : 100;

    renderer.drawCenteredText(UI_10_FONT_ID, top,
                              state == State::GENERATING ? tr(STR_GENERATING_METADATA) : tr(STR_BUILDING_INDEX), true,
                              EpdFontFamily::BOLD);

    int y = top + lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight}, pct,
        100);
    // drawProgressBar already draws its own "N%" label 15px below the bar (BaseTheme::drawProgressBar) —
    // clear that line before drawing the count, or the two overlap.
    y += metrics.progressBarHeight + 15 + lineHeight + metrics.verticalSpacing;

    const std::string countText = std::to_string(currentIndex) + " / " + std::to_string(total);
    renderer.drawCenteredText(UI_10_FONT_ID, y, countText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const int maxWidth = pageWidth - 40;
    const std::string heading = std::string(tr(STR_METADATA_GENERATION_COMPLETE)) + " " + libraryPath;
    const std::string safeHeading =
        renderer.truncatedText(UI_10_FONT_ID, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top, safeHeading.c_str(), true, EpdFontFamily::BOLD);

    std::string resultText = std::to_string(generatedCount) + " " + std::string(tr(STR_BOOKS_GENERATED));
    if (skippedCount > 0) {
      resultText += ", " + std::to_string(skippedCount) + " " + std::string(tr(STR_BOOKS_ALREADY_CACHED));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, resultText.c_str());

    // Say so when the heap floor refused some: silence would read as "all done"
    // while those books keep their placeholder (BUG-006).
    if (lowMemorySkipped > 0) {
      const std::string lowMemText =
          std::to_string(lowMemorySkipped) + " " + std::string(tr(STR_BOOKS_SKIPPED_LOW_MEMORY));
      renderer.drawCenteredText(UI_10_FONT_ID, top + (lineHeight + metrics.verticalSpacing) * 2, lowMemText.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
