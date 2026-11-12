#include "core/Boilerplate.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc::boilerplate {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }
bool isAsciiAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }
char toUpper(char c) { return isLower(c) ? char(c - 'a' + 'A') : c; }
char toLower(char c) { return isUpper(c) ? char(c - 'A' + 'a') : c; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (isSpace(s.front()) || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (isSpace(s.back()) || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = toLower(c);
    return out;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
}

bool blank(std::string_view line) {
    return std::all_of(line.begin(), line.end(), [](char c) { return isSpace(c) || c == '\r'; });
}

std::string_view leadingSpace(std::string_view line) {
    size_t n = 0;
    while (n < line.size() && isSpace(line[n])) ++n;
    return line.substr(0, n);
}

// The longest run of spaces and tabs every non-blank line starts with.
std::string commonIndent(const std::vector<std::string_view>& lines) {
    std::optional<std::string_view> common;
    for (auto line : lines) {
        if (blank(line)) continue;
        const auto lead = leadingSpace(line);
        if (!common) {
            common = lead;
            continue;
        }
        size_t n = 0;
        while (n < common->size() && n < lead.size() && (*common)[n] == lead[n]) ++n;
        common = common->substr(0, n);
    }
    return common ? std::string(*common) : std::string();
}

bool identifierStart(char c) { return isUpper(c) || isLower(c) || c == '_'; }
bool identifierChar(char c) { return identifierStart(c) || (c >= '0' && c <= '9'); }

void applyLine(Header& header, std::string_view line) {
    line = trim(line);
    if (line.empty() || line.front() == '#') return;
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) return;
    const std::string key = lower(trim(line.substr(0, colon)));
    const std::string value(trim(line.substr(colon + 1)));
    if (key == "label") header.label = value;
    else if (key == "order") {
        try {
            header.order = std::stoi(value);
        } catch (const std::exception&) {
        }
    } else if (key == "section") header.section = value;
    else if (key == "description") header.description = value;
    else if (key == "file") header.file = value;
    else if (key == "whole-file") {
        const std::string v = lower(value);
        header.wholeFile = v == "yes" || v == "true" || v == "1" || v == "on";
    } else if (key == "ask") {
        const size_t eq = value.find('=');
        const std::string name(trim(std::string_view(value).substr(0, eq)));
        const std::string def = eq == std::string::npos ? std::string() : std::string(trim(std::string_view(value).substr(eq + 1)));
        if (!name.empty() && identifierStart(name[0]) && std::all_of(name.begin(), name.end(), identifierChar))
            header.asks.emplace_back(name, def);
    } else if (key == "sfml") {
        try {
            header.sfml = std::stoi(value);
        } catch (const std::exception&) {
        }
    } else if (key == "includes") {
        // <a> <b> or "a.h": brackets and quotes are kept, separated by spaces or commas.
        std::string word;
        for (char c : value + " ") {
            if ((isSpace(c) || c == ',') && !(word.size() > 1 && word[0] == '"' && word.back() != '"')) {
                if (!word.empty()) header.includes.push_back(word);
                word.clear();
            } else {
                word += c;
            }
        }
    } else if (key == "packages") {
        std::string word;
        for (char c : value + " ") {
            if (isSpace(c) || c == ',') {
                if (!word.empty()) header.packages.push_back(word);
                word.clear();
            } else {
                word += c;
            }
        }
    }
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool ignored(const std::string& name) {
    return name.empty() || name[0] == '_' || name[0] == '.' || name.back() == '~';
}

Entry* child(Entry& folder, const std::string& name, bool isFolder) {
    for (auto& c : folder.children)
        if (c.name == name && c.folder == isFolder) return &c;
    return nullptr;
}

void mergeDir(Entry& folder, const fs::path& dir, const std::string& keyPrefix, int depth) {
    if (depth > 16) return; // a symlink loop
    std::error_code ec;
    std::vector<fs::directory_entry> items;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) items.push_back(*it);
    for (const auto& item : items) {
        const std::string name = item.path().filename().string();
        if (ignored(name)) continue;
        if (item.is_directory(ec)) {
            Entry* sub = child(folder, name, true);
            if (!sub) {
                Entry e;
                e.name = e.label = name;
                e.folder = true;
                folder.children.push_back(std::move(e));
                sub = &folder.children.back();
            }
            const fs::path info = item.path() / "_folder.txt";
            if (fs::is_regular_file(info, ec)) {
                const Header h = parseFolderFile(readFile(info));
                sub->label = h.label.empty() ? name : h.label;
                sub->order = h.order;
                sub->section = h.section;
            }
            mergeDir(*sub, item.path(), keyPrefix + name + "/", depth + 1);
        } else if (item.is_regular_file(ec)) {
            // Only the header matters here; snippets are small, but don't read a stray big file whole.
            std::ifstream in(item.path(), std::ios::binary);
            std::string head(8192, '\0');
            in.read(head.data(), std::streamsize(head.size()));
            head.resize(size_t(in.gcount()));
            const Header h = parseSnippet(head).header;
            Entry e;
            e.name = name;
            e.label = h.label.empty() ? item.path().stem().string() : h.label;
            e.order = h.order;
            e.section = h.section;
            e.key = keyPrefix + name;
            e.file = item.path();
            e.sfml = h.sfml;
            if (Entry* old = child(folder, name, false)) *old = std::move(e);
            else folder.children.push_back(std::move(e));
        }
    }
}

void sortAndPrune(Entry& folder) {
    for (auto& c : folder.children)
        if (c.folder) sortAndPrune(c);
    std::erase_if(folder.children, [](const Entry& c) { return c.folder && c.children.empty(); });
    std::stable_sort(folder.children.begin(), folder.children.end(), [](const Entry& a, const Entry& b) {
        if (a.order != b.order) return a.order < b.order;
        const std::string la = lower(a.label), lb = lower(b.label);
        if (la != lb) return la < lb;
        return a.label < b.label;
    });
}

void sign(const Entry& e, std::string& out) {
    out += e.folder ? "F" : "S";
    out += '\x1f' + e.label + '\x1f' + std::to_string(e.order) + '\x1f' + e.section + '\x1f' + e.key + '\x1f' +
           std::to_string(e.sfml);
    if (e.folder) {
        out += '{';
        for (const auto& c : e.children) sign(c, out);
        out += '}';
    }
    out += '\x1e';
}

bool findPath(const Entry& folder, const std::string& key, std::vector<std::string>& path) {
    for (const auto& c : folder.children) {
        path.push_back(c.label);
        if (c.folder ? findPath(c, key, path) : c.key == key) return true;
        path.pop_back();
    }
    return false;
}

enum class Lang { None, C, Cpp, Header, Ts, Js, Svelte, Vue, Html, Css, CMake, Make, Shell, Glsl, Json, Yaml, Markdown, Python, Text };

Lang langOf(const fs::path& file) {
    const std::string name = file.filename().string();
    if (name == "CMakeLists.txt") return Lang::CMake;
    if (name == "Makefile" || name == "makefile" || name == "GNUmakefile") return Lang::Make;
    static const std::map<std::string, Lang> byExtension{
        {".c", Lang::C},          {".h", Lang::Header},    {".cpp", Lang::Cpp},     {".cc", Lang::Cpp},
        {".cxx", Lang::Cpp},      {".c++", Lang::Cpp},     {".hpp", Lang::Cpp},     {".hh", Lang::Cpp},
        {".hxx", Lang::Cpp},      {".h++", Lang::Cpp},     {".ipp", Lang::Cpp},     {".inl", Lang::Cpp},
        {".tpp", Lang::Cpp},      {".ts", Lang::Ts},       {".mts", Lang::Ts},      {".cts", Lang::Ts},
        {".tsx", Lang::Ts},       {".js", Lang::Js},       {".mjs", Lang::Js},      {".cjs", Lang::Js},
        {".jsx", Lang::Js},       {".svelte", Lang::Svelte}, {".vue", Lang::Vue},   {".html", Lang::Html},
        {".htm", Lang::Html},     {".css", Lang::Css},     {".scss", Lang::Css},    {".sass", Lang::Css},
        {".less", Lang::Css},     {".pcss", Lang::Css},    {".cmake", Lang::CMake}, {".mk", Lang::Make},
        {".sh", Lang::Shell},     {".bash", Lang::Shell},  {".glsl", Lang::Glsl},   {".frag", Lang::Glsl},
        {".vert", Lang::Glsl},    {".geom", Lang::Glsl},   {".comp", Lang::Glsl},   {".json", Lang::Json},
        {".jsonc", Lang::Json},   {".json5", Lang::Json},  {".yml", Lang::Yaml},    {".yaml", Lang::Yaml},
        {".md", Lang::Markdown},  {".markdown", Lang::Markdown}, {".py", Lang::Python}, {".txt", Lang::Text},
    };
    const auto it = byExtension.find(lower(file.extension().string()));
    return it == byExtension.end() ? Lang::None : it->second;
}

// Words of a name: "playerShip", "player_ship", "Player Ship" and "HTTPServer" split where they
// change case or have a separator. Bytes outside ASCII count as letters.
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::string current;
    auto flush = [&] {
        if (!current.empty()) out.push_back(current);
        current.clear();
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (static_cast<unsigned char>(c) < 0x80 && !isAsciiAlnum(c)) {
            flush();
            continue;
        }
        if (!current.empty() && isUpper(c)) {
            const char prev = current.back();
            const bool next = i + 1 < s.size() && isLower(s[i + 1]);
            if (isLower(prev) || (isUpper(prev) && next)) flush();
        }
        current += c;
    }
    flush();
    return out;
}

std::string capitalized(std::string_view word) {
    std::string out = lower(word);
    if (!out.empty()) out[0] = toUpper(out[0]);
    return out;
}

// One {{…}} at `at`: its name and modifiers, and where it ends.
struct Placeholder {
    std::string name;
    std::vector<std::string> modifiers;
    size_t end = 0;
};

std::optional<Placeholder> placeholderAt(std::string_view text, size_t at) {
    if (text.substr(at, 2) != "{{") return std::nullopt;
    size_t i = at + 2;
    if (i >= text.size() || !identifierStart(text[i])) return std::nullopt;
    Placeholder p;
    while (i < text.size() && identifierChar(text[i])) p.name += text[i++];
    while (i < text.size() && text[i] == ':') {
        ++i;
        std::string modifier;
        while (i < text.size() && isLower(text[i])) modifier += text[i++];
        if (modifier.empty()) return std::nullopt;
        p.modifiers.push_back(modifier);
    }
    if (text.substr(i, 2) != "}}") return std::nullopt;
    p.end = i + 2;
    return p;
}

constexpr char kCursorMark = '\x01';

// Fills the placeholders. With `selection` set (insertion), {{cursor}} becomes kCursorMark (the first
// one; the rest go) and {{selection}} the selection, its later lines indented like the line the
// placeholder is on.
std::string fill(std::string_view text, const Values& values, const std::string* selection) {
    std::string out;
    bool cursorPlaced = false;
    for (size_t i = 0; i < text.size();) {
        const auto p = text[i] == '{' ? placeholderAt(text, i) : std::nullopt;
        if (!p) {
            out += text[i++];
            continue;
        }
        if (selection && p->name == "cursor") {
            if (!cursorPlaced) out += kCursorMark;
            cursorPlaced = true;
        } else if (selection && p->name == "selection") {
            const size_t lineStart = out.rfind('\n') == std::string::npos ? 0 : out.rfind('\n') + 1;
            const std::string indent(leadingSpace(std::string_view(out).substr(lineStart)));
            const auto lines = splitLines(*selection);
            for (size_t k = 0; k < lines.size(); ++k) {
                if (k) out += '\n';
                if (k && !blank(lines[k])) out += indent;
                if (!k || !blank(lines[k])) out += lines[k];
            }
        } else if (auto v = values.find(p->name); v != values.end()) {
            std::optional<std::string> value = v->second;
            for (const auto& m : p->modifiers)
                if (value) value = applyCase(*value, m);
            if (!value) out += text.substr(i, p->end - i); // an unknown modifier: left for the author to see
            else out += *value;
        } else {
            out += text.substr(i, p->end - i);
        }
        i = p->end;
    }
    return out;
}

std::string normalizeNewlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) out += text[i];
    return out;
}

} // namespace

Snippet parseSnippet(std::string_view text) {
    Snippet s;
    const std::string normalized = normalizeNewlines(text);
    std::string_view rest = normalized;
    if (rest.substr(0, 3) == "---") {
        const size_t firstEnd = rest.find('\n');
        if (firstEnd != std::string_view::npos && trim(rest.substr(3, firstEnd - 3)).empty()) {
            // The header ends at the next line that is only "---"; without one, there's no header.
            size_t at = firstEnd + 1;
            Header header;
            while (at <= rest.size()) {
                const size_t nl = rest.find('\n', at);
                const std::string_view line = rest.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
                if (trim(line) == "---") {
                    s.header = std::move(header);
                    s.body = nl == std::string_view::npos ? std::string() : std::string(rest.substr(nl + 1));
                    return s;
                }
                applyLine(header, line);
                if (nl == std::string_view::npos) break;
                at = nl + 1;
            }
        }
    }
    s.body = normalized;
    return s;
}

Header parseFolderFile(std::string_view text) {
    Header h;
    const std::string normalized = normalizeNewlines(text);
    for (auto line : splitLines(normalized))
        if (trim(line) != "---") applyLine(h, line);
    return h;
}

Entry scan(const std::vector<fs::path>& roots) {
    Entry root;
    root.folder = true;
    for (const auto& dir : roots) mergeDir(root, dir, "", 0);
    sortAndPrune(root);
    return root;
}

Entry forSfml(const Entry& root, int installedMajor) {
    Entry out = root;
    out.children.clear();
    for (const auto& c : root.children) {
        if (c.folder) {
            Entry sub = forSfml(c, installedMajor);
            if (!sub.children.empty()) out.children.push_back(std::move(sub));
        } else if (!c.sfml || c.sfml == installedMajor) {
            out.children.push_back(c);
        }
    }
    return out;
}

std::string signature(const Entry& root) {
    std::string out;
    sign(root, out);
    return out;
}

std::vector<std::string> labelPath(const Entry& root, const std::string& key) {
    std::vector<std::string> path;
    if (!findPath(root, key, path)) path.clear();
    return path;
}

std::optional<fs::path> locate(const std::vector<fs::path>& roots, const std::string& key) {
    const fs::path rel = fs::path(key).lexically_normal();
    if (key.empty() || rel.is_absolute() || rel.empty() || *rel.begin() == "..") return std::nullopt;
    std::error_code ec;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        if (fs::is_regular_file(*it / rel, ec)) return *it / rel;
    return std::nullopt;
}

std::string menuLabel(std::string_view label) {
    std::string out;
    for (char c : label) {
        if (c == '_') out += '_';
        out += c;
    }
    return out;
}

bool fits(const fs::path& snippetFile, const fs::path& target) {
    const Lang s = langOf(snippetFile), t = langOf(target);
    auto in = [t](std::initializer_list<Lang> langs) { return std::find(langs.begin(), langs.end(), t) != langs.end(); };
    switch (s) {
    case Lang::Text: return true;
    case Lang::C: return in({Lang::C, Lang::Header});
    case Lang::Cpp: return in({Lang::Cpp, Lang::Header});
    case Lang::Header: return in({Lang::C, Lang::Cpp, Lang::Header});
    case Lang::Ts: return in({Lang::Ts, Lang::Svelte, Lang::Vue});
    case Lang::Js: return in({Lang::Js, Lang::Ts, Lang::Svelte, Lang::Vue, Lang::Html});
    case Lang::Css: return in({Lang::Css, Lang::Svelte, Lang::Vue, Lang::Html});
    case Lang::Html: return in({Lang::Html, Lang::Svelte, Lang::Vue});
    case Lang::None: {
        // Files known by name: a "C and C++.gitignore" snippet goes into .gitignore.
        const std::string ext = lower(snippetFile.extension().string());
        return !ext.empty() && (ext == lower(target.extension().string()) || ext == lower(target.filename().string()));
    }
    default: return s == t;
    }
}

std::string promptFor(std::string_view name) {
    std::string out;
    for (const auto& w : words(name)) {
        const bool acronym = w.size() > 1 && std::none_of(w.begin(), w.end(), isLower);
        if (out.empty()) out = acronym ? w : capitalized(w);
        else out += " " + (acronym ? w : lower(w));
    }
    return out.empty() ? std::string(name) : out;
}

std::optional<std::string> applyCase(std::string_view value, std::string_view modifier) {
    if (modifier == "upper") {
        std::string out(value);
        for (char& c : out) c = toUpper(c);
        return out;
    }
    if (modifier == "lower") return lower(value);
    const auto w = words(value);
    auto join = [&](char separator) {
        std::string out;
        for (const auto& word : w) out += (out.empty() ? "" : std::string(1, separator)) + lower(word);
        return out;
    };
    if (modifier == "snake") return join('_');
    if (modifier == "kebab") return join('-');
    if (modifier == "camel" || modifier == "pascal") {
        std::string out;
        for (const auto& word : w) out += out.empty() && modifier == "camel" ? lower(word) : capitalized(word);
        return out;
    }
    return std::nullopt;
}

std::string substitute(std::string_view text, const Values& values) {
    return fill(text, values, nullptr);
}

Insertion expand(std::string_view body, std::string_view linePrefix, std::string_view selection,
                 std::string_view restOfLine, const Values& values, bool keepFinalNewline) {
    std::string text = normalizeNewlines(body);
    if (!keepFinalNewline && !text.empty() && text.back() == '\n') text.pop_back();

    Insertion result;
    result.fromLineStart = blank(linePrefix);
    std::string sel(selection), base;
    bool selectionEndsLine = false;
    if (result.fromLineStart) {
        if (!sel.empty()) {
            // Whole lines are what's usually selected to wrap: their own indentation is the base, and
            // they come back one level in, wherever {{selection}} sits.
            std::string whole = std::string(linePrefix) + sel;
            if (whole.back() == '\n') {
                selectionEndsLine = true;
                whole.pop_back();
            }
            const auto lines = splitLines(whole);
            base = commonIndent(lines);
            sel.clear();
            for (size_t k = 0; k < lines.size(); ++k) {
                if (k) sel += '\n';
                if (!blank(lines[k])) sel += lines[k].substr(std::min(base.size(), lines[k].size()));
            }
        } else {
            base = linePrefix;
        }
    } else {
        base = leadingSpace(linePrefix);
        // A selection starting mid-line keeps its later lines where they were: the base indentation
        // every line gets below is taken off them first.
        const auto lines = splitLines(sel);
        std::string kept;
        for (size_t k = 0; k < lines.size(); ++k) {
            if (k) kept += '\n';
            kept += k && lines[k].substr(0, base.size()) == base ? lines[k].substr(base.size()) : lines[k];
        }
        sel = kept;
    }

    const std::string filled = fill(text, values, &sel);
    const auto lines = splitLines(filled);
    for (size_t k = 0; k < lines.size(); ++k) {
        if (k) result.text += '\n';
        const bool firstMidLine = k == 0 && !result.fromLineStart;
        if (!firstMidLine && !blank(lines[k])) result.text += base;
        if (firstMidLine || !blank(lines[k]) || lines[k].find(kCursorMark) != std::string_view::npos)
            result.text += lines[k];
    }
    if (selectionEndsLine && (result.text.empty() || result.text.back() != '\n')) result.text += '\n';

    if (const size_t mark = result.text.find(kCursorMark); mark != std::string::npos) {
        result.text.erase(mark, 1);
        result.cursor = mark;
    }
    if (result.fromLineStart && !selectionEndsLine && !blank(restOfLine) &&
        (result.text.empty() || result.text.back() != '\n')) {
        if (result.cursor == std::string::npos) result.cursor = result.text.size(); // after the snippet, not the break
        result.text += '\n' + base;
    }
    return result;
}

std::optional<IncludeInsertion> missingIncludes(std::string_view text, const std::vector<std::string>& includes) {
    const auto lines = splitLines(text);
    auto isInclude = [](std::string_view line) {
        const size_t at = line.find("#include");
        return at != std::string_view::npos && blank(line.substr(0, at));
    };
    std::string missing;
    for (const auto& include : includes) {
        const bool there = std::any_of(lines.begin(), lines.end(), [&](std::string_view line) {
            return isInclude(line) && line.find(include, line.find("#include")) != std::string_view::npos;
        });
        if (!there && missing.find("#include " + include + "\n") == std::string::npos) missing += "#include " + include + "\n";
    }
    if (missing.empty()) return std::nullopt;

    auto lineStart = [&](size_t i) { return size_t(lines[i].data() - text.data()); };
    const size_t last = lines.size() - 1;
    std::optional<size_t> lastInclude;
    for (size_t i = 0; i < lines.size(); ++i)
        if (isInclude(lines[i])) lastInclude = i;
    if (lastInclude && *lastInclude < last) return IncludeInsertion{lineStart(*lastInclude + 1), missing};
    if (lastInclude) { // the file ends in an #include without a newline
        missing.pop_back();
        return IncludeInsertion{text.size(), "\n" + missing};
    }
    // None yet: at the top, below a leading comment and #pragma once, a blank line after them.
    size_t at = 0;
    while (at < last && (trim(lines[at]).starts_with("//") || trim(lines[at]).starts_with("#pragma"))) ++at;
    return IncludeInsertion{lineStart(at), missing + (blank(lines[at]) ? "" : "\n")};
}

std::string indentForBlankLine(std::string_view textAbove) {
    const auto lines = splitLines(textAbove);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (blank(*it)) continue;
        std::string indent(leadingSpace(*it));
        const std::string_view code = trim(*it);
        const bool opens = code.back() == '{' || (code.back() == ':' && code.find("::") == std::string_view::npos &&
                                                   code.find('?') == std::string_view::npos);
        if (opens) indent += indent.find('\t') != std::string::npos ? "\t" : "    ";
        return indent;
    }
    return {};
}

std::string dedent(std::string_view text) {
    const std::string normalized = normalizeNewlines(text);
    auto lines = splitLines(normalized);
    while (!lines.empty() && blank(lines.back())) lines.pop_back();
    while (!lines.empty() && blank(lines.front())) lines.erase(lines.begin());
    const std::string indent = commonIndent(lines);
    std::string out;
    for (auto line : lines) {
        if (!blank(line)) out += line.substr(std::min(indent.size(), line.size()));
        out += '\n';
    }
    return out;
}

std::optional<fs::path> savePath(const fs::path& root, std::string_view menuPath, const std::string& extension,
                                 std::string& error) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t slash = menuPath.find('/', start);
        parts.emplace_back(trim(menuPath.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start)));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    for (const auto& p : parts) {
        if (p.empty()) {
            error = "Give every folder and the item a name, separated by /.";
            return std::nullopt;
        }
        if (p == "." || p == ".." || p[0] == '_' || p[0] == '.' || p.back() == '~') {
            error = "\"" + p + "\" can't be a name in the menu: names starting with _ or . are left out of it.";
            return std::nullopt;
        }
    }
    std::string last = parts.back();
    const std::string ext = extension.empty() ? ".txt" : extension;
    if (last.size() <= ext.size() || last.compare(last.size() - ext.size(), ext.size(), ext) != 0) last += ext;
    fs::path out = root;
    for (size_t i = 0; i + 1 < parts.size(); ++i) out /= parts[i];
    return out / last;
}

std::string snippetExtension(const fs::path& file) {
    const std::string name = file.filename().string();
    if (name == "CMakeLists.txt") return ".cmake";
    if (name == "Makefile" || name == "makefile" || name == "GNUmakefile") return ".mk";
    if (file.extension().empty() && !name.empty() && name[0] == '.') return name;
    return file.extension().string();
}

std::string suggestedFolder(const fs::path& file) {
    switch (langOf(file)) {
    case Lang::C:
    case Lang::Cpp:
    case Lang::Header:
    case Lang::CMake:
    case Lang::Make: return "C and C++/My Boilerplate";
    case Lang::Svelte: return "Svelte and SvelteKit/My Boilerplate";
    case Lang::Ts:
    case Lang::Js: return "TypeScript/My Boilerplate";
    case Lang::Css: return "CSS/My Boilerplate";
    case Lang::Html: return "HTML/My Boilerplate";
    case Lang::Python: return "Python/My Boilerplate";
    case Lang::Shell: return "Shell/My Boilerplate";
    case Lang::Markdown: return "Markdown/My Boilerplate";
    default: return "My Boilerplate";
    }
}

} // namespace kevc::boilerplate
