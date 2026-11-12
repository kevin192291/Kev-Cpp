#include "core/QuickFix.h"

#include "core/Boilerplate.h"
#include "core/Lsp.h"
#include "core/Search.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc::quickfix {

namespace {

// --- unknownName -------------------------------------------------------------------------------

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

// The text between the first pair of single quotes at or after `from`, and where it ends.
std::optional<std::pair<std::string, size_t>> quoted(const std::string& text, size_t from) {
    const size_t open = text.find('\'', from);
    if (open == std::string::npos) return std::nullopt;
    const size_t close = text.find('\'', open + 1);
    if (close == std::string::npos) return std::nullopt;
    return std::make_pair(text.substr(open + 1, close - open - 1), close + 1);
}

// "class Foo" → "Foo", "std::vector<int>" → "std::vector": what a header would declare.
std::string bareName(std::string name) {
    for (std::string_view tag : {"class ", "struct ", "union ", "enum "})
        if (name.starts_with(tag)) name.erase(0, tag.size());
    if (const size_t angle = name.find('<'); angle != std::string::npos) name.erase(angle);
    while (!name.empty() && (name.back() == ' ' || name.back() == '*' || name.back() == '&')) name.pop_back();
    const bool identifier = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == ':' || c >= 0x80;
    });
    return identifier ? name : std::string();
}

// --- Tokens --------------------------------------------------------------------------------------

struct Token {
    enum Kind { Ident, Punct, Literal } kind;
    std::string_view text;
};

bool identStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
bool identChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

// C/C++ tokens without comments, preprocessor lines and the insides of literals; the names #defined
// go straight into `macros`.
std::vector<Token> tokenize(std::string_view s, std::set<std::string>& macros) {
    std::vector<Token> out;
    const size_t n = s.size();
    size_t i = 0;
    bool lineStart = true;
    while (i < n) {
        const char c = s[i];
        if (c == '\n') {
            lineStart = true;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }
        if (c == '\\' && i + 1 < n && (s[i + 1] == '\n' || s[i + 1] == '\r')) {
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string_view::npos ? n : end + 2;
            continue;
        }
        if (c == '#' && lineStart) {
            ++i;
            auto spaces = [&] {
                while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
            };
            auto word = [&] {
                const size_t from = i;
                while (i < n && identChar(s[i])) ++i;
                return s.substr(from, i - from);
            };
            spaces();
            if (word() == "define") {
                spaces();
                if (i < n && identStart(s[i])) macros.insert(std::string(word()));
            }
            while (i < n && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < n && s[i + 1] == '\n') i += 2;
                else if (s[i] == '\\' && i + 2 < n && s[i + 1] == '\r' && s[i + 2] == '\n') i += 3;
                else ++i;
            }
            continue;
        }
        lineStart = false;
        if (identStart(c)) {
            const size_t from = i;
            while (i < n && identChar(s[i])) ++i;
            const std::string_view word = s.substr(from, i - from);
            if (i < n && s[i] == '"' &&
                (word == "R" || word == "u8R" || word == "uR" || word == "UR" || word == "LR")) {
                const size_t open = s.find('(', i + 1);
                if (open == std::string_view::npos) break;
                const std::string close = ")" + std::string(s.substr(i + 1, open - i - 1)) + "\"";
                const size_t end = s.find(close, open + 1);
                i = end == std::string_view::npos ? n : end + close.size();
                out.push_back({Token::Literal, "\"\""});
                continue;
            }
            out.push_back({Token::Ident, word});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            const size_t from = i;
            while (i < n) {
                const char d = s[i];
                if (identChar(d) || d == '.' || d == '\'') ++i;
                else if ((d == '+' || d == '-') && std::string_view("eEpP").find(s[i - 1]) != std::string_view::npos)
                    ++i;
                else break;
            }
            out.push_back({Token::Literal, s.substr(from, i - from)});
            continue;
        }
        if (c == '"' || c == '\'') {
            const size_t from = i++;
            while (i < n && s[i] != c && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < n) ++i;
                ++i;
            }
            if (i < n && s[i] == c) ++i;
            out.push_back({Token::Literal, s.substr(from, i - from)});
            continue;
        }
        if (c == ':' && i + 1 < n && s[i + 1] == ':') {
            out.push_back({Token::Punct, s.substr(i, 2)});
            i += 2;
            continue;
        }
        out.push_back({Token::Punct, s.substr(i, 1)});
        ++i;
    }
    return out;
}

// Words split at spaces (a set literal this long would be laid out one word per line).
std::set<std::string_view> wordSet(std::string_view words) {
    std::set<std::string_view> out;
    for (size_t at = 0; at < words.size();) {
        const size_t space = std::min(words.find(' ', at), words.size());
        if (space > at) out.insert(words.substr(at, space - at));
        at = space + 1;
    }
    return out;
}

const std::set<std::string_view>& keywords() {
    static const auto words = wordSet(
        "alignas alignof and asm auto bool break case catch char char8_t char16_t char32_t class co_await co_return "
        "co_yield concept const consteval constexpr constinit const_cast continue decltype default delete do double "
        "dynamic_cast else enum explicit export extern false final float for friend goto if import inline int long "
        "module mutable namespace new noexcept not nullptr operator or override private protected public register "
        "reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch "
        "template this thread_local throw true try typedef typeid typename union unsigned using virtual void "
        "volatile wchar_t while _Bool _Static_assert __attribute__ __declspec");
    return words;
}

// Keywords that can be part of a declaration's type: the name after them is being declared.
bool typeKeyword(std::string_view word) {
    static const auto words = wordSet("auto bool char char8_t char16_t char32_t const consteval constexpr constinit "
                                      "double extern float inline int long mutable register short signed static "
                                      "thread_local unsigned void volatile wchar_t _Bool");
    return words.count(word) > 0;
}

} // namespace

std::optional<std::string> unknownName(std::string_view message) {
    // gcc quotes with ‘…’ in UTF-8 locales.
    std::string text = replaceAll(replaceAll(std::string(message), "‘", "'"), "’", "'");
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    auto result = [](const std::string& name) -> std::optional<std::string> {
        std::string bare = bareName(name);
        if (bare.empty()) return std::nullopt;
        return bare;
    };

    // clang: the description, then the name.
    static const std::vector<std::string_view> nameAfter{
        "unknown type name '",
        "use of undeclared identifier '",
        "no template named '",
        "no type named '",
        "no member named '",
        "unknown class name '",
        "call to undeclared function '",
        "implicit declaration of function '",
        "use of undeclared '",
    };
    for (std::string_view start : nameAfter) {
        if (!lower.starts_with(start)) continue;
        const auto name = quoted(text, start.size() - 1);
        if (!name) return std::nullopt;
        // "… in namespace 'std'" qualifies it; "… in 'Player'" is a class's missing member, which no
        // #include adds.
        const std::string rest = lower.substr(name->second);
        if (rest.starts_with(" in namespace '")) {
            const auto ns = quoted(text, name->second + std::string_view(" in namespace ").size());
            return ns ? result(ns->first + "::" + name->first) : std::nullopt;
        }
        if (rest.starts_with(" in '")) return std::nullopt;
        return result(name->first);
    }
    // Incomplete types, in either compiler's words: a declaration where the definition was needed.
    if (const size_t at = lower.find("incomplete type '"); at != std::string::npos)
        if (const auto name = quoted(text, at + std::string_view("incomplete type ").size()))
            return result(name->first);

    // gcc: the name, then what's wrong with it.
    if (!text.starts_with("'")) return std::nullopt;
    const auto name = quoted(text, 0);
    if (!name) return std::nullopt;
    const std::string rest = lower.substr(name->second);
    for (std::string_view tail : {" was not declared in this scope", " does not name a type", " has not been declared",
                                  " was not declared", " does not name a template type"})
        if (rest.starts_with(tail)) return result(name->first);
    if (rest.starts_with(" is not a member of '"))
        if (const auto ns = quoted(text, name->second)) return result(ns->first + "::" + name->first);
    if (rest.starts_with(" in namespace '")) {
        const auto ns = quoted(text, name->second);
        if (ns && lower.substr(ns->second).starts_with(" does not name a"))
            return result(ns->first + "::" + name->first);
    }
    return std::nullopt;
}

std::set<std::string> declaredNames(std::string_view text) {
    std::set<std::string> names;
    const std::vector<Token> t = tokenize(text, names);
    enum class Scope { Namespace, Enum, Other };
    std::vector<Scope> scopes;
    std::map<size_t, Scope> braceKinds; // what the '{' at a token opens, when what came before it said
    int parens = 0;

    auto is = [&t](size_t k, std::string_view s) {
        return k < t.size() && t[k].kind != Token::Literal && t[k].text == s;
    };
    auto ident = [&t](size_t k) { return k < t.size() && t[k].kind == Token::Ident; };
    auto add = [&names](std::string_view name) {
        if (!keywords().count(name)) names.insert(std::string(name));
    };
    auto namespaceLevel = [&scopes](size_t ignoreLast) {
        return std::all_of(scopes.begin(), scopes.end() - static_cast<long>(ignoreLast),
                           [](Scope s) { return s == Scope::Namespace; });
    };
    // Past [[attributes]], alignas(…), __attribute__((…)) and __declspec(…).
    auto skipAttributes = [&](size_t k) {
        for (;;) {
            std::string_view open, close;
            if (is(k, "[") && is(k + 1, "[")) {
                open = "[", close = "]";
            } else if (ident(k) && is(k + 1, "(") &&
                       (t[k].text == "alignas" || t[k].text == "__attribute__" || t[k].text == "__declspec")) {
                open = "(", close = ")";
                ++k;
            }
            if (open.empty()) return k;
            for (int depth = 0; k < t.size(); ++k) {
                if (is(k, open)) {
                    ++depth;
                } else if (is(k, close) && --depth == 0) {
                    ++k;
                    break;
                }
            }
        }
    };
    // The '{' or ';' that ends what starts at k (a base class list, an enum's underlying type).
    auto braceOrSemicolon = [&](size_t k) {
        for (int p = 0; k < t.size(); ++k) {
            if (is(k, "(")) ++p;
            else if (is(k, ")")) --p;
            else if (p == 0 && (is(k, "{") || is(k, ";"))) return k;
        }
        return t.size();
    };

    for (size_t i = 0; i < t.size(); ++i) {
        const Token& tok = t[i];
        if (tok.kind == Token::Punct) {
            if (tok.text == "{") {
                Scope scope = Scope::Other;
                if (const auto it = braceKinds.find(i); it != braceKinds.end()) {
                    scope = it->second;
                } else if (parens == 0) {
                    // namespace a::b {, namespace {, inline namespace v1 {, extern "C" {
                    size_t k = i;
                    while (k > 0 &&
                           ((t[k - 1].kind == Token::Ident && t[k - 1].text != "namespace") || is(k - 1, "::")))
                        --k;
                    if (k > 0 && is(k - 1, "namespace")) scope = Scope::Namespace;
                    else if (i >= 2 && t[i - 1].kind == Token::Literal && is(i - 2, "extern")) scope = Scope::Namespace;
                }
                scopes.push_back(scope);
            } else if (tok.text == "}") {
                if (!scopes.empty()) scopes.pop_back();
            } else if (tok.text == "(") {
                ++parens;
            } else if (tok.text == ")") {
                parens = std::max(0, parens - 1);
            }
            continue;
        }
        if (tok.kind != Token::Ident || parens > 0) continue;
        if (!scopes.empty() && scopes.back() == Scope::Enum) {
            if (namespaceLevel(1) && (is(i - 1, "{") || is(i - 1, ","))) add(tok.text);
            continue;
        }
        if (!namespaceLevel(0)) continue;

        const std::string_view word = tok.text;
        if (word == "class" || word == "struct" || word == "union") {
            if (i > 0 && is(i - 1, "enum")) continue;
            size_t k = i + 1;
            std::string_view name;
            for (;;) {
                if (const size_t past = skipAttributes(k); past != k) {
                    k = past;
                } else if (ident(k)) {
                    if (t[k].text != "final") name = t[k].text;
                    ++k;
                } else if (is(k, "::")) {
                    ++k;
                } else {
                    break;
                }
            }
            // Defined here (a body, or bases then a body), not only declared: `class TileMap;` makes
            // no complete type, and `template <class T>` is no class at all.
            if (!name.empty() && (is(k, "{") || is(k, ":"))) {
                const size_t brace = is(k, "{") ? k : braceOrSemicolon(k);
                if (is(brace, "{")) {
                    add(name);
                    braceKinds[brace] = Scope::Other;
                    i = brace - 1;
                    continue;
                }
            }
            i = k - 1;
            continue;
        }
        if (word == "enum") {
            size_t k = i + 1;
            const bool scoped = is(k, "class") || is(k, "struct");
            if (scoped) ++k;
            k = skipAttributes(k);
            std::string_view name;
            if (ident(k)) name = t[k++].text;
            if (is(k, ":")) k = braceOrSemicolon(k);
            if (is(k, "{")) {
                if (!name.empty()) add(name);
                braceKinds[k] = scoped ? Scope::Other : Scope::Enum;
            }
            i = k - 1;
            continue;
        }
        if (word == "using") {
            if (ident(i + 1) && is(i + 2, "=")) add(t[i + 1].text);
            continue;
        }
        if (word == "concept") {
            if (ident(i + 1)) add(t[i + 1].text);
            continue;
        }
        if (word == "typedef") {
            // The names before each ',' and the ';' (typedef int A, *B;), and (*Name) function pointers.
            // The loop goes on after the typedef, so a struct or enum defined in it counts as well.
            int braces = 0, p = 0;
            for (size_t k = i + 1; k < t.size(); ++k) {
                if (is(k, "{")) ++braces;
                else if (is(k, "}")) --braces;
                else if (is(k, "(")) ++p;
                else if (is(k, ")")) --p;
                if (braces < 0) break;
                if (braces > 0) continue;
                if (ident(k) && is(k - 1, "*") && is(k - 2, "(") && is(k + 1, ")")) add(t[k].text);
                if (p == 0 && (is(k, ",") || is(k, ";")) && ident(k - 1)) add(t[k - 1].text);
                if (is(k, ";")) break;
            }
            continue;
        }
        if (keywords().count(word)) continue;
        // A function or variable: a name after its type, followed by its parameters or the end of
        // the declaration.
        const bool afterType = i > 0 && ((t[i - 1].kind == Token::Ident &&
                                          (!keywords().count(t[i - 1].text) || typeKeyword(t[i - 1].text))) ||
                                         is(i - 1, "*") || is(i - 1, "&") || is(i - 1, ">"));
        if (afterType &&
            (is(i + 1, "(") || is(i + 1, ";") || is(i + 1, "=") || is(i + 1, "[") || is(i + 1, "{") || is(i + 1, ",")))
            add(word);
    }
    return names;
}

bool isHeader(const fs::path& file) {
    static const std::set<std::string> extensions{".h", ".hpp", ".hh", ".hxx", ".h++", ".inl", ".ipp", ".tpp"};
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return extensions.count(ext) > 0;
}

std::string includeSpelling(const fs::path& file, const fs::path& header, const std::vector<fs::path>& includeDirs) {
    const fs::path target = header.lexically_normal();
    const fs::path folder = file.parent_path().lexically_normal();
    auto parts = [](const fs::path& p) { return std::distance(p.begin(), p.end()); };
    auto within = [&target](const fs::path& dir) -> std::optional<fs::path> {
        const fs::path rel = target.lexically_relative(dir.lexically_normal());
        if (rel.empty() || rel == "." || *rel.begin() == "..") return std::nullopt;
        return rel;
    };
    std::optional<fs::path> best = within(folder);
    for (const auto& dir : includeDirs) {
        const auto rel = within(dir);
        if (rel && (!best || parts(*rel) < parts(*best))) best = rel;
    }
    if (!best) {
        const fs::path rel = target.lexically_relative(folder);
        best = rel.empty() ? target : rel;
    }
    return "\"" + best->generic_string() + "\"";
}

std::vector<fs::path> HeaderIndex::find(const std::vector<fs::path>& folders, std::string_view name, size_t limit) {
    std::string last(name);
    if (const size_t colons = last.rfind("::"); colons != std::string::npos) last.erase(0, colons + 2);
    std::vector<fs::path> found;
    if (last.empty()) return found;
    std::set<fs::path> seen;
    size_t headers = 0;
    for (const auto& folder : folders) {
        for (const auto& rel : search::listFiles(folder, 50000)) {
            const fs::path file = (folder / rel).lexically_normal();
            if (!isHeader(file) || !seen.insert(file).second) continue;
            if (headers++ == limit) return found;
            std::error_code ec;
            const auto size = fs::file_size(file, ec);
            if (ec) continue;
            const auto time = fs::last_write_time(file, ec);
            if (ec) continue;
            auto [it, added] = files_.try_emplace(file);
            Entry& entry = it->second;
            if (added || entry.size != size || entry.time != time) {
                entry.size = size;
                entry.time = time;
                entry.names.clear();
                if (size <= 4 * 1024 * 1024) { // bigger is generated, not something to #include by hand
                    std::ifstream in(file, std::ios::binary);
                    std::ostringstream text;
                    text << in.rdbuf();
                    entry.names = declaredNames(text.str());
                }
            }
            if (entry.names.count(last)) found.push_back(file);
        }
    }
    return found;
}

std::vector<Fix> includeFixes(const fs::path& file, std::string_view text, const std::vector<fs::path>& headers,
                              const std::vector<fs::path>& includeDirs, const std::vector<Fix>& serverFixes) {
    struct Candidate {
        std::string spelling;
        long ups = 0, depth = 0;
    };
    std::vector<Candidate> candidates;
    for (const auto& header : headers) {
        if (header.lexically_normal() == file.lexically_normal()) continue;
        const std::string spelling = includeSpelling(file, header, includeDirs);
        if (!boilerplate::missingIncludes(text, {spelling})) continue; // there already
        const bool offered = std::any_of(serverFixes.begin(), serverFixes.end(), [&spelling](const Fix& fix) {
            return fix.include == spelling ||
                   std::any_of(fix.edits.begin(), fix.edits.end(), [&spelling](const format::TextEdit& e) {
                       return e.newText.find("#include " + spelling) != std::string::npos;
                   });
        });
        const bool twice = std::any_of(candidates.begin(), candidates.end(),
                                       [&spelling](const Candidate& c) { return c.spelling == spelling; });
        if (offered || twice) continue;
        Candidate c{spelling};
        const fs::path path(spelling.substr(1, spelling.size() - 2));
        for (const auto& part : path) (part == ".." ? c.ups : c.depth)++;
        candidates.push_back(std::move(c));
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return std::tie(a.ups, a.depth, a.spelling) < std::tie(b.ups, b.depth, b.spelling);
    });
    std::vector<Fix> fixes;
    for (const auto& c : candidates) fixes.push_back({"#include " + c.spelling, c.spelling, {}});
    return fixes;
}

namespace {

// "\"TileMap.hpp\"" when the edits only insert the line `#include "TileMap.hpp"`.
std::optional<std::string> addedInclude(const std::vector<format::TextEdit>& edits) {
    std::string added;
    for (const auto& e : edits) {
        if (e.startLine != e.endLine || e.startUtf16 != e.endUtf16) return std::nullopt; // replaces text
        added += e.newText;
    }
    const size_t first = added.find_first_not_of(" \t\n");
    const size_t last = added.find_last_not_of(" \t\n");
    if (first == std::string::npos) return std::nullopt;
    const std::string line = added.substr(first, last - first + 1);
    if (!line.starts_with("#include") || line.find('\n') != std::string::npos) return std::nullopt;
    std::string target = line.substr(8);
    target.erase(0, target.find_first_not_of(" \t"));
    const bool quoted = target.size() > 2 && ((target.front() == '"' && target.back() == '"') ||
                                              (target.front() == '<' && target.back() == '>'));
    return quoted ? std::optional<std::string>(target) : std::nullopt;
}

} // namespace

std::string withoutFixCount(std::string message) {
    for (std::string_view tail : {" (fix available)", " (fixes available)"})
        if (message.ends_with(tail)) message.erase(message.size() - tail.size());
    return message;
}

std::vector<Fix> parseCodeActions(const nlohmann::json& result, const fs::path& file) {
    std::vector<Fix> preferred, others;
    if (!result.is_array()) return preferred;
    const fs::path target = file.lexically_normal();
    for (const auto& action : result) {
        if (!action.is_object() || action.contains("disabled")) continue;
        const auto edit = action.find("edit");
        if (edit == action.end() || !edit->is_object()) continue; // a Command, or an action that runs one
        const std::string kind = action.value("kind", std::string("quickfix"));
        if (kind != "quickfix" && !kind.starts_with("quickfix.")) continue;

        std::vector<format::TextEdit> edits;
        bool here = true;
        auto take = [&](const std::string& uri, const nlohmann::json& list) {
            const auto path = lsp::uriToPath(uri);
            if (!path || path->lexically_normal() != target) {
                here = false;
                return;
            }
            const auto parsed = format::parseTextEdits(list);
            edits.insert(edits.end(), parsed.begin(), parsed.end());
        };
        if (const auto changes = edit->find("documentChanges"); changes != edit->end() && changes->is_array()) {
            for (const auto& change : *changes) {
                // Creating, renaming or deleting files is more than a fix in this file.
                if (!change.is_object() || !change.contains("textDocument") || !change["textDocument"].is_object()) {
                    here = false;
                    break;
                }
                take(change["textDocument"].value("uri", std::string()), change.value("edits", nlohmann::json()));
            }
        } else if (const auto byUri = edit->find("changes"); byUri != edit->end() && byUri->is_object()) {
            for (auto it = byUri->begin(); it != byUri->end(); ++it) take(it.key(), it.value());
        }
        if (!here || edits.empty()) continue;
        Fix fix{action.value("title", std::string("Fix")), {}, std::move(edits)};
        if (const auto include = addedInclude(fix.edits)) fix = {"#include " + *include, *include, {}};
        (action.value("isPreferred", false) ? preferred : others).push_back(std::move(fix));
    }
    preferred.insert(preferred.end(), std::make_move_iterator(others.begin()), std::make_move_iterator(others.end()));
    return preferred;
}

} // namespace kevc::quickfix
