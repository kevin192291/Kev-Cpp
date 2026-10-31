#include "core/SvelteKit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace kevc::kit {

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int lineAt(std::string_view s, size_t offset) {
    return 1 + static_cast<int>(std::count(s.begin(), s.begin() + static_cast<long>(std::min(offset, s.size())), '\n'));
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}
bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// If a string, template literal or comment starts at i, the index just past it; else i.
size_t skipNonCode(std::string_view s, size_t i) {
    if (i >= s.size()) return i;
    const char c = s[i];
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
        const auto nl = s.find('\n', i);
        return nl == std::string_view::npos ? s.size() : nl;
    }
    if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
        const auto end = s.find("*/", i + 2);
        return end == std::string_view::npos ? s.size() : end + 2;
    }
    if (c == '"' || c == '\'' || c == '`') {
        for (size_t j = i + 1; j < s.size(); ++j) {
            if (s[j] == '\\') {
                ++j;
                continue;
            }
            if (s[j] == c) return j + 1;
        }
        return s.size();
    }
    return i;
}

size_t skipSpace(std::string_view s, size_t i) {
    while (i < s.size()) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
            continue;
        }
        const size_t past = s[i] == '/' ? skipNonCode(s, i) : i;
        if (past == i) break;
        i = past;
    }
    return i;
}

// s[open] is (, [ or {; the index of its partner, or npos.
size_t matchBracket(std::string_view s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size();) {
        const size_t past = skipNonCode(s, i);
        if (past != i) {
            i = past;
            continue;
        }
        const char c = s[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') {
            if (--depth == 0) return i;
        }
        ++i;
    }
    return std::string_view::npos;
}

std::string identAt(std::string_view s, size_t i) {
    if (i >= s.size() || !isIdentStart(s[i])) return {};
    size_t j = i;
    while (j < s.size() && isIdentChar(s[j])) ++j;
    return std::string(s.substr(i, j - i));
}

// The keys of the object literal whose { is at s[open].
std::vector<std::string> objectKeys(std::string_view s, size_t open) {
    std::vector<std::string> keys;
    const size_t close = matchBracket(s, open);
    if (close == std::string_view::npos) return keys;
    size_t i = open + 1;
    while (true) {
        i = skipSpace(s, i);
        if (i >= close) break;
        std::string key;
        if (s.compare(i, 3, "...") == 0) {
            i += 3;
        } else if (s[i] == '"' || s[i] == '\'') {
            const size_t past = skipNonCode(s, i);
            key = std::string(s.substr(i + 1, past - i - 2));
            i = past;
        } else if (s[i] == '[') {
            i = matchBracket(s, i) + 1; // computed key: no name to show
        } else if (isIdentStart(s[i])) {
            key = identAt(s, i);
            i += key.size();
            // `async name()`, `get name()`
            const size_t after = skipSpace(s, i);
            if ((key == "async" || key == "get" || key == "set") && isIdentStart(s[after])) {
                key = identAt(s, after);
                i = after + key.size();
            }
        } else {
            ++i;
            continue;
        }
        // Skip the value (or method body) up to the next comma at this level.
        while (i < close) {
            const size_t past = skipNonCode(s, i);
            if (past != i) {
                i = past;
                continue;
            }
            const char c = s[i];
            if (c == ',') break;
            if (c == '(' || c == '[' || c == '{') {
                const size_t end = matchBracket(s, i);
                if (end == std::string_view::npos || end >= close) {
                    i = close;
                    break;
                }
                i = end + 1;
                continue;
            }
            ++i;
        }
        if (!key.empty() && std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        if (i >= close) break;
        ++i; // the comma
    }
    return keys;
}

// Whether `name` is a whole word at s[i].
bool wordAt(std::string_view s, size_t i, std::string_view name) {
    if (s.compare(i, name.size(), name) != 0) return false;
    if (i > 0 && isIdentChar(s[i - 1])) return false;
    return i + name.size() >= s.size() || !isIdentChar(s[i + name.size()]);
}

// Keys of the object literals a function body returns at its own level.
std::vector<std::string> returnsInBody(std::string_view s, size_t open) {
    std::vector<std::string> keys;
    const size_t close = matchBracket(s, open);
    if (close == std::string_view::npos) return keys;
    int depth = 0;
    for (size_t i = open; i < close;) {
        const size_t past = skipNonCode(s, i);
        if (past != i) {
            i = past;
            continue;
        }
        const char c = s[i];
        if (c == '{' || c == '(' || c == '[') ++depth;
        else if (c == '}' || c == ')' || c == ']') --depth;
        else if (depth == 1 && wordAt(s, i, "return")) {
            const size_t value = skipSpace(s, i + 6);
            if (value < close && s[value] == '{')
                for (auto& k : objectKeys(s, value))
                    if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
        }
        ++i;
    }
    return keys;
}

std::string roleOf(const std::string& name) {
    for (const char* role : {"page", "layout", "server", "error"})
        if (name.starts_with(std::string("+") + role)) return role;
    return {};
}

std::string parentId(const std::string& id) {
    if (id == "/") return {};
    const auto slash = id.rfind('/');
    return slash == 0 ? "/" : id.substr(0, slash);
}

std::string lastSegment(const std::string& id) {
    return id == "/" ? "" : id.substr(id.rfind('/') + 1);
}

} // namespace

// ---- scanners

std::vector<Export> exportsOf(std::string_view source) {
    static const std::regex fn(R"(^\s*export\s+(?:async\s+)?function\s*\*?\s*([A-Za-z_$][\w$]*))");
    static const std::regex var(R"(^\s*export\s+(?:const|let|var)\s+([A-Za-z_$][\w$]*))");
    static const std::regex list(R"(^\s*export\s*\{([^}]*)\})");
    std::vector<Export> out;
    std::istringstream in{std::string(source)};
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        std::smatch m;
        if (std::regex_search(line, m, fn) || std::regex_search(line, m, var)) {
            out.push_back({m[1], number});
        } else if (std::regex_search(line, m, list)) {
            std::stringstream names(m[1].str());
            std::string item;
            while (std::getline(names, item, ',')) {
                // "a", "a as b": the exported name is the last word
                std::istringstream words(item);
                std::string word, last;
                while (words >> word) last = word;
                if (!last.empty()) out.push_back({last, number});
            }
        }
    }
    return out;
}

std::vector<std::string> returnedKeys(std::string_view s, std::string_view function) {
    const std::string name(function);
    const std::regex def("(?:\\bfunction\\s*\\*?\\s*" + name + "\\s*\\(|\\b(?:const|let|var)\\s+" + name + "\\b[^=;]*=)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_search(s.begin(), s.end(), m, def)) return {};
    size_t i = static_cast<size_t>(m.position(0) + m.length(0));
    const bool isFunctionKeyword = m.str(0).find("function") != std::string::npos;
    if (isFunctionKeyword) {
        const size_t paramsEnd = matchBracket(s, i - 1);
        if (paramsEnd == std::string_view::npos) return {};
        const size_t body = s.find('{', paramsEnd);
        return body == std::string_view::npos ? std::vector<std::string>{} : returnsInBody(s, body);
    }
    // const load = async (event) => …  /  const load: PageLoad = function (event) { … }
    i = skipSpace(s, i);
    if (wordAt(s, i, "async")) i = skipSpace(s, i + 5);
    if (wordAt(s, i, "function")) {
        const size_t params = s.find('(', i);
        const size_t paramsEnd = params == std::string_view::npos ? params : matchBracket(s, params);
        const size_t body = paramsEnd == std::string_view::npos ? paramsEnd : s.find('{', paramsEnd);
        return body == std::string_view::npos ? std::vector<std::string>{} : returnsInBody(s, body);
    }
    if (i < s.size() && s[i] == '(') {
        const size_t paramsEnd = matchBracket(s, i);
        if (paramsEnd == std::string_view::npos) return {};
        i = paramsEnd + 1;
    } else {
        i += identAt(s, i).size(); // a single unparenthesised parameter
    }
    const size_t arrow = s.find("=>", i);
    if (arrow == std::string_view::npos) return {};
    i = skipSpace(s, arrow + 2);
    if (i < s.size() && s[i] == '{') return returnsInBody(s, i);
    if (i < s.size() && s[i] == '(') {
        const size_t inner = skipSpace(s, i + 1);
        if (inner < s.size() && s[inner] == '{') return objectKeys(s, inner);
    }
    return {};
}

std::vector<std::string> actionNames(std::string_view s) {
    static const std::regex def(R"(\bexport\s+const\s+actions\b[^=;]*=)");
    std::match_results<std::string_view::const_iterator> m;
    if (!std::regex_search(s.begin(), s.end(), m, def)) return {};
    const size_t i = skipSpace(s, static_cast<size_t>(m.position(0) + m.length(0)));
    if (i >= s.size() || s[i] != '{') return {};
    return objectKeys(s, i);
}

std::string paramOf(std::string_view seg) {
    if (seg.starts_with("[[") && seg.ends_with("]]")) {
        std::string inner(seg.substr(2, seg.size() - 4));
        return inner.substr(0, inner.find('=')) + "?";
    }
    if (!seg.starts_with("[") || !seg.ends_with("]")) return {};
    std::string inner(seg.substr(1, seg.size() - 2));
    return inner.substr(0, inner.find('='));
}

// ---- routes

int RouteFile::lineOf(std::string_view exportName) const {
    for (const auto& e : exports)
        if (e.name == exportName) return e.line;
    return 0;
}

const RouteFile* Route::file(std::string_view role, bool serverOnly) const {
    for (const auto& f : files)
        if (f.role == role && f.serverOnly == serverOnly) return &f;
    return nullptr;
}

const Route* App::find(std::string_view id) const {
    for (const auto& r : routes)
        if (r.id == id) return &r;
    return nullptr;
}

App scan(const fs::path& root) {
    App app;
    const fs::path routes = root / "src" / "routes";
    std::error_code ec;
    std::vector<fs::path> dirs;
    if (fs::is_directory(routes, ec)) {
        dirs.push_back(routes);
        for (fs::recursive_directory_iterator it(routes, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) {
                if (it->path().filename().string().starts_with(".")) it.disable_recursion_pending();
                else dirs.push_back(it->path());
            }
        }
    }
    for (const auto& dir : dirs) {
        Route route;
        route.dir = dir;
        const std::string rel = dir.lexically_relative(routes).generic_string();
        route.id = rel == "." ? "/" : "/" + rel;
        std::string url;
        for (const auto& seg : fs::path(rel)) {
            const std::string s = seg.string();
            if (s == "." || (s.starts_with("(") && s.ends_with(")"))) continue; // groups aren't in the URL
            url += "/" + s;
            if (auto p = paramOf(s); !p.empty()) route.params.push_back(p);
        }
        route.url = url.empty() ? "/" : url;

        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec) && entry.path().filename().string().starts_with("+")) files.push_back(entry.path());
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            RouteFile f;
            f.path = path;
            f.name = path.filename().string();
            f.role = roleOf(f.name);
            if (f.role.empty()) continue;
            f.serverOnly = f.role == "server" || f.name.find(".server.") != std::string::npos;
            if (const auto at = f.name.find('@'); at != std::string::npos) {
                const auto dot = f.name.find('.', at);
                f.layoutReset = f.name.substr(at + 1, dot - at - 1);
                if (f.layoutReset.empty()) f.layoutReset = "/";
            }
            const std::string ext = path.extension().string();
            if (ext == ".ts" || ext == ".js") {
                const std::string text = readFile(path);
                f.exports = exportsOf(text);
                if (f.lineOf("load")) f.loadKeys = returnedKeys(text, "load");
                if (f.lineOf("actions")) f.actions = actionNames(text);
            }
            route.files.push_back(std::move(f));
        }
        if (!route.files.empty()) app.routes.push_back(std::move(route));
    }
    std::sort(app.routes.begin(), app.routes.end(), [](const Route& a, const Route& b) { return a.id < b.id; });

    // Remote functions can live anywhere under src.
    static const std::regex remote(R"(export\s+const\s+([A-Za-z_$][\w$]*)\s*=\s*(query|form|command|prerender)\b)");
    for (fs::recursive_directory_iterator it(root / "src", fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec) && (name == "node_modules" || name.starts_with("."))) {
            it.disable_recursion_pending();
            continue;
        }
        if (!name.ends_with(".remote.ts") && !name.ends_with(".remote.js")) continue;
        const std::string text = readFile(it->path());
        for (std::sregex_iterator m(text.begin(), text.end(), remote), e; m != e; ++m)
            app.remote.push_back({it->path(), (*m)[1], (*m)[2], lineAt(text, static_cast<size_t>(m->position(0)))});
    }
    return app;
}

namespace {

// The routes whose layouts wrap `route`, outermost first, after @ resets. `reset` is the page's (or
// the layout's own) "@…" target.
std::vector<const Route*> layoutRoutes(const App& app, const Route& route, const std::string& reset) {
    std::vector<std::string> ids;
    for (std::string id = route.id; !id.empty(); id = parentId(id)) ids.insert(ids.begin(), id);
    std::vector<const Route*> out;
    for (const auto& id : ids) {
        const Route* r = app.find(id);
        if (!r || !r->file("layout", false)) {
            if (r && (r->file("layout", true))) out.push_back(r); // a server-only layout still loads data
            continue;
        }
        out.push_back(r);
    }
    if (!reset.empty()) {
        // +page@(app).svelte: only the layouts down to the (app) segment; "@" alone: just the root.
        auto keep = std::find_if(out.rbegin(), out.rend(), [&](const Route* r) {
            return reset == "/" ? r->id == "/" : lastSegment(r->id) == reset;
        });
        if (keep != out.rend()) out.erase(keep.base(), out.end());
    }
    return out;
}

std::string pageReset(const Route& route) {
    for (const auto& f : route.files)
        if (f.role == "page" && !f.layoutReset.empty()) return f.layoutReset;
    return {};
}

} // namespace

std::vector<const RouteFile*> layoutChain(const App& app, const Route& route) {
    std::vector<const RouteFile*> out;
    for (const Route* r : layoutRoutes(app, route, pageReset(route)))
        if (const RouteFile* f = r->file("layout", false)) out.push_back(f);
    return out;
}

std::vector<DataSource> dataFlow(const App& app, const Route& route) {
    std::vector<DataSource> out;
    auto add = [&out](const Route& r, const char* role) {
        // The server load runs first and its data reaches the universal load, then the page.
        // (+layout.svelte and +layout.ts share a role: look at every file, not just the first.)
        for (bool server : {true, false})
            for (const auto& f : r.files)
                if (f.role == role && f.serverOnly == server && f.lineOf("load")) out.push_back({&f, f.loadKeys});
    };
    for (const Route* r : layoutRoutes(app, route, pageReset(route))) add(*r, "layout");
    add(route, "page");
    return out;
}

std::optional<std::string> routeFolder(std::string_view input) {
    std::string out;
    std::string segment;
    auto flush = [&]() -> bool {
        if (segment.empty()) return true;
        if (segment == "." || segment == "..") return false;
        out += (out.empty() ? "" : "/") + segment;
        segment.clear();
        return true;
    };
    for (char c : input) {
        if (c == '/' || c == '\\') {
            if (!flush()) return std::nullopt;
        } else if (c == '\0' || c == '\n') {
            return std::nullopt;
        } else {
            segment += c;
        }
    }
    if (!flush()) return std::nullopt;
    return out;
}

std::vector<std::pair<std::string, std::string>> newRouteFiles(std::string_view folder) {
    std::string url;
    for (const auto& seg : fs::path(std::string(folder)))
        if (const std::string s = seg.string(); !(s.starts_with("(") && s.ends_with(")"))) url += "/" + s;
    if (url.empty()) url = "/";
    return {
        {"+page.svelte", "<script lang=\"ts\">\n\tlet { data } = $props();\n</script>\n\n<h1>" + url + "</h1>\n"},
        {"+page.ts", "import type { PageLoad } from './$types';\n\nexport const load: PageLoad = async ({ params }) => {\n\treturn {};\n};\n"},
    };
}

std::string fillParams(std::string_view url) {
    return url.find('[') == std::string_view::npos ? std::string(url) : std::string();
}

// ---- $env

std::vector<EnvVar> parseDotEnv(std::string_view text, const fs::path& file) {
    static const std::regex line(R"(^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$)");
    std::vector<EnvVar> out;
    std::istringstream in{std::string(text)};
    std::string raw;
    int number = 0;
    while (std::getline(in, raw)) {
        ++number;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::smatch m;
        if (!std::regex_match(raw, m, line)) continue;
        std::string value = m[2];
        if (!value.empty() && (value[0] == '"' || value[0] == '\'' || value[0] == '`')) {
            const auto end = value.find(value[0], 1);
            value = end == std::string::npos ? value.substr(1) : value.substr(1, end - 1);
        } else if (const auto hash = value.find(" #"); hash != std::string::npos) {
            value = value.substr(0, hash);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        }
        out.push_back({m[1], value, file, number});
    }
    return out;
}

std::vector<EnvVar> loadEnv(const fs::path& root, const std::string& mode) {
    std::vector<EnvVar> out;
    for (const auto& name : {std::string(".env"), std::string(".env.local"), ".env." + mode, ".env." + mode + ".local"}) {
        std::error_code ec;
        if (!fs::is_regular_file(root / name, ec)) continue;
        auto vars = parseDotEnv(readFile(root / name), root / name);
        out.insert(out.end(), vars.begin(), vars.end());
    }
    return out;
}

bool isClientSide(const fs::path& relative) {
    const std::string s = relative.generic_string();
    const std::string name = relative.filename().string();
    if (name.find(".server.") != std::string::npos) return false; // +page.server.ts, hooks.server.ts
    if (name.starts_with("+server.")) return false;
    if (s.find("/lib/server/") != std::string::npos || s.starts_with("lib/server/")) return false;
    return true;
}

std::vector<EnvUse> envUses(const fs::path& root) {
    static const std::regex import(R"(import\s*\{([^}]*)\}\s*from\s*['"](\$env/(?:static|dynamic)/(?:private|public))['"])");
    std::vector<EnvUse> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root / "src", fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        const std::string fname = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (fname == "node_modules" || fname.starts_with(".")) it.disable_recursion_pending();
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (ext != ".ts" && ext != ".js" && ext != ".svelte" && ext != ".mts" && ext != ".mjs") continue;
        const std::string text = readFile(it->path());
        if (text.find("$env/") == std::string::npos) continue;
        const bool client = isClientSide(it->path().lexically_relative(root / "src"));
        for (std::sregex_iterator m(text.begin(), text.end(), import), e; m != e; ++m) {
            const std::string module = (*m)[2];
            const int line = lineAt(text, static_cast<size_t>(m->position(0)));
            std::stringstream names((*m)[1].str());
            std::string item;
            while (std::getline(names, item, ',')) {
                std::istringstream words(item);
                std::string name, as, alias;
                words >> name >> as >> alias;
                if (name.empty()) continue;
                if (module.find("/dynamic/") != std::string::npos && name == "env") {
                    // import { env } from '$env/dynamic/private'  …  env.API_KEY
                    const std::string local = as == "as" && !alias.empty() ? alias : name;
                    const std::regex use("\\b" + local + R"(\.([A-Za-z_][A-Za-z0-9_]*))");
                    for (std::sregex_iterator u(text.begin(), text.end(), use), ue; u != ue; ++u)
                        out.push_back({(*u)[1], module, it->path(), lineAt(text, static_cast<size_t>(u->position(0))), client});
                } else {
                    out.push_back({name, module, it->path(), line, client});
                }
            }
        }
    }
    return out;
}

EnvReport envReport(const fs::path& root, const std::string& publicPrefix) {
    EnvReport report;
    const auto defs = loadEnv(root);
    const auto uses = envUses(root);
    std::map<std::string, EnvRow> rows;
    for (const auto& d : defs) rows[d.name].definitions.push_back(d);
    for (const auto& u : uses) rows[u.name].uses.push_back(u);

    std::vector<EnvProblem> leaks, undefined, prefix, unused;
    for (auto& [name, row] : rows) {
        row.name = name;
        row.isPublic = name.starts_with(publicPrefix);
        std::set<fs::path> reported;
        for (const auto& u : row.uses) {
            const bool privateModule = u.module.ends_with("/private");
            if (privateModule && u.clientSide && reported.insert(u.file).second)
                leaks.push_back({EnvProblem::Kind::PrivateInClient, name, u.file, u.line,
                                 name + " is private, but " + u.file.filename().string() +
                                     " can run in the browser (SvelteKit won't build this)"});
            if (!privateModule && !row.isPublic)
                prefix.push_back({EnvProblem::Kind::PublicPrefix, name, u.file, u.line,
                                  name + " has no " + publicPrefix + " prefix, so " + u.module + " doesn't include it"});
            if (privateModule && row.isPublic)
                prefix.push_back({EnvProblem::Kind::PublicPrefix, name, u.file, u.line,
                                  name + " is public (" + publicPrefix + "): import it from " +
                                      u.module.substr(0, u.module.rfind('/')) + "/public"});
            if (u.module.find("/static/") != std::string::npos && row.definitions.empty())
                undefined.push_back({EnvProblem::Kind::Undefined, name, u.file, u.line,
                                     name + " isn't in any .env file: the build fails unless it's set in the environment"});
        }
        if (row.uses.empty() && !row.definitions.empty())
            unused.push_back({EnvProblem::Kind::Unused, name, row.definitions.back().file, row.definitions.back().line,
                              name + " isn't used anywhere in src/"});
        report.rows.push_back(row);
    }
    for (auto* list : {&leaks, &undefined, &prefix, &unused})
        report.problems.insert(report.problems.end(), list->begin(), list->end());
    return report;
}

} // namespace kevc::kit
