#include "MosaicGroupPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void MosaicGroupPickerActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  requestUpdate();
}

void MosaicGroupPickerActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_MOSAIC_GROUP_PICKER_TITLE));

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  // TEMPORARY (CGV-010 measurement): folder-walk vs per-book-metadata timings,
  // drawn above the list so they're readable without a serial cable.
  if (!debugLine.empty()) {
    const int lineHeight = renderer.getTextHeight(SMALL_FONT_ID) + 4;
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, contentTop, debugLine.c_str());
    contentTop += lineHeight;
  }

  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(groups.size()),
              selectorIndex, [this](const int index) { return groups[index]; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void MosaicGroupPickerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!groups.empty()) {
      setResult(ActivityResult{KeyboardResult{groups[selectorIndex]}});
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
  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
}
