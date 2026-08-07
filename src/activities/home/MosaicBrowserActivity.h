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
// folder is configurable, with a small dismissable info dialog (not a separate
// screen) when it's missing or has no books (CGV-005/CGV-011). Grouping by
// author/series via a two-step picker is CGV-002. Sort options (CGV-003) come
// later.
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
    std::string author;         // populated eagerly when grouping is active (CGV-002)
    std::string series;
    float seriesIndex = -1.0f;
  };

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  unsigned long lastInputMs = 0;  // for the idle gate before generating a cover
  std::vector<GridBook> books;
  std::string libraryPath = "/books";
  uint8_t grouping = 0;  // session copy of SETTINGS.mosaicDefaultGrouping, set in onEnter (CGV-002)

  // Missing/empty-folder info dialog (CGV-005/CGV-011 v2): a small dismissable
  // overlay drawn over the (empty) grid, not a separate full-screen activity.
  bool infoDialogVisible = false;
  bool infoDialogMissing = false;  // true = folder doesn't exist; false = folder exists but has no books

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

  // Library-folder setting + missing/empty-folder handling (CGV-005/CGV-011).
  void checkLibraryFolder();
  void onPickFolderResult(const ActivityResult& result);
  void finishLoadingBooks();  // called after any successful loadBooks(); routes into grouping if active

  // Grouping (CGV-002): eager author/series pass + two-step group picker.
  void loadGroupMetadata();
  void launchGroupPicker();
  void onGroupPickerResult(const ActivityResult& result);
  void applyGroupFilter(const std::string& group);

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
