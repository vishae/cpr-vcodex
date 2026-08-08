#pragma once

#include <functional>
#include <string>

class GfxRenderer;

// Shared paged 3x3 tile painter for the Cover Grid (DEC-011).
//
// Both the book browser (CGV-001) and the group picker's grid mode (CGV-002 v2)
// paint the same thing — cover-or-placeholder tile, label underneath, selection
// highlight — over different item lists. Painting lives here so a display change
// (CGV-006's bigger covers, for one) is made once rather than kept in sync by
// hand across two activities.
//
// The caller supplies per-index accessors, the same way BaseTheme::drawList
// takes row callbacks: the painter never knows whether it's drawing books or
// groups. An empty coverBmpPath draws the placeholder icon — which is how an
// author tile stays a placeholder even when the author's books have covers.
namespace MosaicGrid {

constexpr int COLS = 3;
constexpr int ROWS = 3;
constexpr int PER_PAGE = COLS * ROWS;

// Tile geometry for the current screen and theme. Computed once per activity
// entry (computeLayout) and passed back in on each paint.
struct Layout {
  int coverW = 0;
  int coverH = 0;
  int labelH = 0;
  int gapX = 10;
  int gapY = 10;
  int labelGap = 2;
  int gridX0 = 0;
  int gridY0 = 0;
};

Layout computeLayout(GfxRenderer& renderer);

// Paint one page of tiles. `pageStart` is the first item index on the visible
// page; `total` is the whole list's size. `coverBmpPath` returns the base thumb
// path for an item, or "" for a placeholder tile.
void drawPage(GfxRenderer& renderer, const Layout& layout, int pageStart, int total, int selectedIndex,
              const std::function<std::string(int index)>& label,
              const std::function<std::string(int index)>& coverBmpPath);

// Centred "still indexing" popup, drawn over the grid while covers are being
// generated one per idle tick — the metadata-cache build blocks the loop on a
// single core, so without this the grid just looks frozen.
void drawIndexingOverlay(GfxRenderer& renderer);

// First item index of the page containing `index`.
int pageStartFor(int index);

// TEMPORARY (BUG-006 measurement): draws "free=… largest=… min=…" so the heap
// headroom can be read off the device without a serial cable — the same trick
// used for the CGV-010 timings. Remove once BUG-006 is settled.
void drawHeapDebugLine(GfxRenderer& renderer, int y);

}  // namespace MosaicGrid
