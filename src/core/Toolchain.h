#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

struct ToolInfo {
    std::string name;      // executable name, e.g. "g++"
    bool required = false; // missing required tools stop the IDE from starting
    std::string purpose;   // shown to the user when the tool is missing
    std::optional<std::filesystem::path> path;
    std::string version;   // first line of `<tool> --version`, empty if not found

    bool found() const { return path.has_value(); }
};

// Searches a PATH-style list ("a:b:c") for an executable file called `name`.
std::optional<std::filesystem::path> findInPath(const std::string& name, const std::string& pathList);
// `dir` put first in a PATH-style list (unless it's already in it).
std::string prependToPath(const std::string& pathList, const std::string& dir);

class Toolchain {
public:
    // Resolves every tool against $PATH and records its version.
    static Toolchain detect();
    // Resolves against an explicit PATH list without running anything (used by tests).
    static Toolchain detect(const std::string& pathList, bool queryVersions);

    const std::vector<ToolInfo>& tools() const { return tools_; }
    const ToolInfo* get(const std::string& name) const;
    std::filesystem::path pathOf(const std::string& name) const;

    std::vector<const ToolInfo*> missingRequired() const;
    std::vector<const ToolInfo*> missingOptional() const;

private:
    std::vector<ToolInfo> tools_;
};

} // namespace kevc
