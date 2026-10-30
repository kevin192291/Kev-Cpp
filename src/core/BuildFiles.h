#pragma once

#include "core/Project.h"

#include <filesystem>
#include <string>
#include <vector>

namespace kevc {

// CMakeLists.txt generation for C++ projects.
namespace cmake_gen {
// The managed block body (no markers).
std::string block(const Project& p);
// A brand-new CMakeLists.txt: user preamble, managed block, and a user section after it.
std::string initialFile(const Project& p);
} // namespace cmake_gen

// Makefile generation for C projects.
namespace make_gen {
std::string block(const Project& p);
std::string initialFile(const Project& p);
// Sources that Make cannot handle (paths containing whitespace); they are left out of SRCS.
std::vector<std::string> unsupportedSources(const Project& p);
} // namespace make_gen

// compile_commands.json for C projects, so clangd works without bear.
// `pkgCflags` is the already-expanded output of `pkg-config --cflags <PKGS>`, split into args.
std::string compileCommandsJson(const Project& p, const std::filesystem::path& root, const std::string& compiler,
                                const std::vector<std::string>& pkgCflags);

} // namespace kevc
