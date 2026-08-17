#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

// Generic list menu for the Cover Grid's in-view Options overlay (CGV-003,
// CGV-DEC-006). Long-press Confirm in the browser opens it; it is reused for
// every level of the overlay -- the top menu and each sub-menu -- rather than
// one activity per screen.
//
// Returns the chosen row's index via MenuResult::action; Back cancels. It
// reuses MenuResult rather than adding a variant to ActivityResult, which is an
// upstream-tracked file (same add-alongside reasoning as CGV-DEC-002).
//
// Deliberately a text list, not the tile grid: these are settings, not books,
// and a cover has nothing to show for "Sort by author".
//
// Two modes:
//
//   * **Navigating** (no onSelect) — Confirm returns the chosen row index via
//     MenuResult and closes. Used by the top menu, whose rows lead somewhere.
//   * **Live** (onSelect supplied) — Confirm applies the choice and the screen
//     *stays open*, redrawn from the labels the handler returns. Used by every
//     settings sub-menu: selecting the active sort key toggles its direction,
//     so closing on each press would mean reopening the menu to see what the
//     press did (Serena, 2026-08-18). Back is what navigates up.
class MosaicOptionsActivity final : public Activity {
 public:
  // Applies the selection and returns the redrawn row labels.
  using SelectHandler = std::function<std::vector<std::string>(size_t)>;

  MosaicOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                        std::vector<std::string> items, size_t initialIndex = 0, SelectHandler onSelect = nullptr)
      : Activity("MosaicOptions", renderer, mappedInput),
        title(std::move(title)),
        items(std::move(items)),
        selectorIndex(initialIndex),
        onSelect(std::move(onSelect)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string title;
  std::vector<std::string> items;
  size_t selectorIndex = 0;
  SelectHandler onSelect;
  ButtonNavigator buttonNavigator;
};
