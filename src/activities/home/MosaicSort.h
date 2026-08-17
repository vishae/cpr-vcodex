#pragma once

#include <cstdint>
#include <string>

#include "MosaicLibraryScan.h"

// Ordering for the Cover Grid mosaic browser (CGV-003).
//
// Kept apart from MosaicBrowserActivity so the comparison rules live in one
// place: the same key has to order the flat grid, the books inside a chosen
// group, and the group picker's own tiles. Three call sites deriving "what does
// sort-by-series mean" separately is how the picker and the filter came to
// disagree about grouping buckets once already (CGV-002, groupKeyFor).
//
// Every key resolves ties through a deterministic chain ending in path, so the
// order never flickers between renders on a library with duplicate titles.
// Chains are specified in CGV-DOC-002; the comment on each branch of less()
// below names the one it implements.
namespace MosaicSort {

enum class Key : uint8_t {
  DateAdded = 0,
  Author = 1,
  Series = 2,
  RecentlyRead = 3,
  Title = 4,
};

// The fields an ordering decision needs, gathered from wherever the caller
// holds them. Pointers rather than copies: sorting a library of several hundred
// books shouldn't allocate, and the strings always outlive the sort.
struct Fields {
  const std::string* title = nullptr;
  const std::string* author = nullptr;
  const std::string* series = nullptr;
  const std::string* path = nullptr;
  float seriesIndex = -1.0f;             // < 0 = unknown
  MosaicLibraryScan::CreatedAt createdAt = 0;  // 0 = unknown
  uint32_t lastReadAt = 0;               // 0 = never read
};

// Strict weak ordering for `key`. True when a sorts before b.
bool less(const Fields& a, const Fields& b, Key key);

// Case-insensitive ASCII compare used by every text key, exposed for tests.
// Returns <0, 0, >0 like strcmp. An EMPTY string is not "less than everything"
// here — callers use isMissing() to push blanks into the trailing bucket first,
// so this only ever compares two present values.
int compareText(const std::string& a, const std::string& b);

}  // namespace MosaicSort
