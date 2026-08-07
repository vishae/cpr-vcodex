#pragma once

class GfxRenderer;

// Shared cover-tile sizing math for the Cover Grid mosaic (CGV-001) and its
// bulk metadata/cover generator (CGV-008) — both must compute identical
// dimensions, since UITheme::getCoverThumbPath keys the cached thumb's path
// on width+height: a mismatch means generated thumbs are never found.
namespace MosaicGridMetrics {
constexpr int GRID_COLS = 3;
constexpr int GRID_ROWS = 3;
constexpr int GAP_X = 10;
constexpr int GAP_Y = 10;
constexpr int LABEL_GAP = 2;

struct CoverSize {
  int width;
  int height;
};

CoverSize computeCoverSize(GfxRenderer& renderer);
}  // namespace MosaicGridMetrics
