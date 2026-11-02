#include "core/Format.h"

#include "core/Lsp.h"
#include "core/WebProject.h"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <unordered_map>

namespace kevc::format {

namespace fs = std::filesystem;

namespace {

bool continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

int utf16Length(std::string_view s) {
    int units = 0;
    for (char c : s)
        if (!continuation(c)) units += static_cast<unsigned char>(c) >= 0xF0 ? 2 : 1;
    return units;
}

// The text's lines, each with its '\n' (the last may have none), and where each starts.
struct Lines {
    std::vector<std::string_view> text;
    std::vector<size_t> start;
};

Lines splitLines(std::string_view s) {
    Lines out;
    for (size_t pos = 0; pos < s.size();) {
        const size_t nl = s.find('\n', pos);
        const size_t end = nl == std::string_view::npos ? s.size() : nl + 1;
        out.text.push_back(s.substr(pos, end - pos));
        out.start.push_back(pos);
        pos = end;
    }
    return out;
}

// Byte offset of an LSP position. Columns past a line's end stop at its end (before the newline);
// lines past the last one are the end of the text.
size_t offsetOf(std::string_view text, const Lines& lines, int line, int utf16) {
    if (line < 0) return 0;
    if (static_cast<size_t>(line) >= lines.text.size()) return text.size();
    std::string_view l = lines.text[static_cast<size_t>(line)];
    if (!l.empty() && l.back() == '\n') l.remove_suffix(1);
    size_t i = 0;
    for (int units = 0; i < l.size() && units < utf16;) {
        units += static_cast<unsigned char>(l[i]) >= 0xF0 ? 2 : 1;
        ++i;
        while (i < l.size() && continuation(l[i])) ++i;
    }
    return lines.start[static_cast<size_t>(line)] + i;
}

// The LSP position of a byte offset (at a character boundary) in the text `lines` came from.
std::pair<int, int> positionOf(std::string_view text, const Lines& lines, size_t offset) {
    // The end of a text whose last line ends in '\n' is the start of the (empty) line after it.
    if (offset >= text.size() && (text.empty() || text.back() == '\n')) return {static_cast<int>(lines.text.size()), 0};
    const auto it = std::upper_bound(lines.start.begin(), lines.start.end(), offset);
    const size_t line = static_cast<size_t>(it - lines.start.begin()) - 1;
    return {static_cast<int>(line), utf16Length(text.substr(lines.start[line], offset - lines.start[line]))};
}

std::string squashed(std::string_view line) {
    std::string out;
    for (char c : line)
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    return out;
}

// The matched pairs (index in a, index in b) of a longest common subsequence, by Myers' O(ND)
// algorithm; nullopt when more than `maxD` lines would have to be inserted or deleted.
std::optional<std::vector<std::pair<int, int>>> commonLines(const std::vector<int>& a, const std::vector<int>& b,
                                                            int maxD) {
    const int n = static_cast<int>(a.size()), m = static_cast<int>(b.size());
    const int max = std::min(n + m, maxD);
    const int offset = max + 1;
    std::vector<int> v(static_cast<size_t>(2 * max + 3), 0);
    // trace[d]: v as it was before step d, for k in [-d-1, d+1] (index k + d + 1).
    std::vector<std::vector<int>> trace;
    for (int d = 0; d <= max; ++d) {
        trace.emplace_back(v.begin() + (offset - d - 1), v.begin() + (offset + d + 2));
        for (int k = -d; k <= d; k += 2) {
            const bool down = k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1]);
            int x = down ? v[offset + k + 1] : v[offset + k - 1] + 1;
            int y = x - k;
            while (x < n && y < m && a[x] == b[y]) ++x, ++y;
            v[offset + k] = x;
            if (x < n || y < m) continue;

            std::vector<std::pair<int, int>> pairs;
            x = n, y = m;
            for (int e = d; e > 0; --e) {
                const auto& t = trace[static_cast<size_t>(e)];
                auto at = [&t, e](int kk) { return t[static_cast<size_t>(kk + e + 1)]; };
                const int kk = x - y;
                const bool wentDown = kk == -e || (kk != e && at(kk - 1) < at(kk + 1));
                const int prevK = wentDown ? kk + 1 : kk - 1;
                const int prevX = at(prevK), prevY = prevX - prevK;
                const int snakeX = wentDown ? prevX : prevX + 1; // where this step's diagonal began
                while (x > snakeX && y > snakeX - kk) {
                    --x, --y;
                    pairs.emplace_back(x, y);
                }
                x = prevX, y = prevY;
            }
            while (x > 0 && y > 0) {
                --x, --y;
                pairs.emplace_back(x, y);
            }
            std::reverse(pairs.begin(), pairs.end());
            return pairs;
        }
    }
    return std::nullopt;
}

class EditMaker {
public:
    EditMaker(std::string_view before, const Lines& lines) : before_(before), lines_(lines) {}

    // `oldText` (which starts at byte `at` of the old text) becomes `newText`, as small an edit as can be.
    void replace(size_t at, std::string_view oldText, std::string_view newText) {
        size_t prefix = 0;
        while (prefix < oldText.size() && prefix < newText.size() && oldText[prefix] == newText[prefix]) ++prefix;
        while (prefix > 0 && ((prefix < oldText.size() && continuation(oldText[prefix])) ||
                              (prefix < newText.size() && continuation(newText[prefix]))))
            --prefix;
        size_t suffix = 0;
        const size_t room = std::min(oldText.size(), newText.size()) - prefix;
        while (suffix < room && oldText[oldText.size() - 1 - suffix] == newText[newText.size() - 1 - suffix]) ++suffix;
        while (suffix > 0 &&
               (continuation(oldText[oldText.size() - suffix]) || continuation(newText[newText.size() - suffix])))
            --suffix;
        if (prefix + suffix == oldText.size() && prefix + suffix == newText.size()) return; // the same
        const auto [startLine, startCol] = positionOf(before_, lines_, at + prefix);
        const auto [endLine, endCol] = positionOf(before_, lines_, at + oldText.size() - suffix);
        edits_.push_back({startLine, startCol, endLine, endCol,
                          std::string(newText.substr(prefix, newText.size() - prefix - suffix))});
    }

    // When `oldText` and `newText` differ only in whitespace: an edit for each stretch of whitespace
    // between the same two characters that changed, so nothing but whitespace is touched. False,
    // with nothing done, when they differ in more.
    bool whitespace(size_t at, std::string_view oldText, std::string_view newText) {
        if (squashed(oldText) != squashed(newText)) return false;
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        for (size_t i = 0, j = 0;;) {
            size_t i2 = i, j2 = j;
            while (i2 < oldText.size() && space(oldText[i2])) ++i2;
            while (j2 < newText.size() && space(newText[j2])) ++j2;
            replace(at + i, oldText.substr(i, i2 - i), newText.substr(j, j2 - j));
            if (i2 >= oldText.size() || j2 >= newText.size()) break;
            i = i2 + 1, j = j2 + 1; // the same character on both sides
        }
        return true;
    }

    std::vector<TextEdit> take() { return std::move(edits_); }

private:
    std::string_view before_;
    const Lines& lines_;
    std::vector<TextEdit> edits_;
};

} // namespace

std::vector<TextEdit> parseTextEdits(const nlohmann::json& edits) {
    std::vector<TextEdit> out;
    if (!edits.is_array()) return out;
    for (const auto& e : edits) {
        if (!e.is_object() || !e.contains("range") || !e["range"].is_object()) continue;
        const auto& range = e["range"];
        const auto& start = range.value("start", nlohmann::json::object());
        const auto& end = range.value("end", nlohmann::json::object());
        out.push_back({start.value("line", 0), start.value("character", 0), end.value("line", 0),
                       end.value("character", 0), e.value("newText", std::string())});
    }
    return out;
}

std::vector<TextEdit> lastFirst(std::vector<TextEdit> edits) {
    std::vector<size_t> order(edits.size());
    std::iota(order.begin(), order.end(), 0);
    // Later starts first; of edits starting at the same place, the later one in the list goes in
    // first, so that the earlier one's text ends up in front of it.
    std::sort(order.begin(), order.end(), [&edits](size_t a, size_t b) {
        const auto& x = edits[a];
        const auto& y = edits[b];
        if (x.startLine != y.startLine) return x.startLine > y.startLine;
        if (x.startUtf16 != y.startUtf16) return x.startUtf16 > y.startUtf16;
        return a > b;
    });
    std::vector<TextEdit> out;
    out.reserve(edits.size());
    for (size_t i : order) out.push_back(std::move(edits[i]));
    return out;
}

std::string applyTextEdits(std::string_view text, const std::vector<TextEdit>& edits) {
    const Lines lines = splitLines(text);
    std::string out(text);
    for (const auto& e : lastFirst(edits)) {
        const size_t start = offsetOf(text, lines, e.startLine, e.startUtf16);
        const size_t end = std::max(start, offsetOf(text, lines, e.endLine, e.endUtf16));
        out.replace(start, end - start, e.newText);
    }
    return out;
}

std::vector<TextEdit> diffEdits(std::string_view before, std::string_view after) {
    if (before == after) return {};
    const Lines a = splitLines(before), b = splitLines(after);
    EditMaker edits(before, a);
    // clang-format, and other formatters most of the time, only change whitespace: then the old and
    // new text line up character for character, and only the whitespace between them is edited.
    if (edits.whitespace(0, before, after)) return edits.take();

    // Otherwise lines are matched first (by what's on them besides whitespace), and edited in place.
    auto startOf = [&](size_t line) { return line < a.start.size() ? a.start[line] : before.size(); };
    auto join = [](const Lines& lines, size_t from, size_t to) {
        if (from >= to) return std::string_view();
        const size_t start = lines.start[from];
        return std::string_view(lines.text[from].data(), lines.start[to - 1] + lines.text[to - 1].size() - start);
    };

    // Lines the same at both ends need no looking at.
    size_t head = 0;
    while (head < a.text.size() && head < b.text.size() && a.text[head] == b.text[head]) ++head;
    size_t tail = 0;
    while (tail < a.text.size() - head && tail < b.text.size() - head &&
           a.text[a.text.size() - 1 - tail] == b.text[b.text.size() - 1 - tail])
        ++tail;
    const size_t aEnd = a.text.size() - tail, bEnd = b.text.size() - tail;

    std::unordered_map<std::string, int> ids;
    auto keys = [&ids](const Lines& lines, size_t from, size_t to) {
        std::vector<int> out;
        for (size_t i = from; i < to; ++i)
            out.push_back(ids.emplace(squashed(lines.text[i]), static_cast<int>(ids.size())).first->second);
        return out;
    };
    const std::vector<int> aKeys = keys(a, head, aEnd), bKeys = keys(b, head, bEnd);
    // Past this many differing lines, the middle is edited as one block: rare for a formatter, whose
    // lines differ mostly in whitespace, and the search would cost more than it saves.
    constexpr int kMaxDifferences = 1000;
    std::vector<std::pair<int, int>> pairs =
        commonLines(aKeys, bKeys, kMaxDifferences).value_or(std::vector<std::pair<int, int>>{});
    pairs.emplace_back(static_cast<int>(aKeys.size()), static_cast<int>(bKeys.size())); // the end, as a last pair

    auto change = [&edits](size_t at, std::string_view from, std::string_view to) {
        if (!edits.whitespace(at, from, to)) edits.replace(at, from, to);
    };
    auto pair = [&](size_t i, size_t j) { change(a.start[i], a.text[i], b.text[j]); };
    size_t ia = head, ib = head;
    for (const auto& [pi, pj] : pairs) {
        const size_t i = head + static_cast<size_t>(pi), j = head + static_cast<size_t>(pj);
        // Unmatched lines between two matches: line by line when there are as many on each side
        // (each keeps its marks), else one edit over the lot.
        if (i - ia == j - ib) {
            for (size_t k = 0; k < i - ia; ++k) pair(ia + k, ib + k);
        } else {
            change(startOf(ia), join(a, ia, i), join(b, ib, j));
        }
        if (i < aEnd && j < bEnd) pair(i, j);
        ia = i + 1, ib = j + 1;
    }
    return edits.take();
}

int utf16Offset(std::string_view text, int line, int utf16Column) {
    const Lines lines = splitLines(text);
    return utf16Length(text.substr(0, offsetOf(text, lines, line, utf16Column)));
}

std::optional<fs::path> clangFormatConfig(const fs::path& file) {
    std::error_code ec;
    for (fs::path dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        for (const char* name : {".clang-format", "_clang-format"})
            if (fs::is_regular_file(dir / name, ec)) return dir / name;
        if (dir == dir.parent_path()) break;
    }
    return std::nullopt;
}

Prettier findPrettier(const fs::path& file) {
    for (fs::path dir = file.parent_path(); !dir.empty(); dir = dir.parent_path()) {
        if (fs::path script = nodeBin(dir, "prettier"); !script.empty()) return {script, dir};
        if (dir == dir.parent_path()) break;
    }
    return {};
}

Formatter formatterFor(const fs::path& file, bool projectHasPrettier) {
    const lsp::Server server = lsp::serverFor(file);
    if (server == lsp::Server::Clangd) return Formatter::Clangd;
    if (server == lsp::Server::Svelte) return Formatter::Svelte;
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    static const char* prettierKinds[] = {".js",   ".jsx",      ".mjs",   ".cjs", ".ts",      ".tsx",  ".mts",  ".cts",
                                          ".json", ".jsonc",    ".json5", ".css", ".scss",    ".less", ".html", ".vue",
                                          ".md",   ".markdown", ".yaml",  ".yml", ".graphql", ".gql"};
    if (projectHasPrettier &&
        std::find(std::begin(prettierKinds), std::end(prettierKinds), ext) != std::end(prettierKinds))
        return Formatter::Prettier;
    if (server == lsp::Server::TypeScript) return Formatter::TypeScript;
    return Formatter::None;
}

std::string standInName(int serial) {
    return "document-" + std::to_string(serial) + ".kevc-format";
}

} // namespace kevc::format
