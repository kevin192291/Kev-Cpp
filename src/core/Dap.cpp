#include "core/Dap.h"

#include "core/Toolchain.h"

#include <unistd.h>

#include <cctype>

namespace fs = std::filesystem;

namespace kevc::dap {

int serverPort(std::string_view line) {
    const auto at = line.find("listening at ");
    if (at == std::string_view::npos) return 0;
    const auto colon = line.rfind(':');
    if (colon == std::string_view::npos || colon < at) return 0;
    int port = 0;
    for (size_t i = colon + 1; i < line.size() && std::isdigit(static_cast<unsigned char>(line[i])); ++i)
        port = port * 10 + (line[i] - '0');
    return port > 0 && port < 65536 ? port : 0;
}

json initializeArguments(const std::string& adapterId) {
    return {{"clientID", "kev-cpp"},
            {"clientName", "Kev-C++"},
            {"adapterID", adapterId},
            {"pathFormat", "path"},
            {"linesStartAt1", true},
            {"columnsStartAt1", true},
            {"supportsVariableType", true},
            {"supportsStartDebuggingRequest", true}, // js-debug creates child sessions this way
            {"locale", "en"}};
}

json nodeLaunch(const fs::path& root, const std::string& node, const std::vector<std::string>& runtimeArgs,
                const std::vector<std::pair<std::string, std::string>>& env) {
    json vars = json::object();
    for (const auto& [name, value] : env) vars[name] = value;
    return {{"type", "pwa-node"},
            {"env", vars},
            {"request", "launch"},
            {"name", "Vite (server code)"},
            {"runtimeExecutable", node},
            {"runtimeArgs", runtimeArgs},
            {"cwd", root.string()},
            {"console", "internalConsole"},
            {"outputCapture", "std"},
            {"sourceMaps", true},
            {"autoAttachChildProcesses", true},
            // Only the project's own code: map its sources, step over node and its packages.
            {"resolveSourceMapLocations", json::array({root.string() + "/**", "!**/node_modules/**"})},
            {"skipFiles", json::array({"<node_internals>/**", "**/node_modules/**"})}};
}

json chromeLaunch(const std::string& url, const fs::path& root, const fs::path& chrome, const fs::path& profileDir) {
    return {{"type", "pwa-chrome"},
            {"request", "launch"},
            {"name", "Browser"},
            {"url", url},
            {"webRoot", root.string()},
            {"runtimeExecutable", chrome.string()},
            {"userDataDir", profileDir.string()}, // its own profile: the user's browser is untouched
            {"sourceMaps", true},
            {"skipFiles", json::array({"**/node_modules/**", "**/@vite/**"})}};
}

json setBreakpointsArguments(const fs::path& file, const std::set<int>& lines) {
    json bps = json::array();
    for (int line : lines) bps.push_back({{"line", line}});
    return {{"source", {{"path", file.string()}, {"name", file.filename().string()}}}, {"breakpoints", bps}};
}

bool debuggableSource(const fs::path& file) {
    const std::string ext = file.extension().string();
    for (const char* e : {".ts", ".js", ".mjs", ".cjs", ".mts", ".cts", ".tsx", ".jsx", ".svelte"})
        if (ext == e) return true;
    return false;
}

bool sessionWantsBreakpoints(const std::string& adapter, const fs::path& file) {
    if (file.extension() != ".svelte") return true;
    return adapter.find("chrome") != std::string::npos || adapter.find("msedge") != std::string::npos;
}

std::vector<Frame> framesOf(const json& body) {
    std::vector<Frame> out;
    for (const auto& f : body.value("stackFrames", json::array())) {
        Frame frame;
        frame.id = f.value("id", 0);
        frame.name = f.value("name", "");
        frame.line = f.value("line", 0);
        if (auto it = f.find("source"); it != f.end() && it->is_object()) {
            const std::string path = it->value("path", "");
            // Only real files: js-debug uses sourceReference for node internals and generated code.
            if (!path.empty() && path.front() == '/') frame.file = path;
        }
        out.push_back(std::move(frame));
    }
    return out;
}

std::vector<Variable> variablesOf(const json& body) {
    std::vector<Variable> out;
    for (const auto& v : body.value("variables", json::array()))
        out.push_back({v.value("name", ""), v.value("value", ""), v.value("type", ""), v.value("variablesReference", 0)});
    return out;
}

std::vector<Scope> scopesOf(const json& body) {
    std::vector<Scope> out;
    for (const auto& s : body.value("scopes", json::array()))
        out.push_back({s.value("name", ""), s.value("variablesReference", 0), s.value("expensive", false)});
    return out;
}

std::optional<fs::path> findChrome(const std::string& chosen, const std::string& pathList) {
    if (!chosen.empty()) {
        if (auto p = findInPath(chosen, pathList)) return *p;
        return std::nullopt;
    }
    for (const char* name : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "brave-browser",
                             "microsoft-edge", "microsoft-edge-stable"})
        if (auto p = findInPath(name, pathList)) return *p;
    return std::nullopt;
}

} // namespace kevc::dap
