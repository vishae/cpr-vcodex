#include "MosaicGrid.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "MosaicGridMetrics.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"
#include "fontIds.h"

namespace {
constexpr int kCornerRadius = 6;
constexpr int kSelectPad = 3;  // border inset around the selected cover
constexpr int kPlaceholderIcon = 32;
}  // namespace

namespace MosaicGrid {

Layout computeLayout(GfxRenderer& renderer) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentBottom = pageHeight - m.buttonHintsHeight - m.verticalSpacing;
  const int contentH = contentBottom - contentTop;

  Layout layout;
  layout.labelH = renderer.getLineHeight(SMALL_FONT_ID);

  const auto coverSize = MosaicGridMetrics::computeCoverSize(renderer);
  layout.coverW = coverSize.width;
  layout.coverH = coverSize.height;

  const int cellH = layout.coverH + layout.labelGap + layout.labelH;
  const int totalGridW = COLS * layout.coverW + (COLS - 1) * layout.gapX;
  const int totalGridH = ROWS * cellH + (ROWS - 1) * layout.gapY;
  layout.gridX0 = (pageWidth - totalGridW) / 2;
  layout.gridY0 = contentTop + std::max(0, (contentH - totalGridH) / 2);
  return layout;
}

int pageStartFor(const int index) { return (index / PER_PAGE) * PER_PAGE; }

void drawPage(GfxRenderer& renderer, const Layout& layout, const int pageStart, const int total,
              const int selectedIndex, const std::function<std::string(int index)>& label,
              const std::function<std::string(int index)>& coverBmpPath) {
  const int cellH = layout.coverH + layout.labelGap + layout.labelH;
  const int pageEnd = std::min(pageStart + PER_PAGE, total);

  for (int i = pageStart; i < pageEnd; ++i) {
    const int slot = i - pageStart;
    const int x = layout.gridX0 + (slot % COLS) * (layout.coverW + layout.gapX);
    const int y = layout.gridY0 + (slot / COLS) * (cellH + layout.gapY);

    bool hasCover = false;
    const std::string basePath = coverBmpPath ? coverBmpPath(i) : std::string();
    if (!basePath.empty()) {
      const std::string thumb = UITheme::getCoverThumbPath(basePath, layout.coverW, layout.coverH);
      FsFile file;
      if (Storage.openFileForRead("MOSAIC", thumb, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          const float bmpRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
          const float tileRatio = static_cast<float>(layout.coverW) / static_cast<float>(layout.coverH);
          const float cropX = (bmpRatio > tileRatio) ? (1.0f - tileRatio / bmpRatio) : 0.0f;
          renderer.drawBitmap(bitmap, x, y, layout.coverW, layout.coverH, cropX, 0.0f);
          renderer.maskRoundedRectOutsideCorners(x, y, layout.coverW, layout.coverH, kCornerRadius, Color::White);
          hasCover = true;
        }
        file.close();
      }
    }
    if (!hasCover) {
      renderer.drawRoundedRect(x, y, layout.coverW, layout.coverH, 1, kCornerRadius, true);
      renderer.drawIcon(CoverIcon, x + layout.coverW / 2 - kPlaceholderIcon / 2,
                        y + layout.coverH / 2 - kPlaceholderIcon / 2, kPlaceholderIcon, kPlaceholderIcon);
    }

    // Label under the tile, truncated to the tile width.
    std::string text = label ? label(i) : std::string();
    while (!text.empty() && renderer.getTextWidth(SMALL_FONT_ID, text.c_str()) > layout.coverW) {
      text.pop_back();
    }
    const int textW = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
    renderer.drawText(SMALL_FONT_ID, x + (layout.coverW - textW) / 2, y + layout.coverH + layout.labelGap,
                      text.c_str());

    // Selection highlight: a thicker rounded border around the current tile.
    if (i == selectedIndex) {
      renderer.drawRoundedRect(x - kSelectPad, y - kSelectPad, layout.coverW + 2 * kSelectPad,
                               layout.coverH + 2 * kSelectPad, 3, kCornerRadius + kSelectPad, true);
    }
  }
}

// TEMPORARY (BUG-006 measurement): the crash was a failed allocation, so what
// matters is how much headroom is actually left at the worst moment — biggest
// group, covers not yet generated. Drawn on screen rather than logged so it can
// be read without a serial cable. Remove once BUG-006 is settled.
namespace {
// Lowest value the cover-generation gate has been shown this boot, and how many
// times it has run — the gate's own view, which is smaller than the at-rest
// figure because the OPF parse has already taken its share by then.
size_t lastCoverCheck = 0;
size_t lowestCoverCheck = SIZE_MAX;
}  // namespace

void noteCoverCheck(const size_t largestFreeBlock) {
  lastCoverCheck = largestFreeBlock;
  if (largestFreeBlock < lowestCoverCheck) lowestCoverCheck = largestFreeBlock;
}

namespace {
int outcomeCounts[5] = {0, 0, 0, 0, 0};
size_t generationLowWater = SIZE_MAX;
}  // namespace

void noteIndexOutcome(const IndexOutcome outcome) { outcomeCounts[static_cast<int>(outcome)]++; }

void noteGenerationLowWater(const size_t minimumFreeSize) {
  if (minimumFreeSize < generationLowWater) generationLowWater = minimumFreeSize;
}

void drawHeapDebugLine(GfxRenderer& renderer, int) {
  // Drawn at the bottom of the page, above the button hints, on a cleared strip:
  // over the grid the tiles rendered straight through the numbers. Wrapped to
  // the screen width so nothing runs off the right edge (the y parameter is
  // ignored — kept only so the call sites don't churn while this is temporary).
  //
  // On the numbers themselves: free/largest are sampled here during render, but
  // a cover is decompressed in loop() and freed before the next redraw, so the
  // peak never appears in them. min/genmin come from the allocator's own
  // boot-long low-water mark, which catches it regardless.
  const std::string stats =
      "free=" + std::to_string(ESP.getFreeHeap()) +
      " largest=" + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)) +
      " min=" + std::to_string(heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT)) +
      " | chk=" + (lastCoverCheck == 0 ? std::string("-") : std::to_string(lastCoverCheck)) +
      " chkmin=" + (lowestCoverCheck == SIZE_MAX ? std::string("-") : std::to_string(lowestCoverCheck)) +
      " genmin=" + (generationLowWater == SIZE_MAX ? std::string("-") : std::to_string(generationLowWater)) +
      " | gen=" + std::to_string(outcomeCounts[3]) + " lowmem=" + std::to_string(outcomeCounts[4]) +
      " hasthumb=" + std::to_string(outcomeCounts[2]) + " metafail=" + std::to_string(outcomeCounts[1]) +
      " notepub=" + std::to_string(outcomeCounts[0]);

  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int sidePad = 4;
  const int lineH = renderer.getTextHeight(SMALL_FONT_ID) + 2;

  const auto lines = renderer.wrappedText(SMALL_FONT_ID, stats.c_str(), pageWidth - sidePad * 2, 4);
  const int blockH = static_cast<int>(lines.size()) * lineH + 4;
  const int top = pageHeight - m.buttonHintsHeight - blockH;

  renderer.fillRect(0, top, pageWidth, blockH, false);  // clear the strip so tiles don't show through
  int lineY = top + 2;
  for (const auto& line : lines) {
    renderer.drawText(SMALL_FONT_ID, sidePad, lineY, line.c_str());
    lineY += lineH;
  }
}

void drawIndexingOverlay(GfxRenderer& renderer) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int pad = 14;
  const int boxW = std::min(pageWidth - 40, 340);
  const int boxH = lineH * 2 + pad * 2 + 4;
  const int boxX = (pageWidth - boxW) / 2;
  const int boxY = (pageHeight - boxH) / 2;
  renderer.fillRoundedRect(boxX, boxY, boxW, boxH, 8, Color::White);
  renderer.drawRoundedRect(boxX, boxY, boxW, boxH, 2, 8, true);
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + pad, tr(STR_COVER_GRID_INDEXING));
  renderer.drawCenteredText(UI_10_FONT_ID, boxY + pad + lineH + 4, tr(STR_COVER_GRID_INDEXING_WAIT));
}

}  // namespace MosaicGrid
