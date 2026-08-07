#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Group picker for the Cover Grid mosaic browser (CGV-002): shown before the
// grid when grouping is active, offering "All books" plus each distinct
// author/series found in the library. Selecting one returns its name via
// KeyboardResult; Back cancels.
class MosaicGroupPickerActivity final : public Activity {
 public:
  MosaicGroupPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<std::string> groups)
      : Activity("MosaicGroupPicker", renderer, mappedInput), groups(std::move(groups)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<std::string> groups;  // groups[0] is always "All books"
  size_t selectorIndex = 0;
  ButtonNavigator buttonNavigator;
};
