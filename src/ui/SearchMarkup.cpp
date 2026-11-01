#include "ui/SearchMarkup.h"

#include <glibmm/markup.h>

#include <algorithm>
#include <string>

namespace kevc {

namespace {

Glib::ustring esc(std::string_view s) {
    return Glib::Markup::escape_text(std::string(s));
}

} // namespace

Glib::ustring highlightLetters(std::string_view text, size_t offset, const std::set<size_t>& positions) {
    Glib::ustring out;
    bool open = false;
    for (size_t i = 0; i < text.size();) {
        size_t n = 1; // whole UTF-8 characters
        while (i + n < text.size() && (static_cast<unsigned char>(text[i + n]) & 0xC0) == 0x80) ++n;
        bool hit = false;
        for (size_t k = 0; k < n; ++k) hit = hit || positions.count(offset + i + k);
        if (hit != open) {
            out += hit ? "<span weight=\"bold\" foreground=\"#f0c050\">" : "</span>";
            open = hit;
        }
        out += esc(text.substr(i, n));
        i += n;
    }
    if (open) out += "</span>";
    return out;
}

Glib::ustring matchMarkup(const search::LineMatch& m) {
    const std::string_view p = m.preview;
    const size_t start = std::min(p.size(), m.previewColumn), end = std::min(p.size(), m.previewColumn + m.previewLength);
    return "<span alpha=\"55%\">" + std::to_string(m.line) + "</span>  " + esc(p.substr(0, start)) +
           "<span background=\"#6b5a1f\" foreground=\"#ffffff\">" + esc(p.substr(start, end - start)) + "</span>" +
           esc(p.substr(end));
}

} // namespace kevc
