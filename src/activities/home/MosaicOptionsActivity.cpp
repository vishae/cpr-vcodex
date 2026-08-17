#include "MosaicOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MosaicOptionsActivity::onEnter() {
  Activity::onEnter();
  if (selectorIndex >= items.size()) selectorIndex = 0;
  requestUpdate();
}

void MosaicOptionsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (items.empty()) {
      finish();
      return;
    }
    if (onSelect) {
      // Live mode: apply and stay, so the toggle is seen changing rather than
      // inferred from having to reopen the menu.
      items = onSelect(selectorIndex);
      if (selectorIndex >= items.size()) selectorIndex = 0;
      requestUpdate();
      return;
    }
    // The row index is the whole answer; the caller knows what each row means.
    setResult(ActivityResult{MenuResult{static_cast<int>(selectorIndex), 0, 0}});
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

  const int listSize = static_cast<int>(items.size());
  if (listSize == 0) return;

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
}

void MosaicOptionsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(items.size()), selectorIndex,
               [this](const int index) { return items[index]; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
