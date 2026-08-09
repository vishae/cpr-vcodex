#pragma once

#include <string>
#include <vector>

#include "../Activity.h"

// Shared "the library index needs rebuilding" prompt for the Cover Grid
// (CGV-010). Two situations, one component:
//
//   Stale   - an index exists for this folder but its fingerprint no longer
//             matches the folder (books added, removed, or resized).
//   NoCache - no index exists for this folder yet. Raised eagerly, right after
//             the library folder setting is saved, rather than waiting for the
//             next grouping open to discover it (folded-in CGV-011).
//
// Confirm runs the bulk cover + metadata generation (CGV-008), which also
// writes a fresh index; Cancel carries on without it. Modelled on
// ConfirmationActivity rather than extending it: that file is upstream-tracked
// with ten call sites, and this prompt needs its own button labels and a
// wrapped multi-line body (PID-26028 DEC-002, add alongside rather than modify).
class MosaicIndexPromptActivity final : public Activity {
 public:
  enum class Mode { Stale, NoCache };

  MosaicIndexPromptActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode, std::string libraryPath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int kMargin = 20;
  static constexpr int kSpacing = 12;
  static constexpr int kBodyMaxLines = 6;

  Mode mode;
  std::string libraryPath;

  std::string heading;
  std::vector<std::string> bodyLines;  // body + folder path + the "this takes a while" warning
  int startY = 0;
  int lineHeight = 0;
};
