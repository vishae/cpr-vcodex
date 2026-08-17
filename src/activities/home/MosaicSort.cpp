#include "MosaicSort.h"

#include <cctype>

namespace MosaicSort {

namespace {

const std::string kEmpty;

const std::string& deref(const std::string* s) { return s ? *s : kEmpty; }

// A field is "missing" when there is nothing to order by. Missing values always
// sort AFTER present ones, whichever key is active — CGV-003's rule that a book
// lacking the sort field goes to a defined trailing bucket and never vanishes.
// Ordinary comparison would do the opposite, since "" is less than everything.
bool isMissing(const std::string* s) { return s == nullptr || s->empty(); }

// Resolve a "missing beats present" question. Returns true if the two differ in
// presence, writing the answer to `out`.
bool missingDiffers(bool aMissing, bool bMissing, bool& out) {
  if (aMissing == bMissing) return false;
  out = !aMissing;  // the present one comes first
  return true;
}

// Unknown seriesIndex (< 0) trails within its series, matching the behaviour
// CGV-002 v1 already shipped for By-Series grouping.
bool seriesIndexLess(float a, float b, bool& out) {
  if (a == b) return false;
  const bool aMissing = a < 0.0f;
  const bool bMissing = b < 0.0f;
  if (missingDiffers(aMissing, bMissing, out)) return true;
  out = a < b;
  return true;
}

bool textLess(const std::string* a, const std::string* b, bool& out) {
  if (missingDiffers(isMissing(a), isMissing(b), out)) return true;
  const int cmp = compareText(deref(a), deref(b));
  if (cmp == 0) return false;
  out = cmp < 0;
  return true;
}

// Descending, with 0 meaning unknown and therefore trailing rather than oldest.
bool timeNewerFirst(uint32_t a, uint32_t b, bool& out) {
  if (a == b) return false;
  if (missingDiffers(a == 0, b == 0, out)) return true;
  out = a > b;
  return true;
}

// The tail every chain ends in: series, seriesIndex, title, path. Path is last
// and is unique, so two distinct books never compare equal — which is what
// keeps paging stable across renders.
bool tailLess(const Fields& a, const Fields& b, bool& out) {
  if (textLess(a.series, b.series, out)) return true;
  if (seriesIndexLess(a.seriesIndex, b.seriesIndex, out)) return true;
  if (textLess(a.title, b.title, out)) return true;
  if (textLess(a.path, b.path, out)) return true;
  return false;
}

}  // namespace

int compareText(const std::string& a, const std::string& b) {
  // Case-insensitive so an author list reads as a person would write it —
  // raw byte order puts every lowercase name after every uppercase one, which
  // looks broken on a shelf. ASCII-only folding: the device's fonts cover
  // Latin-1 and beyond, but a full Unicode collation is not worth the flash
  // here, and accented names still sort adjacently to their base letter.
  const size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; i++) {
    const unsigned char ca = static_cast<unsigned char>(a[i]);
    const unsigned char cb = static_cast<unsigned char>(b[i]);
    const int la = std::tolower(ca);
    const int lb = std::tolower(cb);
    if (la != lb) return la < lb ? -1 : 1;
  }
  if (a.size() == b.size()) {
    return 0;
  }
  return a.size() < b.size() ? -1 : 1;
}

bool less(const Fields& a, const Fields& b, Key key) {
  bool out = false;

  switch (key) {
    case Key::Title:
      // title -> path
      if (textLess(a.title, b.title, out)) return out;
      if (textLess(a.path, b.path, out)) return out;
      return false;

    case Key::Author:
      // author -> series -> seriesIndex -> title -> path.
      // Degenerate inside an author group (one author, so this never decides)
      // and falls straight through to the tail — deliberate, per CGV-DOC-002:
      // an option that quietly does the sensible thing beats a menu that
      // changes shape depending on the grouping.
      if (textLess(a.author, b.author, out)) return out;
      if (tailLess(a, b, out)) return out;
      return false;

    case Key::Series:
      // series -> seriesIndex -> title -> path. Inside a series group this is
      // exactly the seriesIndex ordering CGV-002 v1 already shipped.
      if (tailLess(a, b, out)) return out;
      return false;

    case Key::DateAdded:
      // create-time desc -> series -> seriesIndex -> title -> path.
      // Falls through series before title so a same-timestamp batch copy keeps
      // each series together and in reading order rather than splitting it
      // alphabetically (Serena, 2026-08-02).
      if (timeNewerFirst(a.createdAt, b.createdAt, out)) return out;
      if (tailLess(a, b, out)) return out;
      return false;

    case Key::RecentlyRead:
      // read books first, most recent first; then an unread bucket ordered by
      // date added, then the shared tail. Books with no reading activity have
      // lastReadAt == 0, which timeNewerFirst already trails.
      if (timeNewerFirst(a.lastReadAt, b.lastReadAt, out)) return out;
      if (timeNewerFirst(a.createdAt, b.createdAt, out)) return out;
      if (tailLess(a, b, out)) return out;
      return false;
  }

  return false;
}

}  // namespace MosaicSort
