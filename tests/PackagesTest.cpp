#include "core/Packages.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

TEST(Args, SplitHandlesQuotesAndEscapes) {
    EXPECT_EQ(splitArgs(R"(  -O2 -DNAME="a b" '-I/x y' plain\ space -D'Q="1"' )"),
              (std::vector<std::string>{"-O2", "-DNAME=a b", "-I/x y", "plain space", "-DQ=\"1\""}));
    EXPECT_TRUE(splitArgs("   ").empty());
    EXPECT_EQ(splitArgs(R"("")"), std::vector<std::string>{""});
}

TEST(Args, JoinRoundTrips) {
    const std::vector<std::string> args{"-O2", "-DNAME=a b", "-DQ=\"1\"", "back\\slash"};
    EXPECT_EQ(joinArgs(args), R"(-O2 "-DNAME=a b" "-DQ=\"1\"" "back\\slash")");
    EXPECT_EQ(splitArgs(joinArgs(args)), args);
}

TEST(Catalog, BundledCatalogIsValid) {
    std::ifstream in(std::string(KEVC_SOURCE_DIR) + "/data/packages.json");
    std::stringstream ss;
    ss << in.rdbuf();
    const auto entries = parseCatalog(ss.str());
    ASSERT_GE(entries.size(), 11u);
    std::set<std::string> names;
    for (const auto& e : entries) {
        EXPECT_TRUE(names.insert(e.name).second) << "duplicate " << e.name;
        EXPECT_TRUE(e.gitUrl.starts_with("https://")) << e.name;
        EXPECT_FALSE(e.gitTag.empty()) << e.name;
        EXPECT_FALSE(e.targets.empty()) << e.name;
        EXPECT_FALSE(e.description.empty()) << e.name;
        EXPECT_TRUE(e.include.starts_with("<") && e.include.ends_with(">")) << e.name;
    }
    const auto fmt = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return e.name == "fmt"; });
    ASSERT_NE(fmt, entries.end());
    const Package p = fmt->toPackage();
    EXPECT_EQ(p.source, Package::Source::Catalog);
    EXPECT_EQ(p.targets, std::vector<std::string>{"fmt::fmt"});
}

TEST(PkgConfig, ParsesListAll) {
    const auto mods = parsePkgConfigList("zlib                           zlib - zlib compression library\n"
                                         "gtk4  GTK - Multi-platform toolkit\n"
                                         "zlib  duplicate\n"
                                         "\n"
                                         "bare\n");
    ASSERT_EQ(mods.size(), 3u);
    EXPECT_EQ(mods[0].name, "bare");
    EXPECT_EQ(mods[0].description, "");
    EXPECT_EQ(mods[1].name, "gtk4");
    EXPECT_EQ(mods[2].description, "zlib - zlib compression library");
}

TEST(PkgConfig, ListsInstalledModules) {
    // gtk4 is a build dependency of Kev-C++ itself, so it is always installed here.
    const auto mods = listPkgConfigModules();
    EXPECT_TRUE(std::any_of(mods.begin(), mods.end(), [](const auto& m) { return m.name == "gtk4"; }));
}

TEST(CacheSize, HumanSize) {
    EXPECT_EQ(humanSize(0), "0 B");
    EXPECT_EQ(humanSize(1023), "1023 B");
    EXPECT_EQ(humanSize(1536), "1.5 KB");
    EXPECT_EQ(humanSize(5u * 1024 * 1024), "5.0 MB");
    EXPECT_EQ(humanSize(3ull * 1024 * 1024 * 1024 / 2), "1.5 GB");
}

TEST(CacheSize, DirectorySizeCountsFilesRecursively) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-size-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    EXPECT_EQ(directorySize(dir), 0u); // missing directory
    fs::create_directories(dir / "a/b");
    std::ofstream(dir / "one") << std::string(100, 'x');
    std::ofstream(dir / "a/b/two") << std::string(250, 'y');
    fs::create_symlink(dir / "one", dir / "link"); // not double counted
    EXPECT_EQ(directorySize(dir), 350u);
    fs::remove_all(dir);
}

TEST(TargetDetect, ParsesMarkerLines) {
    EXPECT_EQ(parseDetectedTarget("-- KEVC_TARGET fmt::fmt"), std::optional<std::string>("fmt::fmt"));
    EXPECT_EQ(parseDetectedTarget("-- KEVC_TARGET  "), std::nullopt);
    EXPECT_EQ(parseDetectedTarget("-- Configuring done"), std::nullopt);
}

// End-to-end against a local git repository, so it runs offline.
TEST(TargetDetect, FindsLibrariesAndNamespacedAliases) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-detect-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "repo/sub");
    std::ofstream(dir / "repo/CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n"
                                                  "project(widgets CXX)\n"
                                                  "add_library(widgets STATIC w.cpp)\n"
                                                  "add_library(widgets::widgets ALIAS widgets)\n"
                                                  "add_library(Toolkit::core ALIAS widgets)\n"
                                                  "set(pkg_ns Acme CACHE INTERNAL \"\")\n"
                                                  "function(make_alias name)\n"
                                                  "  add_library(${pkg_ns}::${name} ALIAS ${name})\n"
                                                  "endfunction()\n"
                                                  "make_alias(widgets)\n"
                                                  "# add_library(Never::defined ALIAS widgets) is not real\n"
                                                  "add_executable(widget_demo w.cpp)\n"
                                                  "add_subdirectory(sub)\n";
    std::ofstream(dir / "repo/w.cpp") << "int widget() { return 1; }\n";
    std::ofstream(dir / "repo/sub/CMakeLists.txt") << "add_library(gadgets INTERFACE)\n";
    const std::string repo = dir.string() + "/repo";
    ASSERT_EQ(std::system(("git -C '" + repo + "' init -q && git -C '" + repo + "' add . && git -C '" + repo +
                           "' -c user.name=t -c user.email=t@t commit -qm init && git -C '" + repo +
                           "' tag v1").c_str()),
              0);

    fs::create_directories(dir / "probe");
    std::ofstream(dir / "probe/CMakeLists.txt") << targetDetectionProject("widgets", "file://" + repo, "v1");
    const std::string cmd = "cmake -S '" + (dir / "probe").string() + "' -B '" + (dir / "probe/build").string() + "' 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::set<std::string> found;
    std::array<char, 1024> buf{};
    while (fgets(buf.data(), buf.size(), pipe))
        if (auto t = parseDetectedTarget(buf.data())) found.insert(*t);
    EXPECT_EQ(pclose(pipe), 0);
    fs::remove_all(dir);

    EXPECT_EQ(found, (std::set<std::string>{"widgets", "widgets::widgets", "Toolkit::core", "Acme::widgets", "gadgets"}));
}
