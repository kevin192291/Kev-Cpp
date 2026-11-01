#include "core/Breakpoints.h"

#include <algorithm>
#include <cctype>

namespace kevc {

namespace {

constexpr std::string_view kLivePrefix = "<<kevc-live:";
constexpr std::string_view kSeparator = "<<kevc>>";

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::vector<std::string> splitOn(std::string_view text, std::string_view separator) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t at = text.find(separator); at != std::string_view::npos; at = text.find(separator, start)) {
        out.emplace_back(text.substr(start, at - start));
        start = at + separator.size();
    }
    out.emplace_back(text.substr(start));
    return out;
}

bool isIdentifier(std::string_view s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$') return false;
    return true;
}

// The end of the bracket or quote that opens at `i` (exclusive), or npos.
size_t closingOf(std::string_view s, size_t i) {
    const char open = s[i];
    if (open == '"' || open == '\'' || open == '`') {
        for (++i; i < s.size(); ++i) {
            if (s[i] == '\\') ++i;
            else if (s[i] == open) return i + 1;
        }
        return std::string_view::npos;
    }
    const char close = open == '(' ? ')' : open == '[' ? ']' : '}';
    int depth = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\'' || c == '`') {
            const size_t end = closingOf(s, i);
            if (end == std::string_view::npos) return end;
            i = end - 1;
        } else if (c == open) {
            ++depth;
        } else if (c == close && --depth == 0) {
            return i + 1;
        }
    }
    return std::string_view::npos;
}

} // namespace

const char* kindName(BreakKind kind) {
    switch (kind) {
    case BreakKind::Standard: return "standard";
    case BreakKind::Conditional: return "if";
    case BreakKind::Live: return "live";
    }
    return "standard";
}

std::vector<std::string> splitExpressions(std::string_view text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size() && (text[i] == '(' || text[i] == '[' || text[i] == '{' || text[i] == '"' ||
                                text[i] == '\'' || text[i] == '`')) {
            const size_t end = closingOf(text, i);
            if (end == std::string_view::npos) i = text.size() - 1; // unbalanced: the rest is one expression
            else i = end - 1;
            continue;
        }
        if (i == text.size() || text[i] == ',') {
            if (std::string e = trim(text.substr(start, i - start)); !e.empty()) out.push_back(std::move(e));
            start = i + 1;
        }
    }
    return out;
}

std::string joinExpressions(const std::vector<std::string>& expressions) {
    std::string out;
    for (const auto& e : expressions) out += (out.empty() ? "" : ", ") + e;
    return out;
}

std::string liveSummary(const std::vector<std::string>& names, const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
        out += (out.empty() ? "" : ", ") + names[i] + " = " + (i < values.size() ? values[i] : std::string("?"));
    return out;
}

std::string liveLogMessage(int id, const std::vector<std::string>& show) {
    std::string out = std::string(kLivePrefix) + std::to_string(id) + ">>";
    for (size_t i = 0; i < show.size(); ++i) out += (i ? std::string(kSeparator) : std::string()) + "{" + show[i] + "}";
    return out;
}

std::optional<LiveLog> parseLiveLog(std::string_view output) {
    // Only output that starts with the marker: gdb also quotes the dprintf's format in its
    // breakpoint notices, which mustn't count as a hit.
    while (!output.empty() && (output.front() == ' ' || output.front() == '\t')) output.remove_prefix(1);
    if (output.substr(0, kLivePrefix.size()) != kLivePrefix) return std::nullopt;
    size_t i = kLivePrefix.size();
    LiveLog log;
    const size_t digits = i;
    while (i < output.size() && std::isdigit(static_cast<unsigned char>(output[i]))) log.id = log.id * 10 + (output[i++] - '0');
    if (i == digits || output.substr(i, 2) != ">>") return std::nullopt;
    std::string_view rest = output.substr(i + 2);
    while (!rest.empty() && (rest.back() == '\n' || rest.back() == '\r')) rest.remove_suffix(1);
    log.values = rest.empty() ? std::vector<std::string>{} : splitOn(rest, kSeparator);
    return log;
}

std::string gdbLivePython() {
    // Run with exec() from hex so no quoting (MI's, gdb's, Python's) can get in the way.
    static const std::string script = R"py(import gdb
def _kevc_text(v):
    try:
        return v.format_string()
    except Exception:
        return str(v)
class _KevcValue(gdb.Function):
    def __init__(self):
        super().__init__('_kevc_value')
    def invoke(self, expr):
        try:
            return _kevc_text(gdb.parse_and_eval(expr.string()))
        except Exception as e:
            return '<' + str(e) + '>'
class _KevcLocals(gdb.Function):
    def __init__(self):
        super().__init__('_kevc_locals')
    def invoke(self):
        out = []
        try:
            frame = gdb.selected_frame()
            block = frame.block()
            seen = set()
            while block is not None:
                for sym in block:
                    if (sym.is_variable or sym.is_argument) and sym.name not in seen:
                        seen.add(sym.name)
                        try:
                            out.append(sym.name + ' = ' + _kevc_text(sym.value(frame)))
                        except Exception:
                            out.append(sym.name + ' = ?')
                if block.function is not None:
                    break
                block = block.superblock
        except Exception as e:
            return '<' + str(e) + '>'
        return '<<kevc>>'.join(out)
_KevcValue()
_KevcLocals()
)py";
    static const char* digits = "0123456789abcdef";
    std::string hex;
    for (unsigned char c : script) {
        hex += digits[c >> 4];
        hex += digits[c & 15];
    }
    return "python exec(bytes.fromhex('" + hex + "').decode())";
}

std::vector<std::string> gdbLiveArguments(int id, const std::vector<std::string>& show) {
    std::string format = std::string(kLivePrefix) + std::to_string(id) + ">>";
    std::vector<std::string> args;
    if (show.empty()) {
        format += "%s";
        args.push_back("$_kevc_locals()");
    }
    for (size_t i = 0; i < show.size(); ++i) {
        format += (i ? std::string(kSeparator) : std::string()) + "%s";
        std::string literal;
        for (char c : show[i]) {
            if (c == '\\' || c == '"') literal += '\\';
            literal += c;
        }
        args.push_back("$_kevc_value(\"" + literal + "\")");
    }
    args.insert(args.begin(), format + "\n");
    return args;
}

std::pair<std::vector<std::string>, std::vector<std::string>> splitNamedValues(const std::vector<std::string>& items) {
    std::vector<std::string> names, values;
    for (const auto& item : items) {
        const size_t at = item.find(" = ");
        names.push_back(at == std::string::npos ? item : item.substr(0, at));
        values.push_back(at == std::string::npos ? std::string() : item.substr(at + 3));
    }
    return {names, values};
}

std::optional<std::pair<std::string, std::string>> splitPropertyPath(std::string_view expression) {
    const std::string e = trim(expression);
    if (e.empty()) return std::nullopt;
    // The last member access at the top level: .name or [key].
    size_t lastDot = std::string::npos, lastBracket = std::string::npos;
    for (size_t i = 0; i < e.size(); ++i) {
        const char c = e[i];
        if (c == '(' || c == '{' || c == '"' || c == '\'' || c == '`') {
            const size_t end = closingOf(e, i);
            if (end == std::string_view::npos) return std::nullopt;
            i = end - 1;
        } else if (c == '[') {
            const size_t end = closingOf(e, i);
            if (end == std::string_view::npos) return std::nullopt;
            if (end == e.size()) lastBracket = i;
            i = end - 1;
        } else if (c == '.') {
            lastDot = i;
        }
    }
    if (lastBracket != std::string::npos && (lastDot == std::string::npos || lastBracket > lastDot)) {
        std::string key = trim(std::string_view(e).substr(lastBracket + 1, e.size() - lastBracket - 2));
        if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') && key.back() == key.front())
            key = key.substr(1, key.size() - 2);
        else if (key.empty() || !std::all_of(key.begin(), key.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            return std::nullopt; // a computed key: which property isn't known until it runs
        std::string object = trim(std::string_view(e).substr(0, lastBracket));
        if (object.empty()) return std::nullopt;
        return std::make_pair(object, key);
    }
    if (lastDot == std::string::npos) return std::nullopt;
    std::string object = trim(std::string_view(e).substr(0, lastDot));
    const std::string property = trim(std::string_view(e).substr(lastDot + 1));
    if (!object.empty() && object.back() == '?') object.pop_back(); // a?.b
    if (object.empty() || !isIdentifier(property)) return std::nullopt;
    return std::make_pair(object, property);
}

std::string jsWatchInstall(int id, const std::string& object, const std::string& property) {
    const std::string key = nlohmann::json(property).dump();
    const std::string n = std::to_string(id);
    return "(() => {"
           " const reg = globalThis.__kevcWatches || (globalThis.__kevcWatches = new Map());"
           " const o = (" + object + ");"
           " if (o === null || (typeof o !== 'object' && typeof o !== 'function')) return 'not an object';"
           " const k = " + key + ";"
           " const d = Object.getOwnPropertyDescriptor(o, k);"
           " if (d && (!d.configurable || d.get || d.set)) return 'it is a fixed property or already a getter/setter';"
           " let v = o[k];"
           " const enumerable = d ? d.enumerable : true;"
           " Object.defineProperty(o, k, { configurable: true, enumerable,"
           "  get() { return v; },"
           "  set(n) { if (n !== v) { const old = v; v = n; globalThis.__kevcChange = { id: " + n + ", old, now: n };"
           " debugger; } } });"
           " reg.set(" + n + ", { o, k, enumerable, get value() { return v; } });"
           " return 'ok'; })()";
}

std::string jsWatchRemove(int id) {
    const std::string n = std::to_string(id);
    return "(() => {"
           " const reg = globalThis.__kevcWatches; const w = reg && reg.get(" + n + "); if (!w) return 'gone';"
           " const value = w.value;"
           " Object.defineProperty(w.o, w.k, { configurable: true, enumerable: w.enumerable, writable: true, value });"
           " reg.delete(" + n + "); return 'ok'; })()";
}

std::string jsWatchLastChange() {
    return "(() => { const c = globalThis.__kevcChange; globalThis.__kevcChange = undefined; if (!c) return '';"
           " const s = (x) => { try { return typeof x === 'string' ? JSON.stringify(x) : String(x); } catch (e) { return '?'; } };"
           " return c.id + '" + std::string(kSeparator) + "' + s(c.old) + '" + std::string(kSeparator) + "' + s(c.now); })()";
}

std::optional<WatchChange> parseWatchChange(std::string_view text) {
    std::string t = trim(text);
    // A debugger shows a string result in quotes: take them off.
    if (t.size() >= 2 && (t.front() == '\'' || t.front() == '"') && t.back() == t.front()) t = t.substr(1, t.size() - 2);
    const auto parts = splitOn(t, kSeparator);
    if (parts.size() != 3 || parts[0].empty() ||
        !std::all_of(parts[0].begin(), parts[0].end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return std::nullopt;
    return WatchChange{std::stoi(parts[0]), parts[1], parts[2]};
}

nlohmann::json toJson(const FileBreakpoints& file) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [line, bp] : file) {
        if (bp.kind == BreakKind::Standard) {
            out.push_back(line);
            continue;
        }
        nlohmann::json j = {{"line", line}, {"kind", kindName(bp.kind)}};
        if (bp.kind == BreakKind::Conditional) j["condition"] = bp.condition;
        else j["show"] = bp.show;
        out.push_back(std::move(j));
    }
    return out;
}

FileBreakpoints fileBreakpointsFromJson(const nlohmann::json& lines) {
    FileBreakpoints out;
    if (!lines.is_array()) return out;
    for (const auto& item : lines) {
        if (item.is_number_integer()) {
            if (item.get<int>() > 0) out[item.get<int>()] = {};
            continue;
        }
        if (!item.is_object() || !item.contains("line") || !item["line"].is_number_integer()) continue;
        const int line = item["line"].get<int>();
        if (line <= 0) continue;
        Breakpoint bp;
        const std::string kind = item.value("kind", "standard");
        if (kind == "if") {
            bp.kind = BreakKind::Conditional;
            bp.condition = item.value("condition", "");
        } else if (kind == "live") {
            bp.kind = BreakKind::Live;
            if (auto show = item.find("show"); show != item.end() && show->is_array())
                for (const auto& s : *show)
                    if (s.is_string()) bp.show.push_back(s.get<std::string>());
        }
        out[line] = std::move(bp);
    }
    return out;
}

} // namespace kevc
