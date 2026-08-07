#include "MosaicGridMetrics.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/UITheme.h"
#include "fontIds.h"

namespace MosaicGridMetrics {

CoverSize computeCoverSize(GfxRenderer& renderer) {
  const auto& m = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int side = m.contentSidePadding;
  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentBottom = pageHeight - m.buttonHintsHeight - m.verticalSpacing;
  const int contentW = pageWidth - 2 * side;
  const int contentH = contentBottom - contentTop;

  const int labelH = renderer.getLineHeight(SMALL_FONT_ID);

  const int maxCoverW = (contentW - (GRID_COLS - 1) * GAP_X) / GRID_COLS;
  const int maxCellH = (contentH - (GRID_ROWS - 1) * GAP_Y) / GRID_ROWS;
  const int coverBudgetH = std::max(16, maxCellH - labelH - LABEL_GAP);

  // Fit a 2:3 cover inside the per-cell budget (width- or height-limited).
  int coverW = std::min(maxCoverW, coverBudgetH * 2 / 3);
  if (coverW < 8) coverW = 8;
  int coverH = coverW * 3 / 2;
  if (coverH > coverBudgetH) {
    coverH = coverBudgetH;
    coverW = coverH * 2 / 3;
  }

  return {coverW, coverH};
}

}  // namespace MosaicGridMetrics
