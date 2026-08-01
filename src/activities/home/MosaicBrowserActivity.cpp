#include "MosaicBrowserActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCornerRadius = 6;
constexpr int kSelectPad = 3;      // border inset around the selected cover
constexpr char kCacheDir[] = "/.crosspoint";
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

  const int maxCoverW = (contentW - (GRID_COLS - 1) * gapX) / GRID_COLS;
  const int maxCoverH = (contentH - (GRID_ROWS - 1) * gapY) / GRID_ROWS;

  // Fit a 2:3 cover inside the per-cell budget (width- or height-limited).
  coverW = std::min(maxCoverW, maxCoverH * 2 / 3);
  if (coverW < 8) coverW = 8;
  coverH = coverW * 3 / 2;
  if (coverH > maxCoverH) {
    coverH = maxCoverH;
    coverW = coverH * 2 / 3;
  }

  const int totalGridW = GRID_COLS * coverW + (GRID_COLS - 1) * gapX;
  const int totalGridH = GRID_ROWS * coverH + (GRID_ROWS - 1) * gapY;
  gridX0 = (pageWidth - totalGridW) / 2;
  gridY0 = contentTop + std::max(0, (contentH - totalGridH) / 2);
}

void MosaicBrowserActivity::loadBooks() {
  books.clear();

  auto dir = Storage.open(libraryPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  dir.rewindDirectory();

  char nameBuf[512];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDir = file.isDirectory();
    file.getName(nameBuf, sizeof(nameBuf));
    if (isDir || nameBuf[0] == '.') {
      file.close();
      continue;
    }
    std::string_view filename{nameBuf};
    // Cover-capable formats only; the plain list browser still handles the rest.
    if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
      std::string full = libraryPath;
      if (full.empty() || full.back() != '/') full += "/";
      full += nameBuf;
      books.push_back(GridBook{std::move(full), "", false});
    }
    file.close();
  }
  dir.close();

  std::sort(books.begin(), books.end(),
            [](const GridBook& a, const GridBook& b) { return a.path < b.path; });
}

int MosaicBrowserActivity::pageStartFor(size_t index) const {
  return static_cast<int>(index / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
}

void MosaicBrowserActivity::loadPageCovers(int pageStart) {
  const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, static_cast<int>(books.size()));
  for (int i = pageStart; i < pageEnd; ++i) {
    GridBook& book = books[i];
    if (book.coverAttempted) continue;
    book.coverAttempted = true;

    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, kCacheDir);
      if (epub.load(false, true)) {  // metadata only, no CSS
        book.coverBmpPath = epub.getThumbBmpPath();
        const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, coverW, coverH);
        if (!Storage.exists(thumb.c_str())) {
          epub.generateThumbBmp(coverW, coverH);
        }
      }
    }
    // .xtc covers are handled by a later step; they fall back to the placeholder.
  }
  loadedPageStart = pageStart;
}

void MosaicBrowserActivity::onEnter() {
  Activity::onEnter();
  computeLayout();
  selectorIndex = 0;
  loadedPageStart = -1;
  loadBooks();
  requestUpdate();
}

void MosaicBrowserActivity::onExit() {
  Activity::onExit();
  books.clear();
}

void MosaicBrowserActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!books.empty() && selectorIndex < books.size()) {
      onSelectBook(books[selectorIndex].path);
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int listSize = static_cast<int>(books.size());
  if (listSize == 0) return;

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  // Continuous press jumps a full row for fast traversal.
  buttonNavigator.onNextContinuous([this, listSize] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, GRID_COLS);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, GRID_COLS);
    requestUpdate();
  });
}

void MosaicBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int total = static_cast<int>(books.size());
  const int pageStart = pageStartFor(selectorIndex);
  if (loadedPageStart != pageStart) {
    loadPageCovers(pageStart);
  }

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
    const int pageEnd = std::min(pageStart + BOOKS_PER_PAGE, total);
    for (int i = pageStart; i < pageEnd; ++i) {
      const int slot = i - pageStart;
      const int row = slot / GRID_COLS;
      const int col = slot % GRID_COLS;
      const int x = gridX0 + col * (coverW + gapX);
      const int y = gridY0 + row * (coverH + gapY);

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

  (void)pageHeight;
  renderer.displayBuffer();
}
