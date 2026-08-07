#pragma once

#include <string>

#include "../Activity.h"

// Shown by MosaicBrowserActivity (CGV-005) when the configured library folder
// doesn't exist on the SD card. Left = create it at the configured path.
// Right = pick a different folder (FileBrowserActivity::Mode::PickFolder).
// Back = cancel, leaving the mosaic view unopened.
class LibraryFolderMissingActivity final : public Activity {
 public:
  LibraryFolderMissingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string libraryPath)
      : Activity("LibraryFolderMissing", renderer, mappedInput), libraryPath(std::move(libraryPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string libraryPath;
  std::string safeBody;
};
