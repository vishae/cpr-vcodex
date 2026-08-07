#include "MosaicMetadataGenerateActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/home/MosaicGridMetrics.h"
#include "activities/home/MosaicLibraryScan.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kCacheDir[] = "/.crosspoint";
}  // namespace

void MosaicMetadataGenerateActivity::onEnter() {
  Activity::onEnter();

  const auto coverSize = MosaicGridMetrics::computeCoverSize(renderer);
  coverW = coverSize.width;
  coverH = coverSize.height;

  bookPaths = MosaicLibraryScan::scanBookPaths(SETTINGS.libraryFolder);
  currentIndex = 0;
  generatedCount = 0;
  skippedCount = 0;
  state = bookPaths.empty() ? State::DONE : State::GENERATING;

  requestUpdate();
}

void MosaicMetadataGenerateActivity::generateNext() {
  if (currentIndex >= bookPaths.size()) {
    state = State::DONE;
    requestUpdate();
    return;
  }

  const std::string& path = bookPaths[currentIndex++];
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, kCacheDir);
    if (epub.loadMetadataOnly()) {
      const std::string coverBmpPath = epub.getThumbBmpPath();
      const std::string thumb = UITheme::getCoverThumbPath(coverBmpPath, coverW, coverH);
      if (!Storage.exists(thumb.c_str())) {
        epub.generateThumbBmp(coverW, coverH);
        generatedCount++;
      } else {
        skippedCount++;
      }
    }
  }

  if (currentIndex >= bookPaths.size()) {
    state = State::DONE;
  }
  requestUpdate();
}

void MosaicMetadataGenerateActivity::loop() {
  if (state == State::GENERATING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      state = State::DONE;
      requestUpdate();
      return;
    }
    generateNext();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void MosaicMetadataGenerateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GENERATE_METADATA));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::GENERATING) {
    const int total = static_cast<int>(bookPaths.size());
    const int pct = total > 0 ? static_cast<int>((currentIndex * 100) / total) : 100;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_GENERATING_METADATA), true, EpdFontFamily::BOLD);

    int y = top + lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        pct, 100);
    y += metrics.progressBarHeight + metrics.verticalSpacing;

    const std::string countText = std::to_string(currentIndex) + " / " + std::to_string(total);
    renderer.drawCenteredText(UI_10_FONT_ID, y, countText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_METADATA_GENERATION_COMPLETE), true, EpdFontFamily::BOLD);

    std::string resultText = std::to_string(generatedCount) + " " + std::string(tr(STR_BOOKS_GENERATED));
    if (skippedCount > 0) {
      resultText += ", " + std::to_string(skippedCount) + " " + std::string(tr(STR_BOOKS_ALREADY_CACHED));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, resultText.c_str());

    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
