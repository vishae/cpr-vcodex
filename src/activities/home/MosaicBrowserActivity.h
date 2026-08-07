#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// KOReader-style mosaic library browser: a 3x3 grid of book cover thumbnails
// scanned (recursively) from a library folder (default /books), paged when
// there are more than 9 books, with the book title under each cover.
//
// Cover thumbnails are generated incrementally in loop() — one per idle tick,
// only for the currently visible page — so the grid appears instantly with
// placeholders and stays navigable while covers fill in, instead of blocking on
// a "generating" screen. (First-time generation is the one-time metadata-cache
// build per never-opened book, shared with the reader; it's cached afterwards.)
//
// Added alongside FileBrowserActivity (PID-26028, DEC-002), registered as a
// reorderable Apps shortcut (DEC-004). Recursive scan is CGV-004. The library
// folder is configurable with missing-folder handling (CGV-005). Grouping/sort
// (CGV-002/003) come later.
class MosaicBrowserActivity final : public Activity {
 public:
  static constexpr int GRID_COLS = 3;
  static constexpr int GRID_ROWS = 3;
  static constexpr int BOOKS_PER_PAGE = GRID_COLS * GRID_ROWS;  // 9

 private:
  struct GridBook {
    std::string path;
    std::string label;          // filename stem at scan time, upgraded to the title once metadata loads
    std::string coverBmpPath;   // base thumb path, resolved when the book is indexed
    bool loaded = false;        // metadata + thumb attempted for this book
  };

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  unsigned long lastInputMs = 0;  // for the idle gate before generating a cover
  std::vector<GridBook> books;
  std::string libraryPath = "/books";

  // Layout, computed once in onEnter from screen size + theme metrics.
  int coverW = 0;
  int coverH = 0;
  int labelH = 0;
  int gapX = 10;
  int gapY = 10;
  int labelGap = 2;
  int gridX0 = 0;
  int gridY0 = 0;

  void computeLayout();
  void loadBooks();
  int pageStartFor(size_t index) const;
  int visiblePagePending() const;  // index of the next un-indexed book on the visible page, or -1
  void indexBook(int i);

  // Library-folder setting + missing-folder handling (CGV-005).
  void checkLibraryFolder();
  void onMissingFolderResult(const ActivityResult& result);
  void onPickFolderResult(const ActivityResult& result);

 public:
  explicit MosaicBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string libraryPath = "/books")
      : Activity("MosaicBrowser", renderer, mappedInput),
        libraryPath(libraryPath.empty() ? "/books" : std::move(libraryPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override;
};
