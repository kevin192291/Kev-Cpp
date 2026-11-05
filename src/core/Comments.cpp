#include "core/Comments.h"

#include <algorithm>
#include <cctype>

namespace kevc::comments {

namespace {

constexpr auto npos = std::string_view::npos;

// A line's text: [start, end), without its "\n" or "\r\n". The next line starts after them.
struct Line {
    size_t start = 0, end = 0;
};

// As GtkTextBuffer counts lines: text ending with a newline has an empty last line after it.
std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> lines;
    size_t start = 0;
    for (;;) {
        const size_t nl = text.find('\n', start);
        if (nl == npos) {
            lines.push_back({start, text.size()});
            return lines;
        }
        const size_t end = nl > start && text[nl - 1] == '\r' ? nl - 1 : nl;
        lines.push_back({start, end});
        start = nl + 1;
    }
}

size_t indentOf(std::string_view s) {
    size_t n = 0;
    while (n < s.size() && (s[n] == ' ' || s[n] == '\t')) ++n;
    return n;
}

std::string_view trimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

int utf16Length(std::string_view s) {
    int n = 0;
    for (const unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n; // the first byte of a character
        if (c >= 0xF0) ++n;          // four bytes: two UTF-16 units
    }
    return n;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// `tag` ("<script", "</style") at `at`, not the start of a longer name ("<scripts").
bool tagAt(std::string_view text, size_t at, std::string_view tag) {
    if (at + tag.size() > text.size() || !equalsIgnoringCase(text.substr(at, tag.size()), tag)) return false;
    if (at + tag.size() == text.size()) return true;
    const unsigned char next = static_cast<unsigned char>(text[at + tag.size()]);
    return !std::isalnum(next) && next != '-' && next != '_' && next != ':';
}

size_t findTag(std::string_view text, std::string_view tag, size_t from) {
    for (size_t at = text.find('<', from); at != npos; at = text.find('<', at + 1))
        if (tagAt(text, at, tag)) return at;
    return npos;
}

// The lang="…" of an opening tag (`<style lang="scss">`), lower case; empty if it has none.
std::string langAttribute(std::string_view tag) {
    for (size_t at = 0; (at = tag.find("lang", at)) != npos; at += 4) {
        const unsigned char before = at > 0 ? static_cast<unsigned char>(tag[at - 1]) : ' ';
        if (!std::isspace(before)) continue;
        size_t i = at + 4;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        if (i >= tag.size() || tag[i] != '=') continue;
        ++i;
        while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
        const char quote = i < tag.size() && (tag[i] == '"' || tag[i] == '\'') ? tag[i] : '\0';
        if (quote) ++i;
        std::string value;
        while (i < tag.size() && (quote ? tag[i] != quote : !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '>'))
            value += static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i++])));
        return value;
    }
    return {};
}

struct Place {
    Section section = Section::Markup;
    std::string lang; // the section's lang="…"
};

Place placeAt(std::string_view text, size_t offset) {
    offset = std::min(offset, text.size());
    // The line closing a section is the markup's: "</script>" comments as HTML.
    if (tagAt(text, offset, "</script") || tagAt(text, offset, "</style")) return {};
    size_t i = 0;
    while (i < offset) {
        const size_t lt = text.find('<', i);
        if (lt == npos || lt >= offset) return {};
        if (text.compare(lt, 4, "<!--") == 0) { // a commented-out <script> isn't one
            const size_t end = text.find("-->", lt + 4);
            if (end == npos || end + 3 > offset) return {};
            i = end + 3;
            continue;
        }
        const bool script = tagAt(text, lt, "<script");
        if (!script && !tagAt(text, lt, "<style")) {
            i = lt + 1;
            continue;
        }
        const size_t gt = text.find('>', lt);
        if (gt == npos || gt >= offset) return {}; // inside the opening tag itself
        if (text[gt - 1] == '/') {                // <script src="…" />
            i = gt + 1;
            continue;
        }
        Place place{script ? Section::Script : Section::Style, langAttribute(text.substr(lt, gt - lt))};
        const size_t close = findTag(text, script ? "</script" : "</style", gt + 1);
        if (close == npos || close >= offset) return place;
        i = close + 1;
    }
    return {};
}

// A replacement of bytes [from, to) of the text, turned into a TextEdit at the end.
struct Change {
    size_t from = 0, to = 0;
    std::string text;
};

class Editor {
public:
    explicit Editor(std::string_view text) : text_(text), lines_(splitLines(text)) {}

    std::string_view text() const { return text_; }
    int count() const { return static_cast<int>(lines_.size()); }
    const Line& line(int i) const { return lines_[static_cast<size_t>(i)]; }
    std::string_view textOf(int i) const { return text_.substr(line(i).start, line(i).end - line(i).start); }
    size_t indent(int i) const { return indentOf(textOf(i)); }
    // The line after its indentation, without trailing blanks.
    std::string_view body(int i) const { return trimRight(textOf(i).substr(indent(i))); }
    bool blank(int i) const { return body(i).empty(); }
    int lineAt(size_t offset) const {
        auto it = std::upper_bound(lines_.begin(), lines_.end(), offset,
                                   [](size_t at, const Line& l) { return at < l.start; });
        return static_cast<int>(it - lines_.begin()) - 1;
    }
    std::string eol(int i) const {
        return line(i).end < text_.size() && text_[line(i).end] == '\r' ? "\r\n" : "\n";
    }

    void replace(size_t from, size_t to, std::string with) { changes_.push_back({from, to, std::move(with)}); }
    void insert(size_t at, std::string text) { replace(at, at, std::move(text)); }
    // Line i and its line break go; the break taken is the one before it, so the line below keeps its start.
    void removeLine(int i) {
        if (i > 0) replace(line(i - 1).end, line(i).end, "");
        else replace(line(0).start, count() > 1 ? line(1).start : line(0).end, "");
    }
    // Takes `token` off at `at`, with the space after it (as commenting out adds).
    void removeToken(size_t at, std::string_view token, size_t limit) {
        size_t to = at + token.size();
        if (to < limit && text_[to] == ' ') ++to;
        replace(at, to, "");
    }

    std::vector<format::TextEdit> edits() {
        std::stable_sort(changes_.begin(), changes_.end(), [](const Change& a, const Change& b) { return a.from < b.from; });
        std::vector<Change> merged;
        for (auto& c : changes_) {
            if (!merged.empty() && c.from < merged.back().to) { // only line removals ever touch
                merged.back().to = std::max(merged.back().to, c.to);
                merged.back().text += c.text;
            } else {
                merged.push_back(std::move(c));
            }
        }
        std::vector<format::TextEdit> out;
        for (const auto& c : merged) {
            const int a = lineAt(c.from), b = lineAt(c.to);
            out.push_back({a, utf16Length(text_.substr(line(a).start, c.from - line(a).start)), b,
                           utf16Length(text_.substr(line(b).start, c.to - line(b).start)), c.text});
        }
        return out;
    }

private:
    std::string_view text_;
    std::vector<Line> lines_;
    std::vector<Change> changes_;
};

// The block comment that a line holding only its start or only its end belongs to, if both of its
// markers are lines of their own (as commenting out several lines writes them): {opening, closing}.
std::optional<std::pair<int, int>> ownLineBlock(const Editor& ed, int line, const Style& style) {
    const std::string_view text = ed.text();
    const size_t at = ed.line(line).start + ed.indent(line);
    if (ed.body(line) == style.blockStart) {
        const size_t end = text.find(style.blockEnd, at + style.blockStart.size());
        if (end == npos) return std::nullopt;
        const int closing = ed.lineAt(end);
        if (closing == line || ed.body(closing) != style.blockEnd) return std::nullopt;
        return std::pair{line, closing};
    }
    if (ed.body(line) == style.blockEnd && at > 0) {
        const size_t start = text.rfind(style.blockStart, at - 1);
        if (start == npos) return std::nullopt;
        const int opening = ed.lineAt(start);
        if (opening == line || ed.body(opening) != style.blockStart) return std::nullopt;
        if (text.substr(start + style.blockStart.size(), at - start - style.blockStart.size()).find(style.blockEnd) != npos)
            return std::nullopt;
        return std::pair{opening, line};
    }
    return std::nullopt;
}

// A line that is one block comment, "/* … */", with no other comment's end inside.
bool wrappedLine(const Editor& ed, int i, const Style& style) {
    const std::string_view body = ed.body(i);
    const std::string& open = style.blockStart;
    const std::string& close = style.blockEnd;
    return style.hasBlock() && body.size() >= open.size() + close.size() && body.starts_with(open) &&
           body.ends_with(close) && body.substr(open.size(), body.size() - open.size() - close.size()).find(close) == npos;
}

// Takes a wrapped line's markers off, with the space inside each.
void unwrapLine(Editor& ed, int i, const Style& style) {
    const size_t at = ed.line(i).start + ed.indent(i);
    const size_t bodyEnd = at + ed.body(i).size();
    const size_t closing = bodyEnd - style.blockEnd.size();
    ed.removeToken(at, style.blockStart, closing);
    const bool spaceBefore = closing > at + style.blockStart.size() + 1 && ed.text()[closing - 1] == ' ';
    ed.replace(spaceBefore ? closing - 1 : closing, bodyEnd, "");
}

std::string nestingMessage(const Style& style) {
    return "Can't comment that out: it holds a " + style.blockEnd +
           ", which would end the comment early (comments don't nest)";
}

Toggle oneLine(Editor& ed, int i, const Style& style) {
    const Line& l = ed.line(i);
    const size_t at = l.start + ed.indent(i);
    const std::string_view body = ed.body(i);
    if (body.empty()) {
        if (!style.hasLine()) return {{}, {}, "There's nothing to comment out on this line"};
        ed.insert(l.end, style.line + " "); // a comment to write
        return {ed.edits(), {}, {}};
    }
    if (style.hasLine() && body.starts_with(style.line)) {
        ed.removeToken(at, style.line, l.end);
        return {ed.edits(), {}, {}};
    }
    if (style.hasBlock() && (body == style.blockStart || body == style.blockEnd))
        if (auto block = ownLineBlock(ed, i, style)) {
            ed.removeLine(block->first);
            ed.removeLine(block->second);
            return {ed.edits(), {}, {}};
        }
    if (style.hasLine()) {
        ed.insert(at, style.line + " ");
        return {ed.edits(), {}, {}};
    }
    // CSS, HTML: a block comment around the line, or off it.
    if (wrappedLine(ed, i, style)) {
        unwrapLine(ed, i, style);
        return {ed.edits(), {}, {}};
    }
    if (body.find(style.blockEnd) != npos) return {{}, {}, nestingMessage(style)};
    ed.insert(at, style.blockStart + " ");
    ed.insert(at + body.size(), " " + style.blockEnd);
    return {ed.edits(), {}, {}};
}

} // namespace

Style fileStyle(const std::filesystem::path& file, std::string_view languageId, Style declared) {
    const std::string name = file.filename().string();
    const std::string ext = file.extension().string();
    const bool jsonWithComments =
        ext == ".jsonc" ||
        (ext == ".json" && (name.starts_with("tsconfig") || name.starts_with("jsconfig") ||
                            name == "devcontainer.json" || file.parent_path().filename() == ".vscode"));
    if (jsonWithComments) return {"//", "/*", "*/"};
    if (languageId == "json") return {};
    return declared;
}

Section sectionAt(std::string_view text, size_t offset) {
    return placeAt(text, offset).section;
}

Style styleAt(std::string_view text, size_t offset, std::string_view languageId, const Style& fileStyle) {
    if (languageId != "html" && languageId != "svelte" && languageId != "vue") return fileStyle;
    const Place place = placeAt(text, offset);
    switch (place.section) {
    case Section::Script: return {"//", "/*", "*/"};
    case Section::Style:
        if (place.lang == "scss" || place.lang == "sass" || place.lang == "less" || place.lang == "stylus")
            return {"//", "/*", "*/"};
        return {"", "/*", "*/"};
    case Section::Markup: break;
    }
    return fileStyle.hasBlock() ? fileStyle : Style{"", "<!--", "-->"};
}

Toggle toggle(std::string_view text, int firstLine, int lastLine, std::string_view languageId, const Style& fileStyle) {
    Editor ed(text);
    if (firstLine > lastLine) std::swap(firstLine, lastLine);
    firstLine = std::clamp(firstLine, 0, ed.count() - 1);
    lastLine = std::clamp(lastLine, 0, ed.count() - 1);

    // The lines that hold something; blank ones at either end stay outside the comment.
    int first = firstLine, last = lastLine;
    while (first < lastLine && ed.blank(first)) ++first;
    while (last > first && ed.blank(last)) --last;
    const Style style = styleAt(text, ed.line(first).start + ed.indent(first), languageId, fileStyle);
    if (style.none()) return {{}, {}, "This kind of file has no comments"};
    if (first == last) {
        if (firstLine != lastLine && ed.blank(first)) return {{}, {}, "There's nothing to comment out: the lines are blank"};
        return oneLine(ed, first, style);
    }

    const std::pair<int, int> selected{firstLine, lastLine};
    const Line& top = ed.line(first);
    const Line& bottom = ed.line(last);
    const size_t start = top.start + ed.indent(first);   // where the first line's text starts
    const size_t end = bottom.start + ed.indent(last) + ed.body(last).size(); // and the last one's ends

    // In a block comment already: its markers come off (their lines too, when that's all they hold).
    if (style.hasBlock() && ed.body(first).starts_with(style.blockStart) && ed.body(last).ends_with(style.blockEnd)) {
        const size_t opening = start, closing = end - style.blockEnd.size();
        const size_t inside = opening + style.blockStart.size();
        if (closing >= inside && text.substr(inside, closing - inside).find(style.blockEnd) == npos) {
            const bool openingLine = ed.body(first) == style.blockStart, closingLine = ed.body(last) == style.blockEnd;
            if (openingLine) ed.removeLine(first);
            else ed.removeToken(opening, style.blockStart, closing);
            if (closingLine) {
                ed.removeLine(last);
            } else {
                const bool spaceBefore = closing > inside && text[closing - 1] == ' ';
                ed.replace(spaceBefore ? closing - 1 : closing, end, "");
            }
            const int removed = int(openingLine) + int(closingLine);
            return {ed.edits(), std::pair{selected.first, selected.second - removed}, {}};
        }
    }

    // Every line a line comment already: they come off.
    if (style.hasLine()) {
        bool all = true;
        for (int i = first; i <= last && all; ++i)
            if (!ed.blank(i) && !ed.body(i).starts_with(style.line)) all = false;
        if (all) {
            for (int i = first; i <= last; ++i)
                if (!ed.blank(i)) ed.removeToken(ed.line(i).start + ed.indent(i), style.line, ed.line(i).end);
            return {ed.edits(), selected, {}};
        }
    }
    // Or every line a block comment of its own (CSS and HTML lines commented out one at a time).
    if (style.hasBlock()) {
        bool all = true;
        for (int i = first; i <= last && all; ++i)
            if (!ed.blank(i) && !wrappedLine(ed, i, style)) all = false;
        if (all) {
            for (int i = first; i <= last; ++i)
                if (!ed.blank(i)) unwrapLine(ed, i, style);
            return {ed.edits(), selected, {}};
        }
    }

    // Comment out, at the indentation of the least indented line.
    int least = first;
    for (int i = first; i <= last; ++i)
        if (!ed.blank(i) && ed.indent(i) < ed.indent(least)) least = i;
    const size_t column = ed.indent(least);
    const std::string indent(ed.textOf(least).substr(0, column));

    if (style.hasBlock() && text.substr(start, end - start).find(style.blockEnd) == npos) {
        const std::string eol = ed.eol(first > 0 ? first - 1 : first);
        if (first > 0) ed.insert(ed.line(first - 1).end, eol + indent + style.blockStart);
        else ed.insert(top.start, indent + style.blockStart + eol);
        ed.insert(bottom.end, eol + indent + style.blockEnd);
        return {ed.edits(), std::pair{selected.first, selected.second + 2}, {}};
    }
    if (style.hasLine()) {
        for (int i = first; i <= last; ++i)
            if (!ed.blank(i)) ed.insert(ed.line(i).start + column, style.line + " ");
        return {ed.edits(), selected, {}};
    }
    return {{}, {}, nestingMessage(style)};
}

} // namespace kevc::comments
