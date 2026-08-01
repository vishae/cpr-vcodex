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

#include "MappedInputManager.h"
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
}  // namespace

void MosaicBrowserActivity::computeLayout() {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int side = m.contentSidePadding;
  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentBottom = pageHeight - m.buttonHintsHeight - m.verticalSpacing;
  const int contentW = pageWidth - 2 * side;
  const int contentH = contentBottom - contentTop;

  labelH = renderer.getLineHeight(SMALL_FONT_ID);

  const int maxCoverW = (contentW - (GRID_COLS - 1) * gapX) / GRID_COLS;
  const int maxCellH = (contentH - (GRID_ROWS - 1) * gapY) / GRID_ROWS;
  const int coverBudgetH = std::max(16, maxCellH - labelH - labelGap);

  // Fit a 2:3 cover inside the per-cell budget (width- or height-limited).
  coverW = std::min(maxCoverW, coverBudgetH * 2 / 3);
  if (coverW < 8) coverW = 8;
  coverH = coverW * 3 / 2;
  if (coverH > coverBudgetH) {
    coverH = coverBudgetH;
    coverW = coverH * 2 / 3;
  }

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
  std::vector<std::string> stack;
  stack.push_back(libraryPath);
  char nameBuf[512];

  while (!stack.empty()) {
    const std::string dirPath = std::move(stack.back());
    stack.pop_back();

    auto dir = Storage.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    dir.rewindDirectory();

    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      const bool isDir = file.isDirectory();
      file.getName(nameBuf, sizeof(nameBuf));

      if (nameBuf[0] == '.' || strcmp(nameBuf, "System Volume Information") == 0 ||
          strcmp(nameBuf, "finished_books") == 0) {
        file.close();
        continue;
      }

      std::string full = dirPath;
      if (full.empty() || full.back() != '/') full += "/";
      full += nameBuf;

      if (isDir) {
        stack.push_back(full);
      } else {
        std::string_view filename{nameBuf};
        if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
          books.push_back(GridBook{full, fileStem(full), "", false});
        }
      }
      file.close();
    }
    dir.close();
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
    // buildIfMissing = true so never-opened books get their metadata cache
    // built here; without it, thumbnail generation silently no-ops.
    if (epub.load(true, true)) {
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
  loadBooks();
  requestUpdate();
}

void MosaicBrowserActivity::onExit() {
  Activity::onExit();
  books.clear();
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
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lastInputMs = millis();
    onGoHome();
    return;
  }

  const int listSize = static_cast<int>(books.size());
  bool navigated = false;
  if (listSize > 0) {
    buttonNavigator.onNextRelease([this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
      navigated = true;
    });
    buttonNavigator.onPreviousRelease([this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
      navigated = true;
    });
    // Continuous press jumps a full row for fast traversal.
    buttonNavigator.onNextContinuous([this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, GRID_COLS);
      navigated = true;
    });
    buttonNavigator.onPreviousContinuous([this, listSize, &navigated] {
      selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, GRID_COLS);
      navigated = true;
    });
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
    renderer.drawCenteredText(UI_10_FONT_ID, m.topPadding + m.headerHeight + 40, tr(STR_NO_FILES_FOUND));
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

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), books.empty() ? "" : tr(STR_OPEN),
                                            books.empty() ? "" : tr(STR_DIR_UP), books.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
