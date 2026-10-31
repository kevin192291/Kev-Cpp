#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A model of a SvelteKit app read straight from its files (no Node): the routes under src/routes,
// what each route file exports, where a page's data comes from, remote functions, and how the
// $env modules are used. It reads source text with simple scanners, so it describes the usual
// shapes of code (`export const load = …`, `export async function GET`, `return { … }`), not every
// possible one.
namespace kevc::kit {

struct Export {
    std::string name; // "load", "actions", "GET", "prerender", …
    int line = 0;     // 1-based
};

struct RouteFile {
    std::filesystem::path path;       // absolute
    std::string name;                 // file name, e.g. "+page.server.ts"
    std::string role;                 // "page", "layout", "server", "error"
    bool serverOnly = false;          // +page.server.ts, +layout.server.ts, +server.ts
    std::vector<Export> exports;
    std::vector<std::string> loadKeys; // the keys `load` returns, if it returns an object literal
    std::vector<std::string> actions;  // form action names (+page.server only)
    std::string layoutReset;           // "+page@(app).svelte" → "(app)"; "+page@.svelte" → "/"; "" if none

    int lineOf(std::string_view exportName) const; // 0 if it doesn't export it
};

struct Route {
    std::string id;                  // with groups, as SvelteKit names it: "/(app)/blog/[slug]"
    std::string url;                 // without groups: "/blog/[slug]"
    std::vector<std::string> params; // "slug", "lang?" (optional), "...rest"
    std::filesystem::path dir;
    std::vector<RouteFile> files;

    const RouteFile* file(std::string_view role, bool serverOnly) const;
    bool hasPage() const { return file("page", false) != nullptr; }
};

struct RemoteFunction {
    std::filesystem::path file;
    std::string name;
    std::string kind; // query, form, command, prerender
    int line = 0;
};

struct App {
    std::vector<Route> routes; // sorted by id
    std::vector<RemoteFunction> remote;
    const Route* find(std::string_view id) const;
};

// Reads <root>/src/routes and every *.remote.ts/js under <root>/src.
App scan(const std::filesystem::path& root);

// The layouts a page renders inside, outermost first, honouring a +page@… reset.
std::vector<const RouteFile*> layoutChain(const App& app, const Route& route);
// Where a page's `data` comes from: each layout load and the page's own load, outermost first, with
// the keys each returns.
struct DataSource {
    const RouteFile* file;
    std::vector<std::string> keys;
};
std::vector<DataSource> dataFlow(const App& app, const Route& route);

// Scanners, exposed for tests.
std::vector<Export> exportsOf(std::string_view source);
std::vector<std::string> returnedKeys(std::string_view source, std::string_view function);
std::vector<std::string> actionNames(std::string_view source);
// "[slug]" → "slug", "[[lang]]" → "lang?", "[...rest]" → "...rest", "[id=integer]" → "id"; "" otherwise.
std::string paramOf(std::string_view segment);

// "New Route…": the route folder for what the user typed ("/blog/[slug]/" → "blog/[slug]"), or
// nothing if it would leave src/routes. "" or "/" is the root route.
std::optional<std::string> routeFolder(std::string_view input);
// The files a new page route starts with: +page.svelte and +page.ts with an empty load.
std::vector<std::pair<std::string, std::string>> newRouteFiles(std::string_view folder);
// A route URL with concrete values in place of its [params]; empty while any is left.
std::string fillParams(std::string_view url);

// ---- $env

struct EnvVar {
    std::string name;
    std::string value;
    std::filesystem::path file;
    int line = 0;
};
// KEY=value lines; `export`, quotes and comments handled as dotenv does.
std::vector<EnvVar> parseDotEnv(std::string_view text, const std::filesystem::path& file);
// Vite's files for `mode`, later ones overriding earlier: .env, .env.local, .env.<mode>, .env.<mode>.local.
std::vector<EnvVar> loadEnv(const std::filesystem::path& root, const std::string& mode = "development");

struct EnvUse {
    std::string name;
    std::string module;          // "$env/static/private", …
    std::filesystem::path file;
    int line = 0;
    bool clientSide = false;     // the file can end up in the browser
};
std::vector<EnvUse> envUses(const std::filesystem::path& root);
// Whether code in this file (relative to the project) can run in the browser.
bool isClientSide(const std::filesystem::path& relative);

struct EnvProblem {
    enum class Kind { Undefined, PrivateInClient, PublicPrefix, Unused };
    Kind kind;
    std::string name;
    std::filesystem::path file;
    int line = 0;
    std::string message;
};
struct EnvRow {
    std::string name;
    bool isPublic = false;               // has the public prefix
    std::vector<EnvVar> definitions;     // in loading order; the last one wins
    std::vector<EnvUse> uses;
};
struct EnvReport {
    std::vector<EnvRow> rows;            // by name
    std::vector<EnvProblem> problems;    // most serious first
};
EnvReport envReport(const std::filesystem::path& root, const std::string& publicPrefix = "PUBLIC_");

} // namespace kevc::kit
