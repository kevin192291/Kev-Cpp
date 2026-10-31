#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Debug Adapter Protocol pieces Kev-C++ needs to drive Microsoft's js-debug (the debugger VS Code
// uses for Node and Chrome): its server's ready line, the launch configurations, and the replies we
// read. Messages use the same Content-Length framing as LSP (lsp::MessageFramer).
namespace kevc::dap {

using json = nlohmann::json;

// "Debug server listening at 127.0.0.1:41117" → 41117; 0 for any other line.
int serverPort(std::string_view line);

json initializeArguments(const std::string& adapterId);

// Runs `node <runtimeArgs…>` in `root` under the debugger (Vite: vite.js dev --port …), following
// source maps into the project's own .ts and .svelte files and into child processes.
json nodeLaunch(const std::filesystem::path& root, const std::string& node, const std::vector<std::string>& runtimeArgs,
                const std::vector<std::pair<std::string, std::string>>& env = {});
// Opens `url` in a separate Chromium-based browser profile under the debugger.
json chromeLaunch(const std::string& url, const std::filesystem::path& root, const std::filesystem::path& chrome,
                  const std::filesystem::path& profileDir);

json setBreakpointsArguments(const std::filesystem::path& file, const std::set<int>& lines);
// Files a breakpoint can go in for js-debug (it maps them through source maps).
bool debuggableSource(const std::filesystem::path& file);
// Whether a session of this adapter type ("pwa-node", "pwa-chrome", …) gets this file's breakpoints.
// .svelte breakpoints go to the browser only: in Vite's server-side render of a component the source
// map doesn't lead back to the right lines, so a breakpoint there paused on lines that don't exist.
bool sessionWantsBreakpoints(const std::string& adapter, const std::filesystem::path& file);

struct Frame {
    int id = 0;
    std::string name;
    std::filesystem::path file; // empty for frames with no file on disk (node internals)
    int line = 0;               // 1-based
};
std::vector<Frame> framesOf(const json& stackTraceBody);

struct Variable {
    std::string name;
    std::string value;
    std::string type;
    int reference = 0; // > 0: has children (a `variables` request returns them)
};
std::vector<Variable> variablesOf(const json& variablesBody);

struct Scope {
    std::string name;
    int reference = 0;
    bool expensive = false; // e.g. Global: shown collapsed
};
std::vector<Scope> scopesOf(const json& scopesBody);

// A Chromium-based browser for js-debug's Chrome debugging: the one chosen in settings, else the first
// installed of Chrome, Chromium, Brave, Edge. nullopt if there's none.
std::optional<std::filesystem::path> findChrome(const std::string& chosen, const std::string& pathList);

} // namespace kevc::dap
