#pragma once

#include "core/Project.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

enum class BuildConfig { Debug, Release };
enum class BuildAction { Build, Rebuild, Clean };

struct Command {
    std::vector<std::string> argv;
    std::filesystem::path cwd;
};

struct BuildTools {
    std::string gcc = "gcc";
    std::string gxx = "g++";
    std::string cmake = "cmake";
    std::string make = "make";
    bool ninja = false; // use the Ninja generator for new CMake build dirs
    int jobs = 1;
    std::string node = "node";
    std::string tsc; // path to typescript/bin/tsc; empty if not installed
};

// build/debug or build/release under the project root.
std::filesystem::path buildDir(const std::filesystem::path& root, BuildConfig config);

// The commands that perform `action`, in order. Empty if there is nothing to do
// (e.g. cleaning a CMake project that was never configured). For web projects, Build and Rebuild
// type-check with `tsc --noEmit` when the project has a tsconfig.json.
std::vector<Command> buildCommands(const Project& p, const std::filesystem::path& root, BuildAction action,
                                   BuildConfig config, const BuildTools& tools);

// The executable a console project produces, or nullopt for libraries and web projects.
std::optional<std::filesystem::path> runnableOutput(const Project& p, const std::filesystem::path& root,
                                                    BuildConfig config);

// Points <root>/compile_commands.json at the CMake build dir's database so clangd finds it.
// Never replaces a regular file the user put there. Returns true if the link now exists.
bool linkCompileCommands(const std::filesystem::path& root, BuildConfig config);

// "cmake --build 'my dir'" style text for logs.
std::string displayCommand(const std::vector<std::string>& argv);

} // namespace kevc
