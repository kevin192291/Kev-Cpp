#include "core/Search.h"

#include "core/ProjectSession.h"

#include <algorithm>
#include <fnmatch.h>
#include <fstream>
#include <limits>

namespace fs = std::filesystem;

namespace kevc::search {

namespace {

constexpr size_t kMaxFileBytes = 8 * 1024 * 1024;
// std::regex recurses per character it matches, so very long lines (minified code) could exhaust
// the stack; regular expressions skip lines longer than this.
constexpr size_t kMaxRegexLine = 4096;

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// A word starts here: after a separator, or at a capital after a small letter (camelCase).
bool wordStart(std::string_view s, size_t j) {
    if (j == 0) return true;
    const char p = s[j - 1], c = s[j];
    if (p == '/' || p == '_' || p == '-' || p == '.' || p == ' ' || p == '+') return true;
    return (c >= 'A' && c <= 'Z') && ((p >= 'a' && p <= 'z') || (p >= '0' && p <= '9'));
}

// Replaces each byte that isn't part of valid UTF-8 with '?', keeping every offset where it was
// (files in Latin-1 still show, and the panel's markup stays valid).
std::string validUtf8(std::string_view s) {
    std::string out(s);
    const auto byte = [&s](size_t i) { return static_cast<unsigned char>(s[i]); };
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = byte(i);
        size_t n = 0;
        unsigned char lo = 0x80, hi = 0xBF; // the allowed range of the second byte
        if (b < 0x80) n = 1;
        else if (b >= 0xC2 && b <= 0xDF) n = 2;
        else if (b >= 0xE0 && b <= 0xEF) n = 3, lo = b == 0xE0 ? 0xA0 : 0x80, hi = b == 0xED ? 0x9F : 0xBF;
        else if (b >= 0xF0 && b <= 0xF4) n = 4, lo = b == 0xF0 ? 0x90 : 0x80, hi = b == 0xF4 ? 0x8F : 0xBF;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; ++k) {
            const unsigned char c = byte(i + k);
            ok = k == 1 ? (c >= lo && c <= hi) : continuation(static_cast<char>(c));
        }
        if (!ok) {
            out[i++] = '?';
            continue;
        }
        i += n;
    }
    return out;
}

bool matchPattern(std::string pattern, std::string_view path) {
    if (pattern.starts_with("./")) pattern.erase(0, 2);
    const std::string p(path);
    if (pattern.find('/') == std::string::npos) {
        // A name: the file's own, or any folder's on the way ("tests" finds tests/a.cpp).
        size_t start = 0;
        while (true) {
            const size_t slash = p.find('/', start);
            const std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (fnmatch(pattern.c_str(), part.c_str(), 0) == 0) return true;
            if (slash == std::string::npos) return false;
            start = slash + 1;
        }
    }
    while (pattern.size() > 1 && pattern.back() == '/') pattern.pop_back();
    if (pattern.find_first_of("*?[") == std::string::npos) return p == pattern || p.starts_with(pattern + "/");
    // Without FNM_PATHNAME a * also crosses folders, so src/*.h finds src/a/b.h too.
    return fnmatch(pattern.c_str(), p.c_str(), 0) == 0 || fnmatch((pattern + "/*").c_str(), p.c_str(), 0) == 0;
}

} // namespace

bool isHiddenFileName(std::string_view name) {
    return name.starts_with('.') || name.ends_with(".kevc-tmp") || name.ends_with(".tmp");
}

std::vector<std::string> listFiles(const fs::path& root, size_t limit, const std::atomic<bool>* cancel) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (cancel && *cancel) break;
        const fs::directory_entry& entry = *it;
        const std::string name = entry.path().filename().string();
        std::error_code e;
        if (entry.is_directory(e)) {
            if (isIgnoredDirName(name)) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(e) || isHiddenFileName(name)) continue;
        out.push_back(entry.path().lexically_relative(root).generic_string());
        if (out.size() >= limit) break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------------------------
// Go to file

std::optional<FileMatch> matchFile(std::string_view query, std::string_view path) {
    std::string q;
    for (char c : query)
        if (c != ' ') q += lower(c);
    if (q.empty()) return FileMatch{std::string(path), 0, {}};
    const size_t n = path.size(), m = q.size();
    if (m > n) return std::nullopt;
    {
        size_t i = 0;
        for (size_t j = 0; j < n && i < m; ++j)
            if (lower(path[j]) == q[i]) ++i;
        if (i < m) return std::nullopt;
    }

    // score[i][j]: the best total with letter i of the query at path[j]; from[i][j]: where letter
    // i-1 went. A letter scores more at a word start and in the file name; runs earn a bonus and
    // every skipped character between two letters costs a point.
    constexpr int kNone = std::numeric_limits<int>::min() / 4;
    constexpr int kRun = 6, kGap = 1, kWordStart = 10, kInName = 3;
    const size_t slash = path.rfind('/');
    const size_t nameStart = slash == std::string_view::npos ? 0 : slash + 1;
    std::vector<int> score(m * n, kNone), from(m * n, -1);
    const auto at = [n](size_t i, size_t j) { return i * n + j; };
    for (size_t i = 0; i < m; ++i) {
        int best = kNone, bestAt = -1; // the best letter i-1 at least two characters back, gap paid
        for (size_t j = i; j < n; ++j) {
            if (i > 0 && j >= 2) {
                if (best != kNone) best -= kGap;
                const int candidate = score[at(i - 1, j - 2)];
                if (candidate != kNone && candidate - kGap > best) best = candidate - kGap, bestAt = static_cast<int>(j - 2);
            }
            if (lower(path[j]) != q[i]) continue;
            const int here = 1 + (wordStart(path, j) ? kWordStart : 0) + (j >= nameStart ? kInName : 0);
            if (i == 0) {
                score[at(i, j)] = here;
                continue;
            }
            int previous = kNone, previousAt = -1;
            if (const int run = score[at(i - 1, j - 1)]; run != kNone) previous = run + kRun, previousAt = static_cast<int>(j - 1);
            if (best != kNone && best > previous) previous = best, previousAt = bestAt;
            if (previous == kNone) continue;
            score[at(i, j)] = here + previous;
            from[at(i, j)] = previousAt;
        }
    }
    int total = kNone;
    size_t end = 0;
    for (size_t j = 0; j < n; ++j)
        if (score[at(m - 1, j)] != kNone && score[at(m - 1, j)] >= total) total = score[at(m - 1, j)], end = j;
    if (total == kNone) return std::nullopt;

    FileMatch match{std::string(path), total, std::vector<size_t>(m)};
    for (size_t i = m, j = end; i-- > 0;) {
        match.positions[i] = j;
        if (i > 0) j = static_cast<size_t>(from[at(i, j)]);
    }
    // Letters picked one or two at a time from the middles of words aren't a match anyone meant
    // ("greeting" isn't DesiGneR EdiTING.cpp). Allow one such piece ("pjt" for ProjectTree).
    int scattered = 0;
    for (size_t i = 0; i < m;) {
        size_t k = i + 1;
        while (k < m && match.positions[k] == match.positions[k - 1] + 1) ++k;
        if (k - i < 3 && !wordStart(path, match.positions[i])) ++scattered;
        i = k;
    }
    if (scattered > 1) return std::nullopt;
    // Typing the start of a name, or all of it, should put that file first.
    const std::string name = lowered(path.substr(nameStart));
    const size_t dot = name.rfind('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    if (name.starts_with(q)) match.score += 15;
    if (stem == q) match.score += 20;
    if (name == q) match.score += 25;
    return match;
}

std::vector<FileMatch> rankFiles(const std::vector<std::string>& paths, std::string_view query, size_t limit) {
    std::vector<FileMatch> out;
    for (const auto& p : paths)
        if (auto m = matchFile(query, p)) out.push_back(std::move(*m));
    const auto better = [](const FileMatch& a, const FileMatch& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.path.size() != b.path.size()) return a.path.size() < b.path.size();
        return a.path < b.path;
    };
    if (out.size() > limit) {
        std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(limit), out.end(), better);
        out.resize(limit);
    } else {
        std::sort(out.begin(), out.end(), better);
    }
    return out;
}

bool containsAllWords(std::string_view text, std::string_view query) {
    const std::string hay = lowered(text);
    size_t i = 0;
    while (i < query.size()) {
        while (i < query.size() && (query[i] == ' ' || query[i] == '\t')) ++i;
        size_t j = i;
        while (j < query.size() && query[j] != ' ' && query[j] != '\t') ++j;
        if (j > i && hay.find(lowered(query.substr(i, j - i))) == std::string::npos) return false;
        i = j;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Find in files

Matcher::Matcher(TextQuery query) : query_(std::move(query)) {
    if (query_.text.empty()) return;
    if (!query_.regex) {
        needle_ = query_.matchCase ? query_.text : lowered(query_.text);
        return;
    }
    const std::string pattern = query_.wholeWord ? "\\b(?:" + query_.text + ")\\b" : query_.text;
    auto flags = std::regex::ECMAScript;
    if (!query_.matchCase) flags |= std::regex::icase;
    try {
        regex_.emplace(pattern, flags);
    } catch (const std::regex_error& e) {
        error_ = std::string("Invalid regular expression: ") + e.what();
    }
}

template <typename F>
void Matcher::scan(std::string_view line, F&& f) const {
    if (query_.text.empty() || !valid()) return;
    if (regex_) {
        if (line.size() > kMaxRegexLine) return;
        const char* begin = line.data();
        const char* end = begin + line.size();
        const char* p = begin;
        auto flags = std::regex_constants::match_default;
        std::cmatch m;
        while (std::regex_search(p, end, m, *regex_, flags)) {
            const char* first = m[0].first;
            if (m.length(0) == 0) { // ^, \b and the like: nothing to show; look further on
                if (first >= end) break;
                p = first + 1;
            } else {
                f(static_cast<size_t>(first - begin), static_cast<size_t>(m.length(0)), &m);
                p = m[0].second;
            }
            flags |= std::regex_constants::match_prev_avail; // so \b and ^ see the character before p
        }
        return;
    }
    std::string low;
    std::string_view hay = line;
    if (!query_.matchCase) hay = low = lowered(line);
    for (size_t pos = hay.find(needle_); pos != std::string_view::npos; pos = hay.find(needle_, pos)) {
        const size_t end = pos + needle_.size();
        if (query_.wholeWord && ((pos > 0 && isWordChar(hay[pos - 1])) || (end < hay.size() && isWordChar(hay[end])))) {
            ++pos;
            continue;
        }
        f(pos, needle_.size(), static_cast<const std::cmatch*>(nullptr));
        pos = end;
    }
}

std::vector<std::pair<size_t, size_t>> Matcher::find(std::string_view line) const {
    std::vector<std::pair<size_t, size_t>> out;
    scan(line, [&out](size_t column, size_t length, const std::cmatch*) { out.emplace_back(column, length); });
    return out;
}

std::string Matcher::replace(std::string_view contents, const std::string& with, size_t* count) const {
    size_t n = 0;
    if (count) *count = 0;
    if (query_.text.empty() || !valid() || looksBinary(contents)) return std::string(contents);
    std::string out;
    out.reserve(contents.size());
    for (size_t start = 0; start <= contents.size();) {
        const size_t newline = contents.find('\n', start);
        std::string_view line = contents.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        const bool cr = !line.empty() && line.back() == '\r';
        if (cr) line.remove_suffix(1);
        size_t done = 0;
        scan(line, [&](size_t column, size_t length, const std::cmatch* m) {
            out.append(line.substr(done, column - done));
            out += m ? m->format(with) : with;
            done = column + length;
            ++n;
        });
        out.append(line.substr(done));
        if (cr) out += '\r';
        if (newline == std::string_view::npos) break;
        out += '\n';
        start = newline + 1;
    }
    if (count) *count = n;
    return out;
}

std::vector<LineMatch> Matcher::search(std::string_view contents, size_t limit) const {
    std::vector<LineMatch> out;
    if (query_.text.empty() || !valid() || limit == 0 || looksBinary(contents)) return out;
    // Most files don't hold the text at all: one look at the whole file settles that.
    if (!regex_ && (query_.matchCase ? contents : std::string_view(lowered(contents))).find(needle_) == std::string_view::npos)
        return out;
    int number = 0;
    for (size_t start = 0; start <= contents.size() && out.size() < limit;) {
        const size_t newline = contents.find('\n', start);
        std::string_view line = contents.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        ++number;
        for (const auto& [column, length] : find(line)) {
            LineMatch match = makePreview(line, column, length);
            match.line = number;
            out.push_back(std::move(match));
            if (out.size() >= limit) break;
        }
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    return out;
}

PathFilter::PathFilter(std::string_view patterns) {
    std::string word;
    const auto flush = [this, &word] {
        if (word.starts_with('!')) {
            if (word.size() > 1) exclude_.push_back(word.substr(1));
        } else if (!word.empty()) {
            include_.push_back(word);
        }
        word.clear();
    };
    for (char c : patterns) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') flush();
        else word += c;
    }
    flush();
}

bool PathFilter::matches(std::string_view relativePath) const {
    for (const auto& pattern : exclude_)
        if (matchPattern(pattern, relativePath)) return false;
    if (include_.empty()) return true;
    for (const auto& pattern : include_)
        if (matchPattern(pattern, relativePath)) return true;
    return false;
}

bool looksBinary(std::string_view contents) {
    return contents.substr(0, 8000).find('\0') != std::string_view::npos;
}

LineMatch makePreview(std::string_view line, size_t column, size_t length, size_t before, size_t maxBytes) {
    LineMatch out;
    out.column = column;
    out.length = length;
    // Whole characters only, so the panel can split the preview into valid UTF-8 pieces.
    size_t matchStart = std::min(column, line.size()), matchEnd = std::min(column + length, line.size());
    while (matchStart > 0 && continuation(line[matchStart])) --matchStart;
    while (matchEnd < line.size() && continuation(line[matchEnd])) ++matchEnd;

    size_t start = 0;
    while (start < matchStart && (line[start] == ' ' || line[start] == '\t')) ++start;
    std::string prefix;
    if (matchStart - start > before) {
        start = matchStart - before;
        while (start < matchStart && continuation(line[start])) ++start;
        prefix = "…";
    }
    size_t end = std::min(line.size(), std::max(start + maxBytes, matchEnd));
    while (end > matchEnd && end < line.size() && continuation(line[end])) --end;
    std::string text = validUtf8(line.substr(start, end - start));
    std::replace(text.begin(), text.end(), '\t', ' ');
    out.preview = prefix + text + (end < line.size() ? "…" : "");
    out.previewColumn = prefix.size() + (matchStart - start);
    out.previewLength = matchEnd - matchStart;
    return out;
}

Totals searchFiles(const fs::path& root, const std::vector<std::string>& files, const Matcher& matcher,
                   const PathFilter& filter, const std::map<std::string, std::string>& unsaved, size_t limit,
                   const std::atomic<bool>* cancel, const std::function<void(FileResult&&)>& found) {
    Totals totals;
    std::string text;
    for (const auto& rel : files) {
        if (cancel && *cancel) break;
        if (!filter.matches(rel)) continue;
        std::string_view contents;
        if (auto it = unsaved.find(rel); it != unsaved.end()) {
            contents = it->second;
        } else {
            std::error_code ec;
            const auto size = fs::file_size(root / rel, ec);
            if (ec || size > kMaxFileBytes) continue;
            std::ifstream in(root / rel, std::ios::binary);
            if (!in) continue;
            text.resize(size);
            in.read(text.data(), static_cast<std::streamsize>(size));
            text.resize(static_cast<size_t>(in.gcount()));
            contents = text;
        }
        ++totals.files;
        auto matches = matcher.search(contents, limit - totals.matches);
        if (matches.empty()) continue;
        totals.matches += matches.size();
        found(FileResult{rel, std::move(matches)});
        if (totals.matches >= limit) {
            totals.truncated = true;
            break;
        }
    }
    return totals;
}

} // namespace kevc::search
