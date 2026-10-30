#pragma once

#include "core/Project.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kevc {

// Splits a flags entry like `-DNAME="a b" -O2` into arguments (shell-style quotes and backslashes).
std::vector<std::string> splitArgs(std::string_view text);
// The inverse of splitArgs, quoting only arguments that need it.
std::string joinArgs(const std::vector<std::string>& args);

// One entry of the bundled FetchContent package catalog (data/packages.json).
struct CatalogEntry {
    std::string name;
    std::string description;
    std::string category;
    std::string gitUrl;
    std::string gitTag;
    std::vector<std::string> targets;
    std::map<std::string, std::string> options;
    std::string include; // e.g. <fmt/core.h>
    std::string notes;   // e.g. system packages the library needs to build

    Package toPackage() const;
};

// Throws std::runtime_error (or nlohmann::json::exception) on malformed input.
std::vector<CatalogEntry> parseCatalog(std::string_view json);

struct PkgConfigModule {
    std::string name;
    std::string description;
};

// Parses `pkg-config --list-all` output, sorted by name.
std::vector<PkgConfigModule> parsePkgConfigList(std::string_view output);
// Runs pkg-config; empty if it is not installed.
std::vector<PkgConfigModule> listPkgConfigModules();

// "Detect targets" for a custom Git package: a throwaway CMake project that fetches the package
// and prints every library target (and namespaced alias) it defines.
std::string targetDetectionProject(const std::string& name, const std::string& gitUrl, const std::string& gitTag);
// Returns the target named on a "KEVC_TARGET <name>" line of the configure output, if any.
std::optional<std::string> parseDetectedTarget(std::string_view line);

// Total size of the regular files under `dir` (0 if it doesn't exist). Symlinks are not followed.
std::uintmax_t directorySize(const std::filesystem::path& dir);
// 0 B, 512 B, 1.5 KB, 23.4 MB, 1.2 GB
std::string humanSize(std::uintmax_t bytes);

} // namespace kevc
