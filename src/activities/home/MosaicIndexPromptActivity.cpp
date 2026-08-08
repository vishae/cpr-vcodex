#include "MosaicIndexPromptActivity.h"

#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "../../components/UITheme.h"
#include "../../fontIds.h"
#include "HalDisplay.h"

MosaicIndexPromptActivity::MosaicIndexPromptActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     Mode mode, std::string libraryPath)
    : Activity("MosaicIndexPrompt", renderer, mappedInput), mode(mode), libraryPath(std::move(libraryPath)) {}

void MosaicIndexPromptActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxWidth = renderer.getScreenWidth() - (kMargin * 2);

  const bool stale = mode == Mode::Stale;
  heading = renderer.truncatedText(
      UI_10_FONT_ID, I18N.get(stale ? StrId::STR_LIBRARY_INDEX_STALE : StrId::STR_LIBRARY_INDEX_NEW), maxWidth,
      EpdFontFamily::BOLD);

  // Body: what happened (with the folder read into that sentence rather than
  // dropped between two), then a line per button saying what it does — the
  // choice isn't obvious from two-word labels alone.
  const char* bodyFormat =
      I18N.get(stale ? StrId::STR_LIBRARY_INDEX_STALE_BODY_FMT : StrId::STR_LIBRARY_INDEX_NEW_BODY_FMT);
  std::vector<char> formatted(strlen(bodyFormat) + libraryPath.size() + 1);
  snprintf(formatted.data(), formatted.size(), bodyFormat, libraryPath.c_str());

  // Each block wraps separately so an option's description always starts on its
  // own line instead of running on from the sentence before it.
  const char* blocks[] = {
      formatted.data(),
      tr(STR_LIBRARY_INDEX_UPDATE_HINT),
      I18N.get(stale ? StrId::STR_LIBRARY_INDEX_USE_CACHED_HINT : StrId::STR_LIBRARY_INDEX_NOT_NOW_HINT),
  };
  bodyLines.clear();
  for (const char* block : blocks) {
    if (!bodyLines.empty()) bodyLines.emplace_back();  // blank line between blocks
    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, block, maxWidth, kBodyMaxLines);
    bodyLines.insert(bodyLines.end(), wrapped.begin(), wrapped.end());
  }

  int totalHeight = lineHeight + kSpacing + static_cast<int>(bodyLines.size()) * lineHeight;
  startY = (renderer.getScreenHeight() - totalHeight) / 2;

  requestUpdate(true);
}

void MosaicIndexPromptActivity::render(RenderLock&&) {
  renderer.clearScreen();

  int currentY = startY;
  renderer.drawCenteredText(UI_10_FONT_ID, currentY, heading.c_str(), true, EpdFontFamily::BOLD);
  currentY += lineHeight + kSpacing;

  for (const auto& line : bodyLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, currentY, line.c_str());
    currentY += lineHeight;
  }

  // Cancel wording differs per mode: a stale index can still be read from
  // ("Use cached"), an absent one leaves nothing to fall back to ("Not now").
  const char* cancelLabel =
      I18N.get(mode == Mode::Stale ? StrId::STR_LIBRARY_INDEX_USE_CACHED : StrId::STR_LIBRARY_INDEX_NOT_NOW);
  // Back is left blank and inert, matching every other confirmation screen —
  // declining has exactly one button.
  const auto labels = mappedInput.mapLabels("", "", cancelLabel, tr(STR_LIBRARY_INDEX_UPDATE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void MosaicIndexPromptActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
}
