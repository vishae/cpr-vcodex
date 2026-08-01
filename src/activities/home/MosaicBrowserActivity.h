#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// KOReader-style mosaic library browser: a 3x3 grid of book cover thumbnails
// scanned from a library folder (default /books), paged when there are more
// than 9 books. On-demand thumbnail generation, reusing the firmware's existing
// cover pipeline (Epub::generateThumbBmp / UITheme::getCoverThumbPath).
//
// Added alongside the plain-list FileBrowserActivity (PID-26028, DEC-002) and
// registered as its own reorderable Apps shortcut (DEC-004). Step 1: grid +
// paging + thumbnails only; grouping/sorting (CGV-002/003) and the library-
// folder setting + missing-folder popup (CGV-005) come later.
class MosaicBrowserActivity final : public Activity {
 public:
  static constexpr int GRID_COLS = 3;
  static constexpr int GRID_ROWS = 3;
  static constexpr int BOOKS_PER_PAGE = GRID_COLS * GRID_ROWS;  // 9

 private:
  struct GridBook {
    std::string path;
    std::string coverBmpPath;   // base thumb path, resolved when the page loads
    bool coverAttempted = false;
  };

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  int loadedPageStart = -1;
  std::vector<GridBook> books;
  std::string libraryPath = "/books";

  // Layout, computed once in onEnter from screen size + theme metrics.
  int coverW = 0;
  int coverH = 0;
  int gapX = 10;
  int gapY = 10;
  int gridX0 = 0;
  int gridY0 = 0;

  void computeLayout();
  void loadBooks();
  void loadPageCovers(int pageStart);
  int pageStartFor(size_t index) const;

 public:
  explicit MosaicBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string libraryPath = "/books")
      : Activity("MosaicBrowser", renderer, mappedInput),
        libraryPath(libraryPath.empty() ? "/books" : std::move(libraryPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
