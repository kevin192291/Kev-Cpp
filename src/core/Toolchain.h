#pragma once

#include <filesystem>
#include <functional>
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
// First line of `<exe> --version` ("v18.19.1"), empty if it can't run.
std::string toolVersion(const std::filesystem::path& exe);
// The major version in a Node version line: "v18.19.1" -> 18; 0 if there isn't one.
int nodeMajor(const std::string& versionLine);

// Where version managers keep Node, for terminalLikePath. Empty paths mean the usual places.
struct NodeSearch {
    std::filesystem::path home;
    std::filesystem::path nvmDir;                   // $NVM_DIR (~/.nvm)
    std::filesystem::path pnpmHome;                 // $PNPM_HOME (~/.local/share/pnpm)
    std::filesystem::path nPrefix = "/usr/local/n"; // $N_PREFIX
    int minNodeMajor = 20;
};

// Desktop menus, file managers and panels start programs with the session's PATH, without what
// ~/.bashrc adds for terminals: nvm's node, pnpm's and bun's folders. Ubuntu's own node is then too old
// for Vite. Returns `pathList` with the newest node a version manager installed (nvm, fnm, Volta, asdf,
// mise, n) put first when the PATH's node is missing or older than `where.minNodeMajor`, and pnpm's
// and bun's folders added at the end where they exist. `pathNodeMajor` gives the PATH's node's major
// version (0 without one); it runs node, so it's called only when there's a newer node to put first
// that isn't the PATH's own.
std::string terminalLikePath(const std::string& pathList, const std::function<int()>& pathNodeMajor,
                             const NodeSearch& where);

class Toolchain {
public:
    // Resolves every tool against $PATH without running any (versions are left empty: asking each
    // tool took longer than the rest of startup on old laptops; Tools → Toolchain Info asks them).
    static Toolchain detect();
    // Resolves against an explicit PATH list, recording versions if asked to.
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
