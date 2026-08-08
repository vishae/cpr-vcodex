#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "MosaicGrid.h"
#include "util/ButtonNavigator.h"

// Group picker for the Cover Grid mosaic browser (CGV-002): shown before the
// grid when grouping is active, offering "All books" plus each distinct
// author/series found in the library. Selecting one returns its name via
// KeyboardResult; Back cancels.
//
// Two presentations, chosen by the caller from the per-type display settings
// (CGV-002 v2): a text list, or the same tile grid the book browser uses
// (DEC-011's shared painter). Series tiles carry the cover of their
// lowest-seriesIndex book; author tiles never carry a cover — an author isn't
// one visual identity — so they pass an empty cover path and draw the
// placeholder.
class MosaicGroupPickerActivity final : public Activity {
 public:
  struct Group {
    std::string name;
    std::string coverBmpPath;  // empty = placeholder tile (always, for authors)
  };

  MosaicGroupPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Group> groups,
                            bool useGrid)
      : Activity("MosaicGroupPicker", renderer, mappedInput), groups(std::move(groups)), useGrid(useGrid) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<Group> groups;  // groups[0] is always "All books"
  bool useGrid = false;
  MosaicGrid::Layout layout;  // only computed/used in grid mode
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  void renderList();
  void renderGrid();
};
