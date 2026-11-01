#include "core/CppStyle.h"

#include <algorithm>
#include <cctype>

namespace kevc::cppstyle {

namespace {

bool isIdent(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string_view rtrimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// Is `word` in `text` as a whole word?
bool hasWord(std::string_view text, std::string_view word) {
    for (size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
        const bool startOk = at == 0 || !isIdent(text[at - 1]);
        const bool endOk = at + word.size() >= text.size() || !isIdent(text[at + word.size()]);
        if (startOk && endOk) return true;
    }
    return false;
}

bool endsWithWord(std::string_view text, std::string_view word) {
    return text.size() >= word.size() && text.substr(text.size() - word.size()) == word &&
           (text.size() == word.size() || !isIdent(text[text.size() - word.size() - 1]));
}

// The line before the end of `text` that has something on it.
std::string_view lastNonBlankLine(std::string_view text) {
    while (!text.empty()) {
        if (text.back() == '\n') text.remove_suffix(1);
        const size_t nl = text.rfind('\n');
        const std::string_view line = nl == std::string_view::npos ? text : text.substr(nl + 1);
        if (!trimmed(line).empty()) return line;
        text = nl == std::string_view::npos ? std::string_view() : text.substr(0, nl + 1);
        if (nl == std::string_view::npos) break;
    }
    return {};
}

} // namespace

std::string indentOf(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return std::string(line.substr(0, i));
}

std::vector<Open> openBrackets(std::string_view code) {
    std::vector<Open> stack;
    size_t lineStart = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        const char next = i + 1 < code.size() ? code[i + 1] : '\0';
        if (c == '\n') {
            lineStart = i + 1;
        } else if (c == '/' && next == '/') {
            while (i < code.size() && code[i] != '\n') ++i;
            --i; // let the newline be seen
        } else if (c == '/' && next == '*') {
            const size_t end = code.find("*/", i + 2);
            if (end == std::string_view::npos) break; // still inside a comment
            for (size_t k = i; k < end; ++k)
                if (code[k] == '\n') lineStart = k + 1;
            i = end + 1;
        } else if (c == 'R' && next == '"' && (i == 0 || !isIdent(code[i - 1]))) {
            // R"delim( … )delim"
            const size_t open = code.find('(', i + 2);
            if (open == std::string_view::npos) break;
            const std::string close = ")" + std::string(code.substr(i + 2, open - i - 2)) + "\"";
            const size_t end = code.find(close, open);
            if (end == std::string_view::npos) break;
            for (size_t k = i; k < end; ++k)
                if (code[k] == '\n') lineStart = k + 1;
            i = end + close.size() - 1;
        } else if (c == '"' || (c == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(code[i - 1])) &&
                                                std::isxdigit(static_cast<unsigned char>(next))))) {
            // A string or character literal ('\'' between hex digits is a digit separator: 1'000).
            for (++i; i < code.size() && code[i] != c && code[i] != '\n'; ++i)
                if (code[i] == '\\') ++i;
            if (i < code.size() && code[i] == '\n') lineStart = i + 1;
        } else if (c == '{' || c == '(' || c == '[') {
            stack.push_back({c, i, indentOf(code.substr(lineStart))});
        } else if (c == '}' || c == ')' || c == ']') {
            const char want = c == '}' ? '{' : c == ')' ? '(' : '[';
            // Unbalanced code while typing: drop to the matching kind if there is one.
            for (size_t k = stack.size(); k-- > 0;)
                if (stack[k].bracket == want) {
                    stack.resize(k);
                    break;
                }
        }
    }
    return stack;
}

std::string newLineIndent(std::string_view before, std::string_view after, const std::string& unit) {
    const auto stack = openBrackets(before);
    const std::string_view rest = trimmed(after);
    if (stack.empty()) return {};
    const Open& top = stack.back();
    const bool closes = !rest.empty() && (rest.front() == '}' || rest.front() == ')' || rest.front() == ']');
    if (closes) return top.lineIndent;
    if (top.bracket == '{') {
        // After `case 1:` or `default:` the statements go one level in from the label.
        const std::string_view prev = trimmed(lastNonBlankLine(before));
        if ((prev.starts_with("case ") || prev.starts_with("default")) && prev.ends_with(":"))
            return indentOf(lastNonBlankLine(before)) + unit;
    }
    return top.lineIndent + unit;
}

std::optional<std::string> accessSpecifierIndent(std::string_view before, std::string_view line) {
    std::string_view t = trimmed(line);
    if (t.empty() || t.back() != ':' || (t.size() > 1 && t[t.size() - 2] == ':')) return std::nullopt;
    t.remove_suffix(1);
    t = rtrimmed(t);
    static constexpr std::string_view kSpecifiers[] = {"public",       "private",       "protected",
                                                       "public slots", "private slots", "protected slots",
                                                       "signals",      "Q_SIGNALS",     "public Q_SLOTS",
                                                       "private Q_SLOTS", "protected Q_SLOTS"};
    if (std::find(std::begin(kSpecifiers), std::end(kSpecifiers), t) == std::end(kSpecifiers)) return std::nullopt;
    const auto stack = openBrackets(before);
    for (size_t k = stack.size(); k-- > 0;)
        if (stack[k].bracket == '{') return stack[k].lineIndent;
    return std::string();
}

std::optional<std::string> braceOnOwnLine(std::string_view line) {
    if (line.empty() || line.back() != '{') return std::nullopt;
    std::string_view code = rtrimmed(line.substr(0, line.size() - 1));
    const std::string_view t = trimmed(code);
    if (t.empty()) return std::nullopt;
    // Inside a comment or a string: leave it.
    if (code.find("//") != std::string_view::npos || code.find("/*") != std::string_view::npos) return std::nullopt;
    if (std::count(code.begin(), code.end(), '"') % 2 != 0) return std::nullopt;
    // A lambda ([…](…) {) or an initialiser (= {, return {, f({, a, {) keeps its brace.
    for (size_t i = code.find(']'); i != std::string_view::npos; i = code.find(']', i + 1)) {
        size_t j = i + 1;
        while (j < code.size() && code[j] == ' ') ++j;
        if (j >= code.size() || code[j] == '(') return std::nullopt;
    }
    if (t.find('=') != std::string_view::npos && !hasWord(t, "operator")) return std::nullopt;
    if (hasWord(t, "return")) return std::nullopt;

    // What's left of the header after qualifiers that may follow a function's parameters.
    std::string_view head = t;
    for (bool again = true; again;) {
        again = false;
        for (std::string_view q : {"const", "override", "final", "noexcept", "volatile", "&&", "&"})
            if (head.size() > q.size() && head.ends_with(q) && (q[0] == '&' || endsWithWord(head, q))) {
                head = rtrimmed(head.substr(0, head.size() - q.size()));
                again = true;
            }
    }
    bool move = false;
    if (!head.empty() && head.back() == ')') move = true; // a function or if/for/while/switch/catch
    else if (endsWithWord(head, "else") || endsWithWord(head, "do") || endsWithWord(head, "try")) move = true;
    else if ((hasWord(t, "class") || hasWord(t, "struct") || hasWord(t, "union") || hasWord(t, "enum") ||
              hasWord(t, "namespace")) &&
             (isIdent(t.back()) || t.back() == '>'))
        move = true;
    if (!move) return std::nullopt;
    return std::string(code) + "\n" + indentOf(line) + "{";
}

Closing closingBrace(std::string_view before, std::string_view lineBefore) {
    Closing out;
    const auto stack = openBrackets(before);
    size_t k = stack.size();
    while (k > 0 && stack[k - 1].bracket != '{') --k;
    if (k == 0) return out;
    const Open& open = stack[k - 1];
    if (trimmed(lineBefore).empty()) out.indent = open.lineIndent;
    // The brace's header: back from it to the end of the statement before (;, { or }).
    size_t start = open.pos;
    while (start > 0 && before[start - 1] != ';' && before[start - 1] != '{' && before[start - 1] != '}' &&
           open.pos - start < 400)
        --start;
    std::string_view header = trimmed(before.substr(start, open.pos - start));
    // Without comments and preprocessor lines (an #include just before a class, say).
    std::string clean;
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == '/' && i + 1 < header.size() && header[i + 1] == '/') {
            while (i < header.size() && header[i] != '\n') ++i;
            continue;
        }
        if (header[i] == '#' && (i == 0 || header[i - 1] == '\n')) {
            while (i < header.size() && header[i] != '\n') ++i;
            continue;
        }
        clean += header[i];
    }
    const std::string_view h = trimmed(clean);
    const bool type = hasWord(h, "class") || hasWord(h, "struct") || hasWord(h, "union") || hasWord(h, "enum");
    // Not a function returning one (struct Foo make()), an initialiser, or template<class T> void f().
    out.semicolon = type && !h.empty() && h.back() != ')' && h.find('=') == std::string_view::npos &&
                    !hasWord(h, "namespace");
    return out;
}

} // namespace kevc::cppstyle
