#pragma once

#include "core/Format.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kevc::comments {

// Edit → Toggle Comment (Ctrl+/)'s GTK-free half: how a file's language writes comments, and the
// edits that comment lines out or back in.

// An empty token: the language has no comment of that kind (CSS and HTML have no line comments).
struct Style {
    std::string line;                 // "//", "#", "--"
    std::string blockStart, blockEnd; // "/*" and "*/", "<!--" and "-->"
    bool hasLine() const { return !line.empty(); }
    bool hasBlock() const { return !blockStart.empty() && !blockEnd.empty(); }
    bool none() const { return !hasLine() && !hasBlock(); }
    bool operator==(const Style&) const = default;
};

// A file's comments: what its language's definition says (GtkSourceView's line-comment-start,
// block-comment-start and block-comment-end metadata), with the gaps filled in: JSON has none, but
// tsconfig.json, jsconfig.json, .jsonc files and VS Code's settings take JavaScript's.
Style fileStyle(const std::filesystem::path& file, std::string_view languageId, Style declared);

// Where a byte offset is in an HTML or Svelte file: the markup, a <script> or a <style>. A line
// that starts with the tag opening or closing a script belongs to the markup around it.
enum class Section { Markup, Script, Style };
Section sectionAt(std::string_view text, size_t offset);

// The comments that apply at a byte offset: in HTML and Svelte files, scripts comment as JavaScript
// and styles as CSS (or SCSS and Less, which also have "//"), the rest as the file does.
Style styleAt(std::string_view text, size_t offset, std::string_view languageId, const Style& fileStyle);

struct Toggle {
    std::vector<format::TextEdit> edits; // in the text as it was; none when there's nothing to do
    // Several lines: the lines (0-based, first and last, in the text afterwards) that hold them and
    // their comment markers, to select so that Ctrl+/ again takes the comment off.
    std::optional<std::pair<int, int>> select;
    std::string message; // why nothing was done
};

// Comments out lines firstLine to lastLine (0-based) of `text`, or back in if they're commented.
//  - One line gets a line comment after its indentation ("// "), or, in a language without line
//    comments, a block comment around it ("/* … */"). A line on its own holding just "/*" or "*/"
//    (as several lines get, below) takes off that whole block comment.
//  - Several lines get a block comment whose markers are lines of their own, indented like the
//    least indented line. If the lines already hold the block's end ("*/" in C), which would end it
//    early, each line gets a line comment instead.
//  - Lines that start with the block's start and end with its end, that all start with the line
//    comment, or that are each a block comment of their own, are commented already: their comment
//    markers come off.
// The markers' lines go in and come out between the end of the line above and their own end, so a
// breakpoint's mark at the start of a line stays on its line (all but the file's first line's, when
// a comment opens above it: nothing can go in before that mark without going after it).
Toggle toggle(std::string_view text, int firstLine, int lastLine, std::string_view languageId, const Style& fileStyle);

} // namespace kevc::comments
