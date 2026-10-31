#pragma once

#include "core/Project.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

enum class ProjectTemplate { CppConsole, CppStatic, CppShared, CConsole, WebTs };

// Directories never scanned for sources or shown in the project tree.
bool isIgnoredDirName(const std::string& name);

// A project open on disk: the .kevc model plus the files it manages.
class ProjectSession {
public:
    // Expands `pkg-config --cflags` for C projects' compile_commands.json. Replaceable for tests.
    using PkgConfigCflags = std::function<std::vector<std::string>(const std::vector<std::string>& modules)>;

    struct SyncResult {
        std::vector<std::string> added;
        std::vector<std::string> removed;
        bool buildFileWritten = false;
        std::vector<std::string> warnings;

        bool changed() const { return !added.empty() || !removed.empty(); }
    };

    static std::optional<std::filesystem::path> findProjectFile(const std::filesystem::path& root);
    static ProjectSession open(const std::filesystem::path& projectFile);
    // Creates <parentDir>/<name>/ from a template and writes all of its files.
    static ProjectSession create(const std::filesystem::path& parentDir, const std::string& name, ProjectTemplate tmpl);
    // Adds a .kevc and a managed block to a CMake project that Kev-C++ did not create.
    static ProjectSession adoptCMake(const std::filesystem::path& root);
    // A TypeScript project for a folder with a package.json but no .kevc. It lives in memory only:
    // nothing is written to the folder, so opening someone's npm project leaves it as it was.
    static ProjectSession openWebFolder(const std::filesystem::path& root);

    const Project& project() const { return project_; }
    Project& project() { return project_; }
    const std::filesystem::path& root() const { return root_; }
    // Empty for a session from openWebFolder.
    const std::filesystem::path& projectFile() const { return projectFile_; }
    std::filesystem::path buildFile() const;

    // Every tracked file currently on disk (relative, sorted), ignoring exclusions.
    std::vector<std::string> scanSources() const;
    // Rescans the tree; if the source list changed, saves the .kevc and regenerates the build file.
    SyncResult sync();
    // Saves the .kevc and regenerates the build file and compile database from the current model.
    SyncResult saveAndRegenerate();

    void setPkgConfigCflags(PkgConfigCflags fn) { pkgConfigCflags_ = std::move(fn); }

private:
    ProjectSession(Project p, std::filesystem::path projectFile);
    bool writeBuildFile(std::vector<std::string>& warnings);
    void writeCompileCommands();

    Project project_;
    std::filesystem::path projectFile_;
    std::filesystem::path root_;
    PkgConfigCflags pkgConfigCflags_;
};

} // namespace kevc
