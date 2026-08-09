#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * Deletes the generated cover thumbnails for the Cover Grid.
 *
 * Thumbnails live beside each book's metadata cache in /.crosspoint/epub_<hash>/
 * as thumb_<W>x<H>.bmp. Only those are removed: the metadata cache next to them
 * is what makes a book open quickly and what the library index is built from, so
 * wiping it would undo CGV-010's whole point.
 *
 * Useful when cover sizing changes and stale thumbs linger, to reclaim SD space,
 * and while testing cover generation — the cache directory is hidden, so WebDAV
 * deliberately refuses to serve it and the card would otherwise have to come out.
 *
 * Launched from Settings > Apps > Cover Grid > "Delete generated covers".
 */
class MosaicCoverCacheClearActivity final : public Activity {
 public:
  enum class State { DELETING, DONE };

  explicit MosaicCoverCacheClearActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("MosaicCoverCacheClear", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::DELETING; }

 private:
  State state = State::DONE;
  int deletedCount = 0;

  void deleteThumbnails();
};
