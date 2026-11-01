#pragma once

#include "core/Search.h"

#include <glibmm/ustring.h>

#include <set>
#include <string_view>

namespace kevc {

// Pango markup shared by the search results (Project tab, quick search, Search tab).

// `text` with the characters at `positions` (byte offsets, counted from `offset`) in bold yellow:
// the letters a file-name search matched.
Glib::ustring highlightLetters(std::string_view text, size_t offset, const std::set<size_t>& positions);

// A Find in Files match: its line number dimmed, then the line with the match marked the way the
// editor marks search matches.
Glib::ustring matchMarkup(const search::LineMatch& match);

} // namespace kevc
