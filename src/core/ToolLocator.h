#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

struct FoundTool {
    std::filesystem::path path;
    std::string source; // "settings", "node_modules", "PATH" or "vendor"
};

// Finds optional tools in more places than $PATH: a path the user chose in settings, node_modules/.bin
// folders (a project's own vite or svelte-check, or the bundled language servers), $PATH, and vendor
// installs such as STM32CubeIDE, which keep their tools in versioned folders off the PATH.
class ToolLocator {
public:
    struct Config {
        std::map<std::string, std::filesystem::path> overrides; // tool name -> path chosen by the user
        std::vector<std::filesystem::path> binDirs;             // searched in order, before $PATH
        std::string pathList;                                   // $PATH
        // Folders as shell globs, e.g. "/opt/st/stm32cubeide_*/plugins/*/tools/bin". Where several
        // match, the one with the newest version numbers in its path wins.
        std::vector<std::string> vendorGlobs;
    };

    // Where ST's tools live in STM32CubeIDE and STM32CubeCLT installs.
    static std::vector<std::string> defaultVendorGlobs(const std::filesystem::path& optRoot = "/opt");

    explicit ToolLocator(Config config) : config_(std::move(config)) {}

    // The best match: settings, then binDirs, then $PATH, then the newest vendor install.
    std::optional<FoundTool> find(const std::string& name) const;
    // Every match, best first (for Toolchain Info).
    std::vector<FoundTool> findAll(const std::string& name) const;

private:
    Config config_;
};

// The numbers in a path, in order ("stm32cubeide_1.19.0" -> 32, 1, 19, 0), for picking the newest of
// several installs laid out the same way.
std::vector<long long> versionKey(const std::string& path);

} // namespace kevc
