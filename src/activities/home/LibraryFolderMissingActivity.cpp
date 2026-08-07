#include "LibraryFolderMissingActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void LibraryFolderMissingActivity::onEnter() {
  Activity::onEnter();

  const int maxWidth = renderer.getScreenWidth() - 40;
  const std::string body = libraryPath + " " + tr(STR_LIBRARY_FOLDER_MISSING_BODY);
  safeBody = renderer.truncatedText(UI_10_FONT_ID, body.c_str(), maxWidth, EpdFontFamily::REGULAR);

  requestUpdate();
}

void LibraryFolderMissingActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_LIBRARY_FOLDER_MISSING));
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, safeBody.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_CREATE_IT), tr(STR_CHOOSE_ANOTHER));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void LibraryFolderMissingActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    setResult(ActivityResult{MenuResult{0}});
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    setResult(ActivityResult{MenuResult{1}});
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
}
