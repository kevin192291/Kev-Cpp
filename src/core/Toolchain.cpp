#include "core/Toolchain.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace kevc {

namespace {

struct ToolSpec {
    const char* name;
    bool required;
    const char* purpose;
};

constexpr std::array kTools{
    ToolSpec{"gcc", true, "C compiler"},
    ToolSpec{"g++", true, "C++ compiler"},
    ToolSpec{"cmake", true, "C++ project builds"},
    ToolSpec{"make", true, "C project builds"},
    ToolSpec{"gdb", true, "debugger"},
    ToolSpec{"node", true, "TypeScript dev server and language server"},
    ToolSpec{"clangd", false, "C/C++ IntelliSense"},
    ToolSpec{"git", false, "CMake FetchContent packages"},
    ToolSpec{"ninja", false, "faster CMake builds (falls back to make)"},
};

std::string firstLineOfVersionImpl(const std::filesystem::path& exe) {
    std::string cmd = "'" + exe.string() + "' --version 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::array<char, 512> buf{};
    std::string line;
    if (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) line = buf.data();
    pclose(pipe);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    return line;
}

} // namespace

std::string toolVersion(const std::filesystem::path& exe) {
    return firstLineOfVersionImpl(exe);
}

int nodeMajor(const std::string& versionLine) {
    size_t i = 0;
    while (i < versionLine.size() && !std::isdigit(static_cast<unsigned char>(versionLine[i]))) ++i;
    int major = 0;
    for (; i < versionLine.size() && std::isdigit(static_cast<unsigned char>(versionLine[i])); ++i)
        major = std::min(major * 10 + (versionLine[i] - '0'), 100000);
    return major;
}

std::string terminalLikePath(const std::string& pathList, const std::filesystem::path& home, int pathNodeMajor,
                             const std::filesystem::path& nvmDir, const std::filesystem::path& pnpmHome,
                             int minNodeMajor) {
    namespace fs = std::filesystem;
    std::string out = pathList;
    if (pathNodeMajor < minNodeMajor) {
        // Where each manager keeps its versions: <dir>/<version>/<binary inside it>.
        const std::pair<fs::path, const char*> layouts[] = {
            {nvmDir.empty() ? home / ".nvm" / "versions/node" : nvmDir / "versions/node", "bin/node"},
            {home / ".local/share/fnm/node-versions", "installation/bin/node"},
            {home / ".fnm/node-versions", "installation/bin/node"},
            {home / ".volta/tools/image/node", "bin/node"},
            {home / ".asdf/installs/nodejs", "bin/node"},
            {home / ".local/share/mise/installs/node", "bin/node"},
            {"/usr/local/n/versions/node", "bin/node"},
        };
        fs::path best;
        std::vector<int> bestVersion;
        for (const auto& [versions, binary] : layouts) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(versions, ec)) {
                const fs::path node = entry.path() / binary;
                if (!fs::is_regular_file(node, ec) || access(node.c_str(), X_OK) != 0) continue;
                std::vector<int> version; // "v24.22.1" -> 24, 22, 1
                int part = -1;
                for (char c : entry.path().filename().string() + ".") {
                    if (std::isdigit(static_cast<unsigned char>(c))) part = std::max(part, 0) * 10 + (c - '0');
                    else if (part >= 0) version.push_back(std::min(part, 100000)), part = -1;
                }
                if (version.empty() || version.front() < minNodeMajor || version.front() <= pathNodeMajor) continue;
                if (version > bestVersion) best = node, bestVersion = version;
            }
        }
        if (!best.empty()) out = prependToPath(out, best.parent_path().string());
    }
    // Package managers' own folders (pnpm and bun install themselves there), last so they shadow nothing.
    for (const fs::path& dir : {pnpmHome.empty() ? home / ".local/share/pnpm" : pnpmHome, home / ".bun/bin"}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::stringstream ss(out);
        bool present = false;
        for (std::string entry; std::getline(ss, entry, ':');) present = present || entry == dir.string();
        if (!present) out = out.empty() ? dir.string() : out + ":" + dir.string();
    }
    return out;
}

std::string prependToPath(const std::string& pathList, const std::string& dir) {
    if (dir.empty()) return pathList;
    std::stringstream ss(pathList);
    for (std::string entry; std::getline(ss, entry, ':');)
        if (entry == dir) return pathList;
    return pathList.empty() ? dir : dir + ":" + pathList;
}

std::optional<std::filesystem::path> findInPath(const std::string& name, const std::string& pathList) {
    if (name.find('/') != std::string::npos) {
        if (access(name.c_str(), X_OK) == 0) return std::filesystem::path(name);
        return std::nullopt;
    }
    std::stringstream ss(pathList);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) dir = ".";
        std::filesystem::path candidate = std::filesystem::path(dir) / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec) && access(candidate.c_str(), X_OK) == 0)
            return candidate;
    }
    return std::nullopt;
}

Toolchain Toolchain::detect() {
    const char* env = std::getenv("PATH");
    return detect(env ? env : "", true);
}

Toolchain Toolchain::detect(const std::string& pathList, bool queryVersions) {
    Toolchain tc;
    for (const auto& spec : kTools) {
        ToolInfo info{spec.name, spec.required, spec.purpose, findInPath(spec.name, pathList), {}};
        if (info.found() && queryVersions) info.version = firstLineOfVersionImpl(*info.path);
        tc.tools_.push_back(std::move(info));
    }
    return tc;
}

const ToolInfo* Toolchain::get(const std::string& name) const {
    for (const auto& t : tools_)
        if (t.name == name) return &t;
    return nullptr;
}

std::filesystem::path Toolchain::pathOf(const std::string& name) const {
    const ToolInfo* t = get(name);
    return (t && t->path) ? *t->path : std::filesystem::path(name);
}

std::vector<const ToolInfo*> Toolchain::missingRequired() const {
    std::vector<const ToolInfo*> out;
    for (const auto& t : tools_)
        if (t.required && !t.found()) out.push_back(&t);
    return out;
}

std::vector<const ToolInfo*> Toolchain::missingOptional() const {
    std::vector<const ToolInfo*> out;
    for (const auto& t : tools_)
        if (!t.required && !t.found()) out.push_back(&t);
    return out;
}

} // namespace kevc
