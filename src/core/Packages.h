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

// The -l libraries of a `pkg-config --libs` line that aren't in any -L folder it names (as
// lib<name>.so or .a). The linker would then take a library of that name from somewhere else, or
// fail: SFML 3 installed as static libraries says -lsfml-graphics but only has libsfml-graphics-s.a,
// so a build picks up the distribution's SFML 2. Nothing when the line names no -L folder (the
// library is in the system's own folders).
std::vector<std::string> missingPkgConfigLibs(std::string_view libsLine);
// What `pkg-config <arguments> <modules>` prints, trimmed; empty when it fails or isn't installed.
std::string runPkgConfig(const std::string& arguments, const std::vector<std::string>& modules);

// A library's own CMake package (what find_package(<name>) finds), as installed.
struct CMakeConfig {
    std::string name;
    std::string version;       // from <name>ConfigVersion.cmake; may be empty
    std::filesystem::path dir; // the folder holding <name>Config.cmake
    int major() const;         // 0 when the version is unknown
};
// Where find_package looks first: CMAKE_PREFIX_PATH's folders, then /usr/local and /usr.
std::vector<std::filesystem::path> cmakePrefixes();
// The first `name` package under the prefixes (<prefix>/lib/cmake/<name>, lib/<arch>/cmake, lib64,
// share), the order the compiler finds its headers in too.
std::optional<CMakeConfig> findCMakeConfig(const std::string& name, const std::vector<std::filesystem::path>& prefixes);

// SFML is linked three ways, depending on what the computer has:
//   - SFML 3 with its CMake package: find_package (its pkg-config files name libraries a static
//     install doesn't have);
//   - an SFML that only pkg-config knows (the distribution's 2.x): pkg-config modules;
//   - none: fetched and built with the project (SFML 3).
namespace sfml {

struct Install {
    std::optional<CMakeConfig> config; // findCMakeConfig("SFML", …)
    std::string pkgConfigVersion;      // `pkg-config --modversion sfml-graphics`, or empty
    // The API a program should be written for: 2 or 3 (3 when there's no SFML at all).
    int major() const;
    bool installed() const { return config || !pkgConfigVersion.empty(); }
};
Install detect(); // looks at this computer

// The components ("Graphics", "Audio", "Network", "Window", "System") a project's packages ask for.
std::vector<std::string> components(const std::vector<Package>& packages);
// The packages that link those components against `install`.
std::vector<Package> packagesFor(const Install& install, std::vector<std::string> components);
// Makes `wanted` the project's SFML packages, in place of any it has. False when it already had them.
bool setPackages(std::vector<Package>& packages, const std::vector<Package>& wanted);
// A project whose SFML comes from pkg-config modules although SFML 3's CMake package is installed
// and `libsLine` (pkg-config --libs of those modules) names libraries that aren't there: its build
// would link another SFML.
bool needsCMakePackage(const std::vector<Package>& packages, const Install& install, std::string_view libsLine);

} // namespace sfml

// Total size of the regular files under `dir` (0 if it doesn't exist). Symlinks are not followed.
std::uintmax_t directorySize(const std::filesystem::path& dir);
// 0 B, 512 B, 1.5 KB, 23.4 MB, 1.2 GB
std::string humanSize(std::uintmax_t bytes);

} // namespace kevc
