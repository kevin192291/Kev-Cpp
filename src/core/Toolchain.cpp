#include "core/Toolchain.h"

#include <array>
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

std::string firstLineOfVersion(const std::filesystem::path& exe) {
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
        if (info.found() && queryVersions) info.version = firstLineOfVersion(*info.path);
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
