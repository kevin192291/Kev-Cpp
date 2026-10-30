#include "core/Mi.h"

#include <cctype>
#include <stdexcept>

namespace kevc::mi {

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    bool atEnd() const { return i_ >= s_.size(); }
    char peek() const { return atEnd() ? '\0' : s_[i_]; }
    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string("expected '") + c + "'");
        ++i_;
    }

    std::string cstring() {
        expect('"');
        std::string out;
        while (!atEnd() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\' || atEnd()) {
                out += c;
                continue;
            }
            c = s_[i_++];
            switch (c) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'e': out += '\x1b'; break;
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                // Octal escape: up to three digits (gdb uses these for non-ASCII bytes).
                int value = c - '0';
                for (int k = 0; k < 2 && !atEnd() && s_[i_] >= '0' && s_[i_] <= '7'; ++k) value = value * 8 + (s_[i_++] - '0');
                out += static_cast<char>(value);
                break;
            }
            default: out += c; // \" \\ and anything else
            }
        }
        expect('"');
        return out;
    }

    std::string variable() {
        const size_t start = i_;
        while (!atEnd() && s_[i_] != '=' && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']') ++i_;
        return std::string(s_.substr(start, i_ - start));
    }

    json value() {
        switch (peek()) {
        case '"': return cstring();
        case '{': return tuple();
        case '[': return list();
        default: throw std::runtime_error("expected a value");
        }
    }

    // name=value pairs until `end` (or end of input when end == 0).
    json results(char end) {
        json out = json::object();
        while (!atEnd() && peek() != end) {
            std::string name = variable();
            expect('=');
            out[name] = value();
            if (peek() == ',') ++i_;
        }
        return out;
    }

    json tuple() {
        expect('{');
        json out = results('}');
        expect('}');
        return out;
    }

    // [value, value] or [name=value, name=value]; names in lists repeat (frame=, frame=), so a
    // list of results becomes an array of the values.
    json list() {
        expect('[');
        json out = json::array();
        while (!atEnd() && peek() != ']') {
            if (peek() == '"' || peek() == '{' || peek() == '[') {
                out.push_back(value());
            } else {
                variable();
                expect('=');
                out.push_back(value());
            }
            if (peek() == ',') ++i_;
        }
        expect(']');
        return out;
    }

    size_t pos() const { return i_; }
    void skip(size_t n) { i_ += n; }

private:
    std::string_view s_;
    size_t i_ = 0;
};

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // namespace

Record parseLine(std::string_view line) {
    Record r;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (line == "(gdb)") {
        r.type = Record::Type::Prompt;
        return r;
    }
    try {
        size_t i = 0;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
        if (i > 0) r.token = std::stoi(std::string(line.substr(0, i)));
        if (i >= line.size()) throw std::runtime_error("empty record");

        const char kind = line[i];
        std::string_view rest = line.substr(i + 1);
        switch (kind) {
        case '~': case '@': case '&': {
            r.type = kind == '~' ? Record::Type::Console : kind == '@' ? Record::Type::Target : Record::Type::Log;
            Parser p(rest);
            r.text = p.cstring();
            return r;
        }
        case '^': r.type = Record::Type::Result; break;
        case '*': r.type = Record::Type::Exec; break;
        case '+': r.type = Record::Type::Status; break;
        case '=': r.type = Record::Type::Notify; break;
        default: throw std::runtime_error("unknown record kind");
        }
        const auto comma = rest.find(',');
        r.resultClass = std::string(rest.substr(0, comma));
        if (comma != std::string_view::npos) {
            Parser p(rest.substr(comma + 1));
            r.results = p.results('\0');
        }
    } catch (const std::exception&) {
        Record unknown;
        unknown.type = Record::Type::Unknown;
        unknown.text = std::string(line);
        return unknown;
    }
    return r;
}

std::string quote(std::string_view text) {
    std::string out = "\"";
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}

bool isDataPointer(std::string_view type) {
    while (!type.empty() && type.back() == ' ') type.remove_suffix(1);
    // "int * const" -> "int *"
    if (type.ends_with("const")) {
        type.remove_suffix(5);
        while (!type.empty() && type.back() == ' ') type.remove_suffix(1);
    }
    if (!type.ends_with("*")) return false;
    if (type.find('(') != std::string_view::npos) return false; // function pointer
    std::string_view base = type.substr(0, type.size() - 1);
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    if (base.ends_with("*")) return true; // pointer to pointer: still worth following
    // char*, const char*, unsigned char*: gdb already shows the string
    for (std::string_view c : {"char", "const char", "unsigned char", "signed char", "const unsigned char",
                               "char16_t", "char32_t", "wchar_t", "const wchar_t", "void", "const void"})
        if (base == c) return false;
    return true;
}

bool isNullPointer(std::string_view value) {
    if (!value.starts_with("0x")) return false;
    value.remove_prefix(2);
    // gdb may append symbol info: "0x0 <foo>" — only the number matters.
    const auto end = value.find(' ');
    value = value.substr(0, end);
    return !value.empty() && value.find_first_not_of('0') == std::string_view::npos;
}

std::string expressionAt(std::string_view line, int column) {
    if (column < 0 || static_cast<size_t>(column) >= line.size()) {
        // Allow hovering just after the last character of an identifier.
        if (column > 0 && static_cast<size_t>(column) == line.size() && isIdentChar(line[column - 1])) --column;
        else return {};
    }
    size_t pos = static_cast<size_t>(column);
    if (!isIdentChar(line[pos])) {
        if (pos > 0 && isIdentChar(line[pos - 1])) --pos;
        else return {};
    }
    // End of the identifier under the cursor (we don't extend further right).
    size_t end = pos;
    while (end < line.size() && isIdentChar(line[end])) ++end;
    size_t start = pos;
    while (start > 0 && isIdentChar(line[start - 1])) --start;
    if (std::isdigit(static_cast<unsigned char>(line[start]))) return {}; // a number literal

    // Walk left through ".", "->", "::" and "[...]" segments.
    for (;;) {
        size_t s = start;
        while (s > 0 && line[s - 1] == ' ') --s;
        size_t op = 0;
        if (s >= 1 && line[s - 1] == '.') op = 1;
        else if (s >= 2 && line.substr(s - 2, 2) == "->") op = 2;
        else if (s >= 2 && line.substr(s - 2, 2) == "::") op = 2;
        if (op == 0) break;
        size_t t = s - op;
        while (t > 0 && line[t - 1] == ' ') --t;
        // Skip any subscripts: a[i].x, m[k][j].y
        while (t > 0 && line[t - 1] == ']') {
            int depth = 0;
            size_t u = t;
            while (u > 0) {
                const char c = line[--u];
                if (c == ']') ++depth;
                else if (c == '[' && --depth == 0) break;
            }
            if (depth != 0) return std::string(line.substr(start, end - start));
            t = u;
        }
        if (t == 0 || !isIdentChar(line[t - 1])) break; // e.g. f().x: stop at the call
        size_t u = t;
        while (u > 0 && isIdentChar(line[u - 1])) --u;
        start = u;
    }

    // A unary * or & directly in front: *ptr, &value (not a multiplication like a * b).
    if (start > 0 && (line[start - 1] == '*' || line[start - 1] == '&')) {
        size_t p = start - 1;
        size_t q = p;
        while (q > 0 && line[q - 1] == ' ') --q;
        bool binary = q > 0 && (isIdentChar(line[q - 1]) || line[q - 1] == ')' || line[q - 1] == ']');
        if (binary && isIdentChar(line[q - 1])) {
            // `return *p`, `case *p`: a keyword before the operator makes it unary.
            size_t w = q;
            while (w > 0 && isIdentChar(line[w - 1])) --w;
            const std::string_view word = line.substr(w, q - w);
            for (std::string_view kw : {"return", "case", "throw", "sizeof", "co_return", "co_yield", "delete", "else", "do"})
                if (word == kw) binary = false;
        }
        if (!binary && !(p > 0 && line[p - 1] == line[p])) start = p; // skip && and **
    }
    std::string out;
    for (size_t k = start; k < end; ++k)
        if (line[k] != ' ') out += line[k];
    return out;
}

} // namespace kevc::mi
