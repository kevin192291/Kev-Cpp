#pragma once

#include "core/BuildPlan.h"

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kevc {

// What kind of web project a package.json describes. Not saved anywhere: it's read again each time
// a folder opens, so in-memory package.json folders stay untouched.
enum class WebFlavor { Plain, Vite, Svelte, SvelteKit };
enum class PackageManager { Npm, Pnpm, Yarn, Bun };

struct WebProject {
    WebFlavor flavor = WebFlavor::Plain;
    PackageManager packageManager = PackageManager::Npm;
    std::vector<std::pair<std::string, std::string>> scripts; // name -> command, in package.json order
    std::set<std::string> dependencies;                       // dependencies and devDependencies
    bool hasPackageJson = false;

    // Reads <root>/package.json; a missing or broken one gives a Plain npm project.
    static WebProject detect(const std::filesystem::path& root);

    bool usesVite() const { return flavor != WebFlavor::Plain; }
    bool svelte() const { return flavor == WebFlavor::Svelte || flavor == WebFlavor::SvelteKit; }
    bool hasScript(const std::string& name) const;
    const char* label() const;               // "SvelteKit", "Svelte", "Vite" or "TypeScript"
    std::string packageManagerName() const;  // "npm", "pnpm", "yarn" or "bun"

    // `npm install` and friends.
    std::vector<std::string> installCommand() const;
    // `npm run <script> -- <args>` (other managers take the arguments straight after the name).
    std::vector<std::string> runScriptCommand(const std::string& script, const std::vector<std::string>& args = {}) const;
};

// A node_modules/.bin tool as a script for `node` to run, so it works even when node isn't on the
// PATH the IDE was started with. Empty if the project hasn't installed it.
std::filesystem::path nodeBin(const std::filesystem::path& root, const std::string& name);

// Vite's dev server on 127.0.0.1:<port>, failing rather than moving if the port is taken. Runs the
// project's vite directly when "dev" is a plain `vite`/`vite dev` (one process to stop, and it dies
// with the IDE); otherwise the dev script through the package manager.
std::vector<std::string> viteDevCommand(const WebProject& web, const std::filesystem::path& root,
                                        const std::string& node, int port);
// The URL from Vite's "➜  Local:   http://127.0.0.1:5173/" line, or empty.
std::string viteReadyUrl(std::string_view line);

// F9 for Svelte projects: `svelte-kit sync` (SvelteKit) then `svelte-check --output machine-verbose`.
// Empty if svelte-check isn't installed.
std::vector<Command> svelteCheckCommands(const WebProject& web, const std::filesystem::path& root,
                                         const std::string& node);

// Creates a SvelteKit app in <cwd>/<name> with the official CLI (pinned, so its options are the ones we
// know): Svelte 5, TypeScript, no add-ons, dependencies installed with npm. Needs the network.
std::vector<std::string> svCreateCommand(const std::string& name);

// The first port from `from` up that nothing listens on at 127.0.0.1, or 0 if none in 100 tries.
int findFreePort(int from);

} // namespace kevc
