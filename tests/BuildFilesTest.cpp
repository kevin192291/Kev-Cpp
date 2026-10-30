#include "core/BuildFiles.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace kevc;

namespace {

Project cppProject() {
    Project p;
    p.name = "Demo App";
    p.targetName = "demo_app";
    p.kind = ProjectKind::Cpp;
    p.standard = "20";
    p.sources = {"include/foo.hpp", "src/main.cpp"};
    p.compilerFlags = {"-Wall", "-Wextra"};
    return p;
}

Project cProject() {
    Project p;
    p.name = "calc";
    p.targetName = "calc";
    p.kind = ProjectKind::C;
    p.standard = "17";
    p.sources = {"src/main.c", "src/util.c", "src/util.h"};
    p.compilerFlags = {"-Wall"};
    p.linkLibraries = {"m"};
    return p;
}

} // namespace

TEST(CMakeGen, ConsoleBlock) {
    EXPECT_EQ(cmake_gen::block(cppProject()),
              "add_executable(demo_app)\n"
              "target_sources(demo_app PRIVATE\n"
              "    include/foo.hpp\n"
              "    src/main.cpp\n"
              ")\n"
              "set_target_properties(demo_app PROPERTIES\n"
              "    CXX_STANDARD 20\n"
              "    CXX_STANDARD_REQUIRED ON\n"
              "    CXX_EXTENSIONS OFF\n"
              ")\n"
              "target_compile_options(demo_app PRIVATE\n"
              "    -Wall\n"
              "    -Wextra\n"
              "    $<$<CONFIG:Debug>:-O0>\n"
              "    $<$<CONFIG:Release>:-O2>\n"
              ")\n");
}

TEST(CMakeGen, PackagesAndLinker) {
    Project p = cppProject();
    p.compilerFlags.clear();
    p.debugOptimization.clear();
    p.releaseOptimization.clear();
    p.linkLibraries = {"m"};
    p.staticRuntime = true;
    p.packages = {
        {Package::Source::Catalog, "fmt", "https://github.com/fmtlib/fmt.git", "11.0.2", {"fmt::fmt"}, {}},
        {Package::Source::System, "gtk4", "", "", {}, {}},
    };
    const std::string block = cmake_gen::block(p);
    EXPECT_NE(block.find("set(CMAKE_POLICY_VERSION_MINIMUM 3.5)\ninclude(FetchContent)\n"), std::string::npos);
    EXPECT_NE(block.find("FetchContent_Declare(fmt\n"
                         "    GIT_REPOSITORY https://github.com/fmtlib/fmt.git\n"
                         "    GIT_TAG 11.0.2\n"
                         "    GIT_SHALLOW TRUE\n)\n"
                         "FetchContent_MakeAvailable(fmt)\n"),
              std::string::npos);
    EXPECT_NE(block.find("pkg_check_modules(KEVC_PKG_GTK4 REQUIRED IMPORTED_TARGET gtk4)"), std::string::npos);
    EXPECT_NE(block.find("target_link_libraries(demo_app PRIVATE\n"
                         "    fmt::fmt\n"
                         "    PkgConfig::KEVC_PKG_GTK4\n"
                         "    m\n)\n"),
              std::string::npos);
    EXPECT_NE(block.find("target_link_options(demo_app PRIVATE\n    -static-libstdc++\n    -static-libgcc\n)"),
              std::string::npos);
}

TEST(CMakeGen, PackageOptionsAreSetBeforeFetching) {
    Project p = cppProject();
    Package glfw{Package::Source::Catalog, "glfw", "https://github.com/glfw/glfw.git", "3.4", {"glfw"}, {}};
    glfw.options = {{"GLFW_BUILD_WAYLAND", "OFF"}, {"GLFW_BUILD_DOCS", "OFF"}};
    p.packages = {glfw};
    const std::string block = cmake_gen::block(p);
    const auto declare = block.find("FetchContent_Declare(glfw");
    const auto option = block.find("set(GLFW_BUILD_WAYLAND OFF CACHE STRING \"\" FORCE)\n");
    const auto available = block.find("FetchContent_MakeAvailable(glfw)");
    ASSERT_NE(option, std::string::npos);
    EXPECT_LT(declare, option);
    EXPECT_LT(option, available);
}

TEST(CMakeGen, QuotesPathsWithSpacesAndEscapesVariables) {
    Project p = cppProject();
    p.sources = {"src/my file.cpp"};
    p.defines = {"GREETING=\"hi ${USER}\""};
    const std::string block = cmake_gen::block(p);
    EXPECT_NE(block.find("target_sources(demo_app PRIVATE \"src/my file.cpp\")"), std::string::npos);
    EXPECT_NE(block.find(R"x(target_compile_definitions(demo_app PRIVATE "GREETING=\"hi \${USER}\"")x"),
                         std::string::npos);
}

TEST(CMakeGen, AdoptedProjectDoesNotRedefineTarget) {
    Project p = cppProject();
    p.ownsTarget = false;
    const std::string block = cmake_gen::block(p);
    EXPECT_EQ(block.find("add_executable"), std::string::npos);
    EXPECT_EQ(block.find("set_target_properties"), std::string::npos);
    EXPECT_NE(block.find("target_sources(demo_app"), std::string::npos);
}

TEST(CMakeGen, LibraryTypesExportIncludes) {
    Project p = cppProject();
    p.targetType = TargetType::Shared;
    p.includeDirs = {"include"};
    const std::string block = cmake_gen::block(p);
    EXPECT_NE(block.find("add_library(demo_app SHARED)"), std::string::npos);
    EXPECT_NE(block.find("POSITION_INDEPENDENT_CODE ON"), std::string::npos);
    EXPECT_NE(block.find("target_include_directories(demo_app PUBLIC include)"), std::string::npos);
}

TEST(MakeGen, Block) {
    Project p = cProject();
    p.includeDirs = {"include"};
    p.defines = {"VERSION=2"};
    p.packages = {{Package::Source::System, "zlib", "", "", {}, {}}};
    EXPECT_EQ(make_gen::block(p),
              "CC             := gcc\n"
              "KIND           := console\n"
              "OUTPUT         := calc\n"
              "CSTD           := -std=c17\n"
              "SRCS           := src/main.c src/util.c src/util.h\n"
              "CPPFLAGS       := -Iinclude -DVERSION=2\n"
              "CFLAGS         := -Wall\n"
              "DEBUG_CFLAGS   := -O0 -g\n"
              "RELEASE_CFLAGS := -O2 -DNDEBUG\n"
              "LDFLAGS        := \n"
              "LDLIBS         := -lm\n"
              "PKGS           := zlib\n");
}

TEST(MakeGen, SkipsPathsWithSpaces) {
    Project p = cProject();
    p.sources = {"src/ok.c", "src/not ok.c"};
    EXPECT_NE(make_gen::block(p).find("SRCS           := src/ok.c\n"), std::string::npos);
    EXPECT_EQ(make_gen::unsupportedSources(p), std::vector<std::string>{"src/not ok.c"});
}

TEST(MakeGen, RecipesUseTabs) {
    const std::string file = make_gen::initialFile(cProject());
    EXPECT_NE(file.find("\n\t$(CC) $(CSTD) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@\n"), std::string::npos);
}

TEST(CompileCommands, OnlyCFilesWithAllFlags) {
    Project p = cProject();
    p.includeDirs = {"include"};
    auto db = nlohmann::json::parse(compileCommandsJson(p, "/proj", "gcc", {"-I/usr/include/foo"}));
    ASSERT_EQ(db.size(), 2u);
    EXPECT_EQ(db[0]["file"], "/proj/src/main.c");
    EXPECT_EQ(db[0]["directory"], "/proj");
    EXPECT_EQ(db[0]["arguments"], nlohmann::json({"gcc", "-std=c17", "-Iinclude", "-Wall", "-O0", "-g",
                                                  "-I/usr/include/foo", "-c", "src/main.c", "-o",
                                                  "build/debug/obj/src/main.o"}));
}
