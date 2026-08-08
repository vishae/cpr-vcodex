#include "MosaicBrowserActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "MosaicGridMetrics.h"
#include "MosaicGroupPickerActivity.h"
#include "MosaicLibraryScan.h"
#include "activities/home/FileBrowserActivity.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCornerRadius = 6;
constexpr int kSelectPad = 3;                    // border inset around the selected cover
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

void MosaicBrowserActivity::computeLayout() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentBottom = pageHeight - m.buttonHintsHeight - m.verticalSpacing;
  const int contentH = contentBottom - contentTop;

  labelH = renderer.getLineHeight(SMALL_FONT_ID);

  const auto coverSize = MosaicGridMetrics::computeCoverSize(renderer);
  coverW = coverSize.width;
  coverH = coverSize.height;

  const int cellH = coverH + labelGap + labelH;
  const int totalGridW = GRID_COLS * coverW + (GRID_COLS - 1) * gapX;
  const int totalGridH = GRID_ROWS * cellH + (GRID_ROWS - 1) * gapY;
  gridX0 = (pageWidth - totalGridW) / 2;
  gridY0 = contentTop + std::max(0, (contentH - totalGridH) / 2);
}

void MosaicBrowserActivity::loadBooks() {
  books.clear();

  // Recursive walk of the library folder (CGV-004), skipping hidden/system
  // folders and the completed-books directory so those never appear in the grid.
  for (auto& path : MosaicLibraryScan::scanBookPaths(libraryPath)) {
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
      const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, coverW, coverH);
      if (!Storage.exists(thumb.c_str())) {
        epub.generateThumbBmp(coverW, coverH);
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
  loadGroupMetadata();
  allBooksForGrouping = books;  // cached so Back from the filtered grid can re-show the picker without a rescan
  launchGroupPicker();
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
    }
  }
}

void MosaicBrowserActivity::launchGroupPicker() {
  std::vector<std::string> groups;
  groups.emplace_back(tr(STR_ALL_BOOKS));

  std::vector<std::string> keys;
  keys.reserve(books.size());
  for (const auto& book : books) {
    std::string key = (grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES) ? book.series : book.author;
    if (key.empty()) key = tr(STR_UNKNOWN_GROUP);
    keys.push_back(std::move(key));
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  for (auto& key : keys) groups.push_back(std::move(key));

  startActivityForResult(std::make_unique<MosaicGroupPickerActivity>(renderer, mappedInput, std::move(groups)),
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
  const std::string allBooksLabel = tr(STR_ALL_BOOKS);
  applyGroupFilter(keyboard->text == allBooksLabel ? "" : keyboard->text);
  requestUpdate();
}

// Filters `books` in place down to the chosen group (empty = "All books", keeps
// everything); when grouping by series, also re-sorts by seriesIndex so a
// series reads in order instead of the default alphabetical-by-title sort.
void MosaicBrowserActivity::applyGroupFilter(const std::string& group) {
  if (group.empty()) return;

  const std::string unknown = tr(STR_UNKNOWN_GROUP);
  const bool bySeries = grouping == CrossPointSettings::MOSAIC_GROUPING_SERIES;
  books.erase(std::remove_if(books.begin(), books.end(),
                             [&](const GridBook& book) {
                               std::string key = bySeries ? book.series : book.author;
                               if (key.empty()) key = unknown;
                               return key != group;
                             }),
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

  checkLibraryFolder();
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
      onSelectBook(books[selectorIndex].path);
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

  if (total == 0) {
    if (!infoDialogVisible) {
      renderer.drawCenteredText(UI_10_FONT_ID, m.topPadding + m.headerHeight + 40, tr(STR_NO_FILES_FOUND));
    }
  } else {
    const int cellH = coverH + labelGap + labelH;
    const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, total);
    for (int i = pageStart; i < pageEnd; ++i) {
      const int slot = i - pageStart;
      const int row = slot / GRID_COLS;
      const int col = slot % GRID_COLS;
      const int x = gridX0 + col * (coverW + gapX);
      const int y = gridY0 + row * (cellH + gapY);

      bool hasCover = false;
      const GridBook& book = books[i];
      if (!book.coverBmpPath.empty()) {
        const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, coverW, coverH);
        FsFile file;
        if (Storage.openFileForRead("MOSAIC", thumb, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            const float bmpRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
            const float tileRatio = static_cast<float>(coverW) / static_cast<float>(coverH);
            const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
            renderer.drawBitmap(bitmap, x, y, coverW, coverH, cropX, 0.0f);
            renderer.maskRoundedRectOutsideCorners(x, y, coverW, coverH, kCornerRadius, Color::White);
            hasCover = true;
          }
          file.close();
        }
      }
      if (!hasCover) {
        renderer.drawRoundedRect(x, y, coverW, coverH, 1, kCornerRadius, true);
        renderer.drawIcon(CoverIcon, x + coverW / 2 - 16, y + coverH / 2 - 16, 32, 32);
      }

      // Title label under the cover (truncated to the cover width).
      std::string label = book.label;
      while (!label.empty() && renderer.getTextWidth(SMALL_FONT_ID, label.c_str()) > coverW) {
        label.pop_back();
      }
      const int labelW = renderer.getTextWidth(SMALL_FONT_ID, label.c_str());
      renderer.drawText(SMALL_FONT_ID, x + (coverW - labelW) / 2, y + coverH + labelGap, label.c_str());

      // Selection highlight: a thicker rounded border around the current cover.
      if (i == static_cast<int>(selectorIndex)) {
        renderer.drawRoundedRect(x - kSelectPad, y - kSelectPad, coverW + 2 * kSelectPad, coverH + 2 * kSelectPad, 3,
                                 kCornerRadius + kSelectPad, true);
      }
    }
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
    const int pageHeight = renderer.getScreenHeight();
    const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
    const int pad = 14;
    const int boxW = std::min(pageWidth - 40, 340);
    const int boxH = lineH * 2 + pad * 2 + 4;
    const int boxX = (pageWidth - boxW) / 2;
    const int boxY = (pageHeight - boxH) / 2;
    renderer.fillRoundedRect(boxX, boxY, boxW, boxH, 8, Color::White);
    renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 2, 8, true);
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + pad, tr(STR_COVER_GRID_INDEXING));
    renderer.drawCenteredText(UI_10_FONT_ID, boxY + pad + lineH + 4, tr(STR_COVER_GRID_INDEXING_WAIT));
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
