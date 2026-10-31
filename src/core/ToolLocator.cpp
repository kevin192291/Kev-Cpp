#include "core/ToolLocator.h"

#include "core/Toolchain.h"

#include <glob.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace kevc {

namespace {

bool isExecutable(const fs::path& file) {
    std::error_code ec;
    return fs::is_regular_file(file, ec) && access(file.c_str(), X_OK) == 0;
}

std::vector<fs::path> expandGlob(const std::string& pattern) {
    std::vector<fs::path> out;
    glob_t g{};
    if (glob(pattern.c_str(), GLOB_NOSORT | GLOB_ONLYDIR, nullptr, &g) == 0)
        for (size_t i = 0; i < g.gl_pathc; ++i) out.emplace_back(g.gl_pathv[i]);
    globfree(&g);
    return out;
}

} // namespace

std::vector<long long> versionKey(const std::string& path) {
    std::vector<long long> key;
    long long current = -1;
    for (char c : path) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            // Long build stamps such as 202410170706 fit; anything longer is capped, not overflowed.
            current = current < 0 ? c - '0' : std::min(current * 10 + (c - '0'), 999'999'999'999'999LL);
        } else if (current >= 0) {
            key.push_back(current);
            current = -1;
        }
    }
    if (current >= 0) key.push_back(current);
    return key;
}

std::vector<std::string> ToolLocator::defaultVendorGlobs(const fs::path& optRoot) {
    const std::string st = (optRoot / "st").string();
    return {
        // STM32CubeCLT: the command-line toolset.
        st + "/stm32cubeclt_*/GNU-tools-for-STM32/bin",
        st + "/stm32cubeclt_*/STLink-gdb-server/bin",
        st + "/stm32cubeclt_*/STM32CubeProgrammer/bin",
        // STM32CubeIDE: every bundled tool sits in a plugin's tools/bin.
        st + "/stm32cubeide_*/plugins/*/tools/bin",
    };
}

std::vector<FoundTool> ToolLocator::findAll(const std::string& name) const {
    std::vector<FoundTool> out;
    auto add = [&](const fs::path& file, const char* source) {
        if (!isExecutable(file)) return;
        if (std::any_of(out.begin(), out.end(), [&](const FoundTool& t) { return t.path == file; })) return;
        out.push_back({file, source});
    };

    if (auto it = config_.overrides.find(name); it != config_.overrides.end()) add(it->second, "settings");
    for (const auto& dir : config_.binDirs) add(dir / name, "node_modules");
    if (auto onPath = findInPath(name, config_.pathList)) add(*onPath, "PATH");

    std::vector<fs::path> vendor;
    for (const auto& pattern : config_.vendorGlobs)
        for (const auto& dir : expandGlob(pattern))
            if (isExecutable(dir / name)) vendor.push_back(dir / name);
    std::sort(vendor.begin(), vendor.end(), [](const fs::path& a, const fs::path& b) {
        return versionKey(a.string()) > versionKey(b.string());
    });
    for (const auto& file : vendor) add(file, "vendor");
    return out;
}

std::optional<FoundTool> ToolLocator::find(const std::string& name) const {
    auto all = findAll(name);
    if (all.empty()) return std::nullopt;
    return all.front();
}

} // namespace kevc
