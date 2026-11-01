#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace kevc::search {

// The files the Project tab shows under `root` (no dot files or folders, build output or
// node_modules), as generic paths relative to root, sorted. Stops after `limit` files, or early
// once `cancel` is set.
std::vector<std::string> listFiles(const std::filesystem::path& root, size_t limit = 200000,
                                   const std::atomic<bool>* cancel = nullptr);
// Files the Project tab hides: dot files and editor temp files.
bool isHiddenFileName(std::string_view name);

// ---------------------------------------------------------------------------------------------
// Go to file: the typed letters in order anywhere in the path (case ignored), ranked so that
// matches in the file name, at word starts and in runs beat scattered ones ("mwc" finds MainWindow.cpp).

struct FileMatch {
    std::string path;
    int score = 0;
    std::vector<size_t> positions; // byte offsets in path of the matched letters
};
std::optional<FileMatch> matchFile(std::string_view query, std::string_view path);
// Best matches first (then shorter paths); at most `limit`.
std::vector<FileMatch> rankFiles(const std::vector<std::string>& paths, std::string_view query, size_t limit);

// The Routes tab filter: every space-separated word of `query` appears in `text`, case ignored.
bool containsAllWords(std::string_view text, std::string_view query);

// ---------------------------------------------------------------------------------------------
// Find in files

struct TextQuery {
    std::string text;
    bool matchCase = false;
    bool wholeWord = false;
    bool regex = false; // ECMAScript syntax
};

struct LineMatch {
    int line = 0;          // 1-based
    size_t column = 0;     // bytes from the start of the line
    size_t length = 0;     // bytes
    std::string preview;   // the line, trimmed and shortened around the match for a narrow panel
    size_t previewColumn = 0; // the match in preview, widened to whole characters
    size_t previewLength = 0;
};

struct FileResult {
    std::string path; // relative to the root, generic
    std::vector<LineMatch> matches;
};

class Matcher {
public:
    explicit Matcher(TextQuery query);
    bool valid() const { return error_.empty(); }
    const std::string& error() const { return error_; } // a bad regular expression
    // Matches in one line as (byte column, byte length), left to right, not overlapping.
    std::vector<std::pair<size_t, size_t>> find(std::string_view line) const;
    // Every match in a file's text, up to `limit`; none for binary files.
    std::vector<LineMatch> search(std::string_view contents, size_t limit) const;

private:
    TextQuery query_;
    std::string needle_; // lower-cased unless matchCase
    std::optional<std::regex> regex_;
    std::string error_;
};

// The "files to include" field: comma- or space-separated patterns. `*.cpp` matches file names,
// `src/` a folder, `src/*.h` paths from the root; a leading ! excludes. Empty matches every file.
class PathFilter {
public:
    explicit PathFilter(std::string_view patterns);
    bool matches(std::string_view relativePath) const;

private:
    std::vector<std::string> include_, exclude_;
};

bool looksBinary(std::string_view contents);
// `line` around a match: leading whitespace dropped, at most `before` bytes ahead of the match
// (cut at a character, marked with …), at most `maxBytes` long. Tabs become spaces.
LineMatch makePreview(std::string_view line, size_t column, size_t length, size_t before = 16, size_t maxBytes = 240);

struct Totals {
    size_t files = 0;   // files searched
    size_t matches = 0;
    bool truncated = false; // stopped at the limit
};
// Searches `files` (relative to root) that pass `filter`; a file in `unsaved` (by relative path) is
// searched as that text instead of what's on disk. Each file with matches goes to `found` as it's
// done. Skips binary files and files over 8 MB.
Totals searchFiles(const std::filesystem::path& root, const std::vector<std::string>& files, const Matcher& matcher,
                   const PathFilter& filter, const std::map<std::string, std::string>& unsaved, size_t limit,
                   const std::atomic<bool>* cancel, const std::function<void(FileResult&&)>& found);

} // namespace kevc::search
