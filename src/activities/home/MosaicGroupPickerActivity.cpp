#include "MosaicGroupPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

namespace {
// Matches the grid's own Options hold, so the gesture is one thing to learn.
constexpr unsigned long kOptionsHoldMs = 700;
}  // namespace

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MosaicGroupPickerActivity::onEnter() {
  Activity::onEnter();
  // Keep the restored selection (BUG-007), but never trust it blindly — the
  // group list is rebuilt on each open and may have shrunk since.
  if (selectorIndex >= groups.size()) selectorIndex = 0;
  if (useGrid) layout = MosaicGrid::computeLayout(renderer);
  requestUpdate();
}

void MosaicGroupPickerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  renderer.clearScreen();

  // Grid mode gets the same page indicator the book browser's header carries.
  std::string title = tr(STR_MOSAIC_GROUP_PICKER_TITLE);
  const int total = static_cast<int>(groups.size());
  if (useGrid && total > MosaicGrid::PER_PAGE) {
    const int totalPages = (total + MosaicGrid::PER_PAGE - 1) / MosaicGrid::PER_PAGE;
    const int currentPage = MosaicGrid::pageStartFor(static_cast<int>(selectorIndex)) / MosaicGrid::PER_PAGE + 1;
    title += "  " + std::to_string(currentPage) + "/" + std::to_string(totalPages);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  if (useGrid) {
    renderGrid();
  } else {
    renderList();
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN_OPTIONS), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void MosaicGroupPickerActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(groups.size()), selectorIndex,
               [this](const int index) { return groups[index].name; });
}

void MosaicGroupPickerActivity::renderGrid() {
  MosaicGrid::drawPage(
      renderer, layout, MosaicGrid::pageStartFor(static_cast<int>(selectorIndex)), static_cast<int>(groups.size()),
      static_cast<int>(selectorIndex), [this](const int index) { return groups[index].name; },
      [this](const int index) { return groups[index].coverBmpPath; });
}

void MosaicGroupPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= kOptionsHoldMs) {
      setResult(ActivityResult{MenuResult{OPTIONS_REQUESTED, 0, 0}});
      finish();
      return;
    }
    if (!groups.empty()) {
      setResult(ActivityResult{KeyboardResult{groups[selectorIndex].name}});
    }
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  const int listSize = static_cast<int>(groups.size());
  if (listSize == 0) return;

  auto moveNext = [this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  };
  auto movePrevious = [this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  };

  if (!useGrid) {
    buttonNavigator.onNextRelease(moveNext);
    buttonNavigator.onPreviousRelease(movePrevious);
    return;
  }

  // Grid mode navigates like the book browser: Left/Right step one tile in
  // reading order (wrapping), Up/Down move a whole row within the same column.
  using Btn = MappedInputManager::Button;
  auto moveDown = [this, listSize] {
    if (static_cast<int>(selectorIndex) + MosaicGrid::COLS < listSize) {
      selectorIndex += MosaicGrid::COLS;
      requestUpdate();
    }
  };
  auto moveUp = [this] {
    if (selectorIndex >= static_cast<size_t>(MosaicGrid::COLS)) {
      selectorIndex -= MosaicGrid::COLS;
      requestUpdate();
    }
  };
  buttonNavigator.onRelease({Btn::Right}, moveNext);
  buttonNavigator.onRelease({Btn::Left}, movePrevious);
  buttonNavigator.onRelease({Btn::Down}, moveDown);
  buttonNavigator.onRelease({Btn::Up}, moveUp);
  // Hold-to-repeat for fast traversal.
  buttonNavigator.onContinuous({Btn::Right}, moveNext);
  buttonNavigator.onContinuous({Btn::Left}, movePrevious);
  buttonNavigator.onContinuous({Btn::Down}, moveDown);
  buttonNavigator.onContinuous({Btn::Up}, moveUp);
}
