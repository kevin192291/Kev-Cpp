#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace kevc {

// Cpp projects are always built with CMake, C projects always with Make.
enum class ProjectKind { Cpp, C, WebTs };
enum class TargetType { Console, Static, Shared };

struct Package {
    enum class Source { Catalog, Git, System };

    Source source = Source::Catalog;
    std::string name;                 // FetchContent name or pkg-config module
    std::string gitUrl;               // Catalog/Git only
    std::string gitTag;               // Catalog/Git only
    std::vector<std::string> targets; // CMake targets to link, e.g. fmt::fmt
    std::map<std::string, std::string> options; // cache variables set before fetching, e.g. GLFW_BUILD_WAYLAND=OFF

    bool operator==(const Package&) const = default;
};

// How the IDE shows a project, saved with it so it opens the same for everyone: toolbar groups and
// panels shown or hidden (by id; ones not listed follow the IDE's defaults for the kind of project),
// and the Debug/Release choice.
struct ProjectUi {
    std::map<std::string, bool> toolbars;
    std::map<std::string, bool> panels;
    std::string buildConfig; // "Debug" or "Release"; empty: Debug

    bool empty() const { return toolbars.empty() && panels.empty() && buildConfig.empty(); }
    bool operator==(const ProjectUi&) const = default;
};

struct Project {
    std::string name;
    ProjectKind kind = ProjectKind::Cpp;
    TargetType targetType = TargetType::Console;
    std::string standard;   // "20" for C++20, "17" for C17
    std::string outputName; // defaults to the target name

    // When false the IDE adopted an existing CMakeLists.txt whose target it must not redefine.
    bool ownsTarget = true;
    // When false the project's CMakeLists.txt is entirely its own ("Use Its Own CMakeLists.txt"): Kev-C++ writes
    // no managed block and doesn't track sources; it builds with CMake and runs `targetName`.
    bool buildFileManaged = true;
    std::string targetName; // CMake target / Makefile TARGET

    std::vector<std::string> sources;         // relative to the project root, sorted
    std::vector<std::string> excludedSources; // tracked files the user removed from the project
    std::vector<std::string> defines;
    std::vector<std::string> includeDirs;
    std::vector<std::string> compilerFlags;
    std::string debugOptimization = "-O0";
    std::string releaseOptimization = "-O2";
    std::vector<std::string> linkLibraries; // "m" means -lm; anything containing '/' or '::' is used as-is
    std::vector<std::string> linkDirs;
    std::vector<std::string> linkerFlags;
    bool staticRuntime = false; // -static-libstdc++ -static-libgcc
    std::vector<Package> packages;
    ProjectUi ui;
    // Top-level .kevc keys this version doesn't use (as JSON text), written back on save so a
    // project saved by an older Kev-C++ keeps what a newer one added.
    std::map<std::string, std::string> extra;

    static Project load(const std::filesystem::path& projectFile);
    void save(const std::filesystem::path& projectFile) const;

    // Whether a file with this name belongs in the source list for this project kind.
    bool tracksFile(const std::filesystem::path& relPath) const;
    // Replaces the source list (minus exclusions); returns true if it changed.
    bool setSources(std::vector<std::string> found);

    std::string effectiveOutputName() const { return outputName.empty() ? targetName : outputName; }
    std::filesystem::path buildFileName() const; // CMakeLists.txt or Makefile
};

// The "ui" object of a .kevc (and of a web project's remembered state).
nlohmann::json uiToJson(const ProjectUi& ui);
ProjectUi uiFromJson(const nlohmann::json& j);

std::string toString(ProjectKind kind);
std::string toString(TargetType type);
// Turns "My App 2" into "my_app_2" for use as a CMake/Make target.
std::string sanitizeTargetName(const std::string& name);

} // namespace kevc
