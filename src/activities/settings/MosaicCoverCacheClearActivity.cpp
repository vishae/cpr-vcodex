#include "MosaicCoverCacheClearActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>
#include <vector>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kCacheDir[] = "/.crosspoint";
// Per-book cache directories are named epub_<hash of the book's path>.
constexpr char kBookCacheDirPrefix[] = "epub_";
}  // namespace

void MosaicCoverCacheClearActivity::onEnter() {
  Activity::onEnter();
  deletedCount = 0;
  state = State::DELETING;
  requestUpdate(true);  // paint the "deleting" screen before the blocking walk starts
}

// Removes each per-book cache directory under /.crosspoint/ entirely — contents
// first, then the directory — so a re-test exercises the full path including
// creating the folder, not just re-writing a thumbnail into an existing one
// (Serena's call, 2026-08-09).
//
// The library index (/.crosspoint/mosaic_index.bin) is a file, not an epub_
// directory, so it survives: grouping stays fast, and only per-book metadata and
// covers are rebuilt.
//
// Paths are collected before deleting rather than removed mid-iteration, since
// deleting from an open directory handle is not reliable on FAT.
void MosaicCoverCacheClearActivity::deleteThumbnails() {
  auto cacheDir = Storage.open(kCacheDir);
  if (!cacheDir || !cacheDir.isDirectory()) {
    if (cacheDir) cacheDir.close();
    state = State::DONE;
    return;
  }

  char nameBuf[512];
  std::vector<std::string> bookDirs;
  cacheDir.rewindDirectory();
  for (auto entry = cacheDir.openNextFile(); entry; entry = cacheDir.openNextFile()) {
    if (entry.isDirectory()) {
      entry.getName(nameBuf, sizeof(nameBuf));
      if (strncmp(nameBuf, kBookCacheDirPrefix, strlen(kBookCacheDirPrefix)) == 0) {
        bookDirs.push_back(std::string(kCacheDir) + "/" + nameBuf);
      }
    }
    entry.close();
  }
  cacheDir.close();

  for (const auto& bookDir : bookDirs) {
    std::vector<std::string> contents;
    auto dir = Storage.open(bookDir.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      continue;
    }
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      const bool isDir = entry.isDirectory();
      entry.getName(nameBuf, sizeof(nameBuf));
      entry.close();
      if (!isDir) contents.push_back(bookDir + "/" + nameBuf);
    }
    dir.close();

    for (const auto& path : contents) Storage.remove(path.c_str());
    if (Storage.removeDir(bookDir.c_str())) deletedCount++;
  }

  LOG_INF("MOSAIC", "Deleted %d per-book cache directories", deletedCount);
  state = State::DONE;
}

void MosaicCoverCacheClearActivity::loop() {
  if (state == State::DELETING) {
    deleteThumbnails();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void MosaicCoverCacheClearActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_DELETE_MOSAIC_COVERS));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::DELETING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_DELETING_MOSAIC_COVERS), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels("", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const std::string result = std::to_string(deletedCount) + " " + tr(STR_MOSAIC_COVERS_DELETED);
    renderer.drawCenteredText(UI_10_FONT_ID, top, result.c_str(), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing,
                              tr(STR_MOSAIC_COVERS_DELETED_HINT));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
