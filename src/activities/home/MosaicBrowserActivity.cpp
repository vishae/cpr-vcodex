#include "MosaicBrowserActivity.h"

#include <Arduino.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MosaicGridMetrics.h"
#include "MosaicGroupPickerActivity.h"
#include "MosaicIndexPromptActivity.h"
#include "MosaicLibraryIndex.h"
#include "MosaicLibraryScan.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/settings/MosaicMetadataGenerateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kGenerateIdleMs = 250;   // wait this long after input before indexing a cover
constexpr char kCacheDir[] = "/.crosspoint";

std::string fileStem(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
  return name;
}

// Greedy word-wrap for the info dialog (CGV-011) — the dialog's line count varies
// with path length/font, so lines are measured and wrapped rather than fixed.
std::vector<std::string> wrapText(GfxRenderer& renderer, int fontId, const std::string& text, int maxWidth) {
  std::vector<std::string> lines;
  std::string current;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t spacePos = text.find(' ', pos);
    const std::string word = text.substr(pos, spacePos == std::string::npos ? std::string::npos : spacePos - pos);
    const std::string candidate = current.empty() ? word : current + " " + word;
    if (!current.empty() && renderer.getTextWidth(fontId, candidate.c_str()) > maxWidth) {
      lines.push_back(current);
      current = word;
    } else {
      current = candidate;
    }
    if (spacePos == std::string::npos) break;
    pos = spacePos + 1;
  }
  if (!current.empty()) lines.push_back(current);
  return lines;
}
}  // namespace

void MosaicBrowserActivity::computeLayout() { layout = MosaicGrid::computeLayout(renderer); }

void MosaicBrowserActivity::loadBooks() {
  books.clear();

  // Recursive walk of the library folder (CGV-004), skipping hidden/system
  // folders and the completed-books directory so those never appear in the grid.
  for (auto& path : MosaicLibraryScan::scanBookPaths(libraryPath, &currentFingerprint)) {
    books.push_back(GridBook{path, fileStem(path), "", false});
  }

  std::sort(books.begin(), books.end(),
            [](const GridBook& a, const GridBook& b) { return a.label < b.label; });
}

int MosaicBrowserActivity::pageStartFor(size_t index) const {
  return static_cast<int>(index / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
}

int MosaicBrowserActivity::visiblePagePending() const {
  const int pageStart = pageStartFor(selectorIndex);
  const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, static_cast<int>(books.size()));
  for (int i = pageStart; i < pageEnd; ++i) {
    if (!books[i].loaded) return i;
  }
  return -1;
}

// Build one book's metadata cache + cover thumbnail. Blocking (single core),
// so it's only ever called for one book per idle loop tick.
void MosaicBrowserActivity::indexBook(int i) {
  GridBook& book = books[i];
  book.loaded = true;

  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, kCacheDir);
    // Metadata-only load: parses just the OPF (title + cover) and skips the
    // expensive spine-size build. Reuses the full cache if already indexed.
    if (epub.loadMetadataOnly()) {
      const std::string& title = epub.getTitle();
      if (!title.empty()) book.label = title;
      book.coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, layout.coverW, layout.coverH);
      if (!Storage.exists(thumb.c_str())) {
        epub.generateThumbBmp(layout.coverW, layout.coverH);
      }
    }
  }
  // .xtc covers are handled by a later step; they keep the placeholder (still
  // labelled by filename).
}

void MosaicBrowserActivity::onEnter() {
  Activity::onEnter();
  computeLayout();
  selectorIndex = 0;
  lastInputMs = millis();
  grouping = SETTINGS.mosaicDefaultGrouping;
  checkLibraryFolder();
}

void MosaicBrowserActivity::checkLibraryFolder() {
  if (!Storage.exists(libraryPath.c_str())) {
    // Folder doesn't exist — show the small info dialog over the (empty) grid
    // rather than a separate screen; Confirm still reaches the picker (see loop()).
    books.clear();
    infoDialogVisible = true;
    infoDialogMissing = true;
    requestUpdate();
    return;
  }

  loadBooks();
  finishLoadingBooks();
}

void MosaicBrowserActivity::finishLoadingBooks() {
  if (books.empty()) {
    infoDialogVisible = true;
    infoDialogMissing = false;
    requestUpdate();
    return;
  }
  infoDialogVisible = false;

  if (grouping == CrossPointSettings::MOSAIC_GROUPING_NONE) {
    requestUpdate();
    return;
  }
  // Persisted index (CGV-010): on a fingerprint match this serves title/author/
  // series straight from disk, skipping the per-book metadata pass entirely.
  const IndexStatus status = checkIndex();
  if (status == IndexStatus::Fresh) {
    applyIndexEntries();
    continueToGroupPicker();
    return;
  }

  // Stale or absent — ask before spending minutes rebuilding, instead of
  // silently making her wait for it. Unless she already said no once this
  // session (the eager folder-change prompt), in which case don't re-ask.
  if (indexPromptDeclined) {
    continueWithoutUpdate(status);
    return;
  }
  promptIndexUpdate(status);
}

// Common tail of every path into the grid: cache the full list so Back from a
// filtered grid can re-show the picker without a rescan, then show the picker.
void MosaicBrowserActivity::continueToGroupPicker() {
  allBooksForGrouping = books;
  LOG_DBG("MOSAIC", "Group picker: %u books, free=%u largest=%u", static_cast<unsigned>(books.size()),
          ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  launchGroupPicker();
}

// Load the persisted index (CGV-010) into loadedIndex and classify it against
// the library just walked. Absent covers "no index file", "unreadable", and
// "built for a different folder" — all of which mean nothing is cached for this
// path. A fingerprint mismatch, or a scanned book the index doesn't know about,
// is Stale: the index can still be read from, it's just incomplete.
MosaicBrowserActivity::IndexStatus MosaicBrowserActivity::checkIndex() {
  loadedIndex = MosaicLibraryIndex::Index{};
  if (!MosaicLibraryIndex::load(loadedIndex)) return IndexStatus::Absent;
  if (loadedIndex.libraryPath != libraryPath) return IndexStatus::Absent;
  if (loadedIndex.fingerprint != currentFingerprint) return IndexStatus::Stale;

  // A same-count, same-total-size swap passes the fingerprint but can still
  // leave a scanned book unknown to the index — treat that as stale rather than
  // showing a book with no author/series (Serena's call, 2026-08-09).
  std::unordered_map<std::string, const MosaicLibraryIndex::Entry*> byPath;
  byPath.reserve(loadedIndex.entries.size());
  for (const auto& entry : loadedIndex.entries) byPath.emplace(entry.path, &entry);
  for (const auto& book : books) {
    if (byPath.find(book.path) == byPath.end()) return IndexStatus::Stale;
  }
  return IndexStatus::Fresh;
}

// Fill the scanned books from loadedIndex. Books the index doesn't know about
// (the stale case) keep their scanned filename label and empty author/series,
// so they still appear — under "Unknown" — rather than vanishing.
//
// No existence check is needed here: `books` comes from the walk this open just
// performed, so every path in it exists. A deleted book can't be listed from a
// stale index because the index isn't what produces the list.
void MosaicBrowserActivity::applyIndexEntries() {
  std::unordered_map<std::string, const MosaicLibraryIndex::Entry*> byPath;
  byPath.reserve(loadedIndex.entries.size());
  for (const auto& entry : loadedIndex.entries) byPath.emplace(entry.path, &entry);

  for (auto& book : books) {
    const auto found = byPath.find(book.path);
    if (found == byPath.end()) continue;
    const MosaicLibraryIndex::Entry& entry = *found->second;
    if (!entry.title.empty()) book.label = entry.title;
    book.author = entry.author;
    book.series = entry.series;
    book.seriesIndex = entry.seriesIndex;
  }

  // Release the index now its contents live in `books`. Holding it for the rest
  // of the session is a second full copy of the library's metadata, and opening
  // a book needs every byte it can get — decompressing a cover out of an EPUB
  // allocates tens of KB, and on 320 KB of RAM that copy is enough to make the
  // allocation fail (crash on entering a group, 2026-08-09).
  releaseIndex();
}

// Free loadedIndex's heap, capacity included — clear() alone keeps the buffer.
void MosaicBrowserActivity::releaseIndex() {
  loadedIndex.libraryPath.clear();
  loadedIndex.libraryPath.shrink_to_fit();
  std::vector<MosaicLibraryIndex::Entry>().swap(loadedIndex.entries);
}

// The full per-book metadata pass — the ~14 s at 40 books this feature exists to
// avoid — followed by persisting the result so the next open doesn't pay it.
void MosaicBrowserActivity::rebuildIndexFromScratch() {
  releaseIndex();  // whatever was loaded is about to be replaced; don't hold it while parsing every book
  loadGroupMetadata();
  saveIndex();
}

// Stale / no-cache-yet prompt (CGV-010). Confirm runs the bulk cover+metadata
// generation, which writes a fresh index on its way out; cancel carries on with
// whatever is cached (stale) or with the one-off metadata pass (absent).
void MosaicBrowserActivity::promptIndexUpdate(IndexStatus status) {
  const auto mode =
      status == IndexStatus::Stale ? MosaicIndexPromptActivity::Mode::Stale : MosaicIndexPromptActivity::Mode::NoCache;
  startActivityForResult(
      std::make_unique<MosaicIndexPromptActivity>(renderer, mappedInput, mode, libraryPath),
      [this, status](const ActivityResult& result) { onIndexPromptResult(result, status); });
}

void MosaicBrowserActivity::onIndexPromptResult(const ActivityResult& result, IndexStatus status) {
  if (!result.isCancelled) {
    // Update now — bulk-generate covers and metadata for every book (CGV-008),
    // which also saves a fresh index. Re-check it on the way back so this open
    // is served from that index instead of repeating the pass.
    startActivityForResult(std::make_unique<MosaicMetadataGenerateActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             if (checkIndex() == IndexStatus::Fresh) {
                               applyIndexEntries();
                             } else {
                               rebuildIndexFromScratch();
                             }
                             continueToGroupPicker();
                           });
    return;
  }

  indexPromptDeclined = true;
  continueWithoutUpdate(status);
}

void MosaicBrowserActivity::continueWithoutUpdate(IndexStatus status) {
  if (status == IndexStatus::Stale) {
    // Continue with what's cached: books the index knows keep their metadata,
    // the rest group under "Unknown" until the next update. Deliberately not
    // saved — a part-known library must not overwrite the index and then look
    // fresh next open.
    applyIndexEntries();
  } else {
    // Nothing cached to continue from, so this falls back to the pre-CGV-010
    // behaviour: pay the metadata pass once, but keep the result this time.
    rebuildIndexFromScratch();
  }
  continueToGroupPicker();
}

// Persist the metadata just computed by loadGroupMetadata(), stamped with the
// fingerprint of the walk that produced it, so the next open can skip that pass.
void MosaicBrowserActivity::saveIndex() const {
  MosaicLibraryIndex::Index index;
  index.libraryPath = libraryPath;
  index.fingerprint = currentFingerprint;
  index.entries.reserve(books.size());
  for (const auto& book : books) {
    index.entries.push_back(MosaicLibraryIndex::Entry{book.path, book.label, book.author, book.series,
                                                      book.seriesIndex});
  }
  MosaicLibraryIndex::save(index);
}

// Eager metadata-only pass (title/author/series) over every scanned book, so the
// group picker has real names to list. Only run when grouping is active — the
// default (no grouping) path stays as cheap as before this feature.
void MosaicBrowserActivity::loadGroupMetadata() {
  for (auto& book : books) {
    if (!FsHelpers::hasEpubExtension(book.path)) continue;

    Epub epub(book.path, kCacheDir);
    if (epub.loadMetadataOnly()) {
      const std::string& title = epub.getTitle();
      if (!title.empty()) book.label = title;
      book.author = epub.getAuthor();
      book.series = epub.getSeries();
      book.seriesIndex = epub.getSeriesIndex();
      // Deliberately not storing the thumb path here: it's a pure hash of the
      // book path, so the few series tiles that need one derive it on demand.
      // Storing it per book costs a string across two copies of the list, and
      // RAM is the binding constraint on this device.
    }
  }
}

// The bucket a book with no author/series falls into. Deliberately different
// wording per grouping type (CGV-002): a book with no series isn't "unknown",
// it's a standalone; a book with no author genuinely is unknown.
std::string MosaicBrowserActivity::fallbackGroupName() const {
  return grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES ? tr(STR_STANDALONE_BOOKS) : tr(STR_UNKNOWN_GROUP);
}

// The group a book belongs to under the active grouping, with the fallback
// bucket applied. Shared by the picker and the filter so the two can't disagree
// about which bucket a book is in — if they did, selecting the bucket would
// filter to nothing.
std::string MosaicBrowserActivity::groupKeyFor(const GridBook& book) const {
  const std::string key = (grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES) ? book.series : book.author;
  return key.empty() ? fallbackGroupName() : key;
}

void MosaicBrowserActivity::launchGroupPicker() {
  const bool bySeries = grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES;
  const std::string fallback = fallbackGroupName();

  // Distinct group names, alphabetical.
  std::vector<std::string> keys;
  keys.reserve(books.size());
  for (const auto& book : books) keys.push_back(groupKeyFor(book));
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

  // The fallback bucket is pinned 2nd, right after "All books", instead of
  // sorting alphabetically — so it's always in the same place regardless of
  // what it's called or what else is in the library (CGV-002).
  const auto fallbackIt = std::find(keys.begin(), keys.end(), fallback);
  const bool hasFallback = fallbackIt != keys.end();
  if (hasFallback) keys.erase(fallbackIt);

  std::vector<MosaicGroupPickerActivity::Group> groups;
  groups.reserve(keys.size() + 2);
  groups.push_back({tr(STR_ALL_BOOKS), ""});
  if (hasFallback) groups.push_back({fallback, ""});
  for (auto& key : keys) groups.push_back({std::move(key), ""});

  // Series tiles carry the cover of their lowest-seriesIndex book — the first in
  // the series, which is the one that reads as its identity. Author tiles never
  // carry a cover (an author isn't one visual identity, CGV-002), and neither do
  // "All books" or the fallback bucket, so those keep the placeholder.
  if (bySeries) {
    std::unordered_map<std::string, const GridBook*> representative;
    for (const auto& book : books) {
      if (book.series.empty()) continue;
      const auto existing = representative.find(book.series);
      if (existing == representative.end() || book.seriesIndex < existing->second->seriesIndex) {
        representative[book.series] = &book;
      }
    }
    for (auto& group : groups) {
      const auto found = representative.find(group.name);
      if (found == representative.end()) continue;
      const GridBook& book = *found->second;
      // On an index-served open no EPUB is opened, so coverBmpPath is empty —
      // but the thumb path is a pure hash of the book path, so it can be derived
      // without touching the file. If no thumb has been generated yet the tile
      // just falls back to the placeholder (Serena's call: don't generate here).
      group.coverBmpPath =
          book.coverBmpPath.empty() ? Epub(book.path, kCacheDir).getThumbBmpPath() : book.coverBmpPath;
    }
  }

  const uint8_t display = bySeries ? SETTINGS.mosaicSeriesGroupDisplay : SETTINGS.mosaicAuthorGroupDisplay;
  const bool useGrid = display == CrossPointSettings::MOSAIC_GROUP_DISPLAY_GRID;

  // Reopen on the group last chosen, so Back out of a group lands where it left
  // rather than at the top (BUG-007). Matched by name, not by index — the list
  // is rebuilt each time and a library change can reorder it.
  size_t initialIndex = 0;
  if (!lastGroupName.empty()) {
    for (size_t i = 0; i < groups.size(); ++i) {
      if (groups[i].name == lastGroupName) {
        initialIndex = i;
        break;
      }
    }
  }

  startActivityForResult(
      std::make_unique<MosaicGroupPickerActivity>(renderer, mappedInput, std::move(groups), useGrid, initialIndex),
      [this](const ActivityResult& result) { onGroupPickerResult(result); });
}

void MosaicBrowserActivity::onGroupPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    onGoHome();
    return;
  }

  const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
  if (!keyboard) {
    requestUpdate();
    return;
  }

  selectorIndex = 0;
  lastGroupName = keyboard->text;  // so Back re-opens the picker on this group (BUG-007)
  const std::string allBooksLabel = tr(STR_ALL_BOOKS);
  applyGroupFilter(keyboard->text == allBooksLabel ? "" : keyboard->text);
  requestUpdate();
}

// Filters `books` in place down to the chosen group (empty = "All books", keeps
// everything); when grouping by series, also re-sorts by seriesIndex so a
// series reads in order instead of the default alphabetical-by-title sort.
void MosaicBrowserActivity::applyGroupFilter(const std::string& group) {
  if (group.empty()) return;

  const bool bySeries = grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES;
  books.erase(
      std::remove_if(books.begin(), books.end(), [&](const GridBook& book) { return groupKeyFor(book) != group; }),
      books.end());

  if (bySeries) {
    std::sort(books.begin(), books.end(), [](const GridBook& a, const GridBook& b) {
      if (a.seriesIndex != b.seriesIndex) {
        if (a.seriesIndex < 0) return false;
        if (b.seriesIndex < 0) return true;
        return a.seriesIndex < b.seriesIndex;
      }
      return a.label < b.label;
    });
  }
}

// Back from a filtered grid comes here instead of Home — restore the full
// (already metadata-loaded) list and re-show the picker, no rescan needed.
void MosaicBrowserActivity::reshowGroupPicker() {
  books = allBooksForGrouping;
  selectorIndex = 0;
  launchGroupPicker();
}

void MosaicBrowserActivity::onPickFolderResult(const ActivityResult& result) {
  if (result.isCancelled) {
    // Back out of the picker to the missing-folder popup rather than exiting the view entirely.
    checkLibraryFolder();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    checkLibraryFolder();
    return;
  }

  libraryPath = path->path;
  strncpy(SETTINGS.libraryFolder, libraryPath.c_str(), sizeof(SETTINGS.libraryFolder) - 1);
  SETTINGS.libraryFolder[sizeof(SETTINGS.libraryFolder) - 1] = '\0';
  SETTINGS.saveToFile();
  indexPromptDeclined = false;  // new folder, so the earlier "no" no longer applies

  // The new folder has no index (CGV-010/CGV-011). Offer to build it now rather
  // than waiting for the next grouping open to discover it — the covers are
  // worth pre-generating even with grouping off.
  startActivityForResult(
      std::make_unique<MosaicIndexPromptActivity>(renderer, mappedInput, MosaicIndexPromptActivity::Mode::NoCache,
                                                  libraryPath),
      [this](const ActivityResult& promptResult) {
        if (promptResult.isCancelled) {
          indexPromptDeclined = true;
          checkLibraryFolder();
          return;
        }
        startActivityForResult(std::make_unique<MosaicMetadataGenerateActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) { checkLibraryFolder(); });
      });
}

void MosaicBrowserActivity::onExit() {
  Activity::onExit();
  books.clear();
  allBooksForGrouping.clear();
}

bool MosaicBrowserActivity::skipLoopDelay() {
  // Keep the loop running at full speed while the visible page still has covers
  // to index, so generation proceeds tick by tick between input checks.
  return visiblePagePending() >= 0;
}

void MosaicBrowserActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastInputMs = millis();
    if (!books.empty() && selectorIndex < books.size()) {
      // Selection-time guard (CGV-010): the card can be pulled or edited on a
      // computer between the scan and this press, so confirm the file is still
      // there rather than handing a dead path to the reader. Cheap — an
      // existence check, no zip or metadata work.
      const std::string selectedPath = books[selectorIndex].path;
      if (!Storage.exists(selectedPath.c_str())) {
        checkLibraryFolder();  // re-walk; the book has gone, so re-list what's actually there
        return;
      }
      onSelectBook(selectedPath);
    } else if (books.empty()) {
      // Folder missing or has no books — offer the picker either way, instead
      // of dead-ending on an empty grid with only Home to press.
      startActivityForResult(
          std::make_unique<FileBrowserActivity>(renderer, mappedInput, libraryPath,
                                                FileBrowserActivity::Mode::PickFolder),
          [this](const ActivityResult& pickResult) { onPickFolderResult(pickResult); });
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lastInputMs = millis();
    if (infoDialogVisible) {
      // First Back just dismisses the info dialog; a second press then goes Home.
      infoDialogVisible = false;
      requestUpdate();
      return;
    }
    if (grouping != CrossPointSettings::MOSAIC_GROUPING_NONE && !allBooksForGrouping.empty()) {
      // Grouping active — Back returns to the group picker (a level between
      // Home and the grid), not straight out to Home.
      reshowGroupPicker();
      return;
    }
    onGoHome();
    return;
  }

  const int listSize = static_cast<int>(books.size());
  bool navigated = false;
  if (listSize > 0) {
    using Btn = MappedInputManager::Button;
    // Left/Right move one book (reading order, wrapping); Up/Down move a full
    // row, staying in the same column (clamped at the top/bottom row).
    auto moveRight = [this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
      navigated = true;
    };
    auto moveLeft = [this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
      navigated = true;
    };
    auto moveDown = [this, listSize, &navigated] {
      if (static_cast<int>(selectorIndex) + GRID_COLS < listSize) {
        selectorIndex += GRID_COLS;
        navigated = true;
      }
    };
    auto moveUp = [this, &navigated] {
      if (selectorIndex >= static_cast<size_t>(GRID_COLS)) {
        selectorIndex -= GRID_COLS;
        navigated = true;
      }
    };
    buttonNavigator.onRelease({Btn::Right}, moveRight);
    buttonNavigator.onRelease({Btn::Left}, moveLeft);
    buttonNavigator.onRelease({Btn::Down}, moveDown);
    buttonNavigator.onRelease({Btn::Up}, moveUp);
    // Hold-to-repeat for fast traversal.
    buttonNavigator.onContinuous({Btn::Right}, moveRight);
    buttonNavigator.onContinuous({Btn::Left}, moveLeft);
    buttonNavigator.onContinuous({Btn::Down}, moveDown);
    buttonNavigator.onContinuous({Btn::Up}, moveUp);
  }
  if (navigated) {
    lastInputMs = millis();
    requestUpdate();
    return;
  }

  // Idle-gated incremental cover indexing for the visible page: only kick off a
  // (blocking) generation once the user has paused, so active scrolling stays
  // responsive.
  const int next = visiblePagePending();
  if (next >= 0 && (millis() - lastInputMs) >= kGenerateIdleMs) {
    indexBook(next);
    requestUpdate();
  }
}

void MosaicBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  const int total = static_cast<int>(books.size());
  const int pageStart = pageStartFor(selectorIndex);

  // Header (with page indicator when there's more than one page).
  std::string title = tr(STR_COVER_GRID);
  if (total > BOOKS_PER_PAGE) {
    const int totalPages = (total + BOOKS_PER_PAGE - 1) / BOOKS_PER_PAGE;
    const int currentPage = pageStart / BOOKS_PER_PAGE + 1;
    title += "  " + std::to_string(currentPage) + "/" + std::to_string(totalPages);
  }
  GUI.drawHeader(renderer, Rect{0, m.topPadding, pageWidth, m.headerHeight}, title.c_str());

  // TEMPORARY (BUG-006 measurement): the crash happens while a group's covers
  // are generated, so the readout has to be visible here too. Remove with the
  // picker's copy once the headroom is known.
  MosaicGrid::drawHeapDebugLine(renderer, m.topPadding + m.headerHeight);

  if (total == 0) {
    if (!infoDialogVisible) {
      renderer.drawCenteredText(UI_10_FONT_ID, m.topPadding + m.headerHeight + 40, tr(STR_NO_FILES_FOUND));
    }
  } else {
    MosaicGrid::drawPage(
        renderer, layout, pageStart, total, static_cast<int>(selectorIndex),
        [this](const int index) { return books[index].label; },
        [this](const int index) { return books[index].coverBmpPath; });
  }

  const auto labels =
      mappedInput.mapLabels(infoDialogVisible ? tr(STR_BACK) : tr(STR_HOME), books.empty() ? tr(STR_CHOOSE_ANOTHER) : tr(STR_OPEN),
                            books.empty() ? "" : tr(STR_DIR_UP), books.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // First-time indexing overlay: the metadata-cache build for never-opened
  // books blocks the loop (single-core), so covers appear one at a time and the
  // grid isn't navigable until the visible page finishes. Show a centered popup
  // over the grid so it's clear what's happening.
  if (visiblePagePending() >= 0) {
    MosaicGrid::drawIndexingOverlay(renderer);
  }

  // Missing/empty-folder info dialog (CGV-005/CGV-011): a small dismissable
  // overlay, not a separate full-screen activity — same visual pattern as the
  // indexing overlay above. Back dismisses it (see loop()); Confirm ("Browse")
  // works whether it's showing or already dismissed.
  if (infoDialogVisible) {
    const int pageHeight = renderer.getScreenHeight();
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int pad = 14;
    const int lineGap = 4;
    const int boxW = std::min(pageWidth - 30, 380);
    const int maxLineWidth = boxW - 2 * pad;

    // Missing: path first ("/library doesn't exist..."). Empty: path parenthesised
    // at the end ("No books were found in this folder (/library)") — Serena's wording.
    const std::string body = infoDialogMissing ? libraryPath + " " + tr(STR_LIBRARY_FOLDER_MISSING_BODY)
                                               : std::string(tr(STR_LIBRARY_FOLDER_EMPTY_BODY)) + " (" + libraryPath + ")";

    std::vector<std::string> bodyLines = wrapText(renderer, UI_10_FONT_ID, body, maxLineWidth);
    std::vector<std::string> hintLines = wrapText(renderer, UI_10_FONT_ID, tr(STR_LIBRARY_FOLDER_DIALOG_HINT), maxLineWidth);
    std::vector<std::string> dismissLines =
        wrapText(renderer, UI_10_FONT_ID, tr(STR_LIBRARY_FOLDER_DIALOG_DISMISS), maxLineWidth);

    const int totalLines = 1 /* heading */ + static_cast<int>(bodyLines.size()) + static_cast<int>(hintLines.size()) +
                           static_cast<int>(dismissLines.size());
    // Content height: one row per line, plus a gap after the heading and after
    // the body block (matches the two extra gaps inserted while drawing below).
    const int boxH = totalLines * lineH + 2 * lineGap + pad * 2;
    const int boxX = (pageWidth - boxW) / 2;
    const int boxY = std::max(pad, (pageHeight - boxH) / 2);

    renderer.fillRoundedRect(boxX, boxY, boxW, boxH, 8, Color::White);
    renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 2, 8, true);
    int y = boxY + pad;
    renderer.drawCenteredText(UI_10_FONT_ID, y, infoDialogMissing ? tr(STR_LIBRARY_FOLDER_MISSING) : tr(STR_LIBRARY_FOLDER_EMPTY),
                              true, EpdFontFamily::BOLD);
    y += lineH + lineGap;
    for (const auto& line : bodyLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineH;
    }
    y += lineGap;
    for (const auto& line : hintLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineH;
    }
    for (const auto& line : dismissLines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineH;
    }
  }

  renderer.displayBuffer();
}
