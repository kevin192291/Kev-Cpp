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

// ---- pkg-config libraries that aren't where they say, CMake packages, SFML ----------------------

namespace {

struct Prefix {
    fs::path root = fs::temp_directory_path() / ("kevc-prefix-" + std::to_string(::getpid()));
    Prefix() { fs::remove_all(root); }
    ~Prefix() { fs::remove_all(root); }
    void file(const std::string& rel, const std::string& text = "") const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << text;
    }
};

Package systemPackage(const std::string& name) {
    return Package{Package::Source::System, name, {}, {}, {}, {}};
}

} // namespace

TEST(PkgConfig, FindsLibrariesMissingFromTheFoldersTheLineNames) {
    Prefix p;
    p.file("lib/libsfml-graphics-s.a");
    p.file("lib/libsfml-window.so");
    p.file("lib/libfreetype.a");
    const std::string L = "-L" + (p.root / "lib").string();
    // A static-only SFML 3: its .pc says -lsfml-graphics, the folder has libsfml-graphics-s.a.
    EXPECT_EQ(missingPkgConfigLibs(L + " -lsfml-graphics -lsfml-window -lsfml-system -lsfml-graphics"),
              (std::vector<std::string>{"sfml-graphics", "sfml-system"}));
    EXPECT_TRUE(missingPkgConfigLibs(L + " -lsfml-window -lfreetype -pthread").empty());
    // No -L: the libraries are in the system's own folders, which only the linker knows.
    EXPECT_TRUE(missingPkgConfigLibs("-lgtk-4 -lgobject-2.0").empty());
    EXPECT_TRUE(missingPkgConfigLibs("").empty());
}

TEST(PkgConfig, RunsOnlyForModuleNames) {
    EXPECT_EQ(runPkgConfig("--modversion", {"no-such-module-kevc"}), "");
    EXPECT_EQ(runPkgConfig("--modversion", {"x; echo hi"}), "");
}

TEST(CMakeConfig, FindsTheFirstPackageUnderThePrefixes) {
    Prefix local, usr;
    usr.root += "-usr";
    fs::remove_all(usr.root);
    local.file("lib/cmake/SFML/SFMLConfig.cmake");
    local.file("lib/cmake/SFML/SFMLConfigVersion.cmake", "# generated\nset(PACKAGE_VERSION \"3.0.0\")\nif(x)\n");
    usr.file("lib/x86_64-linux-gnu/cmake/SFML/SFMLConfig.cmake");
    usr.file("lib/x86_64-linux-gnu/cmake/SFML/SFMLConfigVersion.cmake", "set(PACKAGE_VERSION \"2.6.1\")\n");
    usr.file("share/cmake/fmt/fmt-config.cmake");
    usr.file("share/cmake/fmt/fmt-config-version.cmake", "set(PACKAGE_VERSION 11.0.2)\n");

    auto found = findCMakeConfig("SFML", {local.root, usr.root});
    ASSERT_TRUE(found);
    EXPECT_EQ(found->version, "3.0.0");
    EXPECT_EQ(found->major(), 3);
    EXPECT_EQ(found->dir, local.root / "lib/cmake/SFML");

    found = findCMakeConfig("SFML", {usr.root}); // Debian's multiarch folder
    ASSERT_TRUE(found);
    EXPECT_EQ(found->version, "2.6.1");
    EXPECT_EQ(found->major(), 2);

    found = findCMakeConfig("fmt", {local.root, usr.root});
    ASSERT_TRUE(found);
    EXPECT_EQ(found->version, "11.0.2");
    EXPECT_FALSE(findCMakeConfig("Nope", {local.root, usr.root}));
    EXPECT_FALSE(findCMakeConfig("SFML", {usr.root / "missing"}));
    fs::remove_all(usr.root);
}

TEST(Sfml, ComponentsAProjectAsksFor) {
    EXPECT_EQ(sfml::components({systemPackage("sfml-window"), systemPackage("gtk4"), systemPackage("sfml-graphics"),
                                systemPackage("sfml-system")}),
              (std::vector<std::string>{"Graphics", "Window", "System"}));
    EXPECT_EQ(sfml::components({systemPackage("sfml-all")}).size(), 5u);
    Package cmake;
    cmake.source = Package::Source::CMake;
    cmake.name = "SFML";
    cmake.components = {"Audio"};
    cmake.targets = {"SFML::Graphics"};
    EXPECT_EQ(sfml::components({cmake}), (std::vector<std::string>{"Graphics", "Audio"}));
    EXPECT_TRUE(sfml::components({systemPackage("gtk4")}).empty());
}

TEST(Sfml, Version3WithACMakePackageIsFoundThroughIt) {
    Prefix p;
    p.file("lib/cmake/SFML/SFMLConfig.cmake");
    p.file("lib/cmake/SFML/SFMLConfigVersion.cmake", "set(PACKAGE_VERSION \"3.0.0\")\n");
    p.file("lib/cmake/SFML/SFMLSystemStaticTargets.cmake");
    sfml::Install install{findCMakeConfig("SFML", {p.root}), "3.0.0"};
    EXPECT_EQ(install.major(), 3);

    auto packages = sfml::packagesFor(install, {"audio", "Graphics"});
    ASSERT_EQ(packages.size(), 1u);
    EXPECT_EQ(packages[0].source, Package::Source::CMake);
    EXPECT_EQ(packages[0].name, "SFML");
    EXPECT_EQ(packages[0].version, "3");
    EXPECT_EQ(packages[0].components, (std::vector<std::string>{"Graphics", "Audio"}));
    EXPECT_EQ(packages[0].targets, (std::vector<std::string>{"SFML::Graphics", "SFML::Audio"}));
    // Static libraries only: SFML's package has to be told, or it looks for shared ones and fails.
    EXPECT_EQ(packages[0].options, (std::map<std::string, std::string>{{"SFML_STATIC_LIBRARIES", "ON"}}));

    p.file("lib/cmake/SFML/SFMLSystemSharedTargets.cmake");
    EXPECT_TRUE(sfml::packagesFor(install, {"Graphics"})[0].options.empty());
}

TEST(Sfml, Version2ComesFromPkgConfigAndNoSfmlIsFetched) {
    sfml::Install two{std::nullopt, "2.6.1"};
    EXPECT_EQ(two.major(), 2);
    EXPECT_EQ(sfml::packagesFor(two, {"Graphics", "Audio"}),
              (std::vector<Package>{systemPackage("sfml-graphics"), systemPackage("sfml-audio")}));
    // SFML 2's own CMake package has other target names: pkg-config works for it.
    Prefix p;
    p.file("lib/cmake/SFML/SFMLConfig.cmake");
    p.file("lib/cmake/SFML/SFMLConfigVersion.cmake", "set(PACKAGE_VERSION \"2.6.1\")\n");
    two.config = findCMakeConfig("SFML", {p.root});
    EXPECT_EQ(two.major(), 2);
    EXPECT_EQ(sfml::packagesFor(two, {"Graphics"}), (std::vector<Package>{systemPackage("sfml-graphics")}));

    const sfml::Install none;
    EXPECT_FALSE(none.installed());
    EXPECT_EQ(none.major(), 3);
    const auto fetched = sfml::packagesFor(none, {"Graphics"});
    ASSERT_EQ(fetched.size(), 1u);
    EXPECT_EQ(fetched[0].source, Package::Source::Catalog);
    EXPECT_EQ(fetched[0].gitUrl, "https://github.com/SFML/SFML.git");
    EXPECT_EQ(fetched[0].targets, (std::vector<std::string>{"SFML::Graphics"}));
    EXPECT_EQ(fetched[0].options.at("SFML_BUILD_AUDIO"), "OFF");
    EXPECT_EQ(sfml::packagesFor(none, {"Audio"})[0].options.count("SFML_BUILD_AUDIO"), 0u);
}

TEST(Sfml, SetPackagesReplacesTheProjectsSfmlInPlace) {
    Prefix p;
    p.file("lib/cmake/SFML/SFMLConfig.cmake");
    p.file("lib/cmake/SFML/SFMLConfigVersion.cmake", "set(PACKAGE_VERSION \"3.0.0\")\n");
    const sfml::Install install{findCMakeConfig("SFML", {p.root}), "3.0.0"};

    std::vector<Package> packages{systemPackage("zlib"), systemPackage("sfml-graphics"), systemPackage("sfml-audio"),
                                  systemPackage("gtk4")};
    const auto wanted = sfml::packagesFor(install, sfml::components(packages));
    EXPECT_TRUE(sfml::setPackages(packages, wanted));
    ASSERT_EQ(packages.size(), 3u);
    EXPECT_EQ(packages[0].name, "zlib");
    EXPECT_EQ(packages[1], wanted[0]);
    EXPECT_EQ(packages[2].name, "gtk4");
    EXPECT_FALSE(sfml::setPackages(packages, wanted)); // nothing to change

    std::vector<Package> fresh{systemPackage("zlib")};
    EXPECT_TRUE(sfml::setPackages(fresh, wanted));
    EXPECT_EQ(fresh.back(), wanted[0]);
}

TEST(Sfml, PkgConfigModulesWhoseLibrariesAreMissingNeedTheCMakePackage) {
    Prefix p;
    p.file("lib/cmake/SFML/SFMLConfig.cmake");
    p.file("lib/cmake/SFML/SFMLConfigVersion.cmake", "set(PACKAGE_VERSION \"3.0.0\")\n");
    p.file("lib/libsfml-graphics-s.a");
    const sfml::Install install{findCMakeConfig("SFML", {p.root}), "3.0.0"};
    const std::string broken = "-L" + (p.root / "lib").string() + " -lsfml-graphics";
    const std::vector<Package> viaPkgConfig{systemPackage("sfml-graphics")};

    EXPECT_TRUE(sfml::needsCMakePackage(viaPkgConfig, install, broken));
    // Shared libraries where pkg-config says: it works as it is.
    p.file("lib/libsfml-graphics.so");
    EXPECT_FALSE(sfml::needsCMakePackage(viaPkgConfig, install, broken));
    fs::remove(p.root / "lib/libsfml-graphics.so");
    // No SFML 3 CMake package to switch to, or no SFML from pkg-config in the project.
    EXPECT_FALSE(sfml::needsCMakePackage(viaPkgConfig, sfml::Install{std::nullopt, "3.0.0"}, broken));
    EXPECT_FALSE(sfml::needsCMakePackage({systemPackage("gtk4")}, install, broken));
    EXPECT_FALSE(sfml::needsCMakePackage(sfml::packagesFor(install, {"Graphics"}), install, broken));
}
