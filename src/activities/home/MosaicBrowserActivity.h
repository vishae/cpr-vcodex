#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../Activity.h"
#include "MosaicGrid.h"
#include "MosaicLibraryIndex.h"
#include "MosaicLibraryScan.h"
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
  static constexpr int GRID_COLS = MosaicGrid::COLS;
  static constexpr int GRID_ROWS = MosaicGrid::ROWS;
  static constexpr int BOOKS_PER_PAGE = MosaicGrid::PER_PAGE;  // 9

 private:
  struct GridBook {
    std::string path;
    std::string label;          // filename stem at scan time, upgraded to the title once metadata loads
    std::string coverBmpPath;   // base thumb path, resolved when the book is indexed
    bool loaded = false;        // metadata + thumb attempted for this book
    bool coverSkipped = false;  // thumb generation deferred on low memory (BUG-006); retried when the page changes
    std::string author;         // populated eagerly when grouping is active (CGV-002)
    std::string series;
    float seriesIndex = -1.0f;
  };

  ButtonNavigator buttonNavigator;
  size_t selectorIndex = 0;
  unsigned long lastInputMs = 0;  // for the idle gate before generating a cover
  std::vector<GridBook> books;
  std::vector<GridBook> allBooksForGrouping;  // full metadata-loaded list, cached so Back can re-show the group picker without a rescan (CGV-002)
  std::string libraryPath = "/books";
  uint8_t grouping = 0;  // session copy of SETTINGS.mosaicDefaultGrouping, set in onEnter (CGV-002)

  // Missing/empty-folder info dialog (CGV-005/CGV-011 v2): a small dismissable
  // overlay drawn over the (empty) grid, not a separate full-screen activity.
  bool infoDialogVisible = false;
  bool infoDialogMissing = false;  // true = folder doesn't exist; false = folder exists but has no books

  // Tile geometry, computed once in onEnter from screen size + theme metrics
  // and handed to the shared painter on each render (DEC-011).
  MosaicGrid::Layout layout;

  void computeLayout();
  void loadBooks();
  int pageStartFor(size_t index) const;
  int visiblePagePending() const;  // index of the next un-indexed book on the visible page, or -1
  void retrySkippedCoversOnPageChange();  // re-arm low-memory skips when the visible page moves (BUG-006)
  int lastPageStart = -1;
  void indexBook(int i);

  // Library-folder setting + missing/empty-folder handling (CGV-005/CGV-011).
  void checkLibraryFolder();
  void onPickFolderResult(const ActivityResult& result);
  void finishLoadingBooks();  // called after any successful loadBooks(); routes into grouping if active

  // Persisted library index (CGV-010): serves the grouping metadata from disk
  // when the library fingerprint still matches, skipping the per-book pass.
  //
  // Three outcomes, not two — each gets different UI:
  //   Fresh  - render straight from the index, no metadata reads at all.
  //   Stale  - an index for this folder exists but the library has changed
  //            since; prompt (update now / continue with what's cached).
  //   Absent - no index for this folder at all; prompt with "no cache yet"
  //            wording. Also covers an index built for a different folder.
  enum class IndexStatus { Fresh, Stale, Absent };

  MosaicLibraryScan::Fingerprint currentFingerprint;
  MosaicLibraryIndex::Index loadedIndex;  // held between the check and the apply/prompt that follows it
  IndexStatus checkIndex();               // loads the index into loadedIndex and classifies it
  void applyIndexEntries();               // fills books from loadedIndex; books it doesn't know are left as scanned
  void releaseIndex();                    // frees loadedIndex's heap once its contents are in `books`
  void saveIndex() const;
  void promptIndexUpdate(IndexStatus status);
  void onIndexPromptResult(const ActivityResult& result, IndexStatus status);
  void continueWithoutUpdate(IndexStatus status);  // the "declined the update" path into the grid
  // Set once she declines an update prompt in this session, so the eager
  // folder-change prompt isn't immediately followed by the grouping-open one
  // asking the same question again.
  bool indexPromptDeclined = false;
  void rebuildIndexFromScratch();  // full per-book metadata pass + save
  void continueToGroupPicker();

  // Grouping (CGV-002): eager author/series pass + two-step group picker.
  std::string lastGroupName;                             // group last opened, so Back re-opens the picker on it (BUG-007)
  std::string fallbackGroupName() const;                 // "Standalone books" by series, "Unknown" by author
  std::string groupKeyFor(const GridBook& book) const;   // shared by the picker and the filter
  void loadGroupMetadata();
  void launchGroupPicker();
  void onGroupPickerResult(const ActivityResult& result);
  void applyGroupFilter(const std::string& group);
  void reshowGroupPicker();  // Back from a filtered grid returns here instead of Home

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
