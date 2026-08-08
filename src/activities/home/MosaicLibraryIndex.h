#pragma once

#include <string>
#include <vector>

#include "MosaicLibraryScan.h"

// Persisted library-wide index for the Cover Grid mosaic browser (CGV-010).
//
// Grouping (CGV-002) and author/series sorting (CGV-003) need title/author/series
// for every book. Without this index that means an Epub metadata load per book on
// every open — and because cover generation (CGV-008) persists no metadata, any
// book never opened in the reader gets its content.opf re-parsed out of the zip
// each time (measured 2026-08-08: 13.9 s of a 14.2 s grouping open, at 40 books).
//
// This index stores that computed metadata once, alongside the fingerprint of the
// library folder it was built from. On a fingerprint match the whole per-book
// metadata pass is skipped. The fingerprint itself is accumulated during the
// name-only directory walk that every open already performs (2.4% of the cost),
// so checking freshness is effectively free.
namespace MosaicLibraryIndex {

struct Entry {
  std::string path;
  std::string title;
  std::string author;
  std::string series;
  float seriesIndex = -1.0f;
};

struct Index {
  std::string libraryPath;  // the library root this index was built for
  MosaicLibraryScan::Fingerprint fingerprint;
  std::vector<Entry> entries;
};

// Read the persisted index. False if absent, unreadable, or written by a
// different format version — all of which the caller treats as "no index yet".
bool load(Index& out);

// Persist the index (written to a temp file then renamed, so an interrupted
// write can't leave a truncated index behind).
bool save(const Index& index);

}  // namespace MosaicLibraryIndex
