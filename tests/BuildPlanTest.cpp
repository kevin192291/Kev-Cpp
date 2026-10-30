#include "core/BuildPlan.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

Project project(ProjectKind kind) {
    Project p;
    p.name = "app";
    p.targetName = "app";
    p.kind = kind;
    return p;
}

class BuildPlanTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("kevc-plan-" + std::to_string(::getpid()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        tools_.gcc = "/usr/bin/gcc";
        tools_.gxx = "/usr/bin/g++";
        tools_.jobs = 8;
    }
    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
    BuildTools tools_;
};

} // namespace

TEST_F(BuildPlanTest, CppConfiguresOnceThenBuilds) {
    tools_.ninja = true;
    auto cmds = buildCommands(project(ProjectKind::Cpp), root_, BuildAction::Build, BuildConfig::Debug, tools_);
    ASSERT_EQ(cmds.size(), 2u);
    const std::string dir = (root_ / "build/debug").string();
    EXPECT_EQ(cmds[0].argv, (std::vector<std::string>{"cmake", "-S", root_.string(), "-B", dir, "-G", "Ninja",
                                                      "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_C_COMPILER=/usr/bin/gcc",
                                                      "-DCMAKE_CXX_COMPILER=/usr/bin/g++",
                                                      "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"}));
    EXPECT_EQ(cmds[1].argv, (std::vector<std::string>{"cmake", "--build", dir, "--parallel", "8"}));

    fs::create_directories(root_ / "build/debug");
    std::ofstream(root_ / "build/debug/CMakeCache.txt") << "";
    cmds = buildCommands(project(ProjectKind::Cpp), root_, BuildAction::Rebuild, BuildConfig::Debug, tools_);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].argv.back(), "--clean-first");
}

TEST_F(BuildPlanTest, CppCleanNeedsConfiguredDir) {
    EXPECT_TRUE(buildCommands(project(ProjectKind::Cpp), root_, BuildAction::Clean, BuildConfig::Release, tools_).empty());
}

TEST_F(BuildPlanTest, CppReleaseUsesReleaseDir) {
    auto cmds = buildCommands(project(ProjectKind::Cpp), root_, BuildAction::Build, BuildConfig::Release, tools_);
    EXPECT_NE(std::find(cmds[0].argv.begin(), cmds[0].argv.end(), "-DCMAKE_BUILD_TYPE=Release"), cmds[0].argv.end());
    EXPECT_EQ(cmds[0].argv[4], (root_ / "build/release").string());
}

TEST_F(BuildPlanTest, CUsesMakeWithGcc) {
    auto p = project(ProjectKind::C);
    auto cmds = buildCommands(p, root_, BuildAction::Build, BuildConfig::Debug, tools_);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].argv, (std::vector<std::string>{"make", "-C", root_.string(), "-j", "8", "CC=/usr/bin/gcc", "DEBUG=1"}));
    cmds = buildCommands(p, root_, BuildAction::Rebuild, BuildConfig::Release, tools_);
    EXPECT_EQ(cmds[0].argv.back(), "-B");
    cmds = buildCommands(p, root_, BuildAction::Clean, BuildConfig::Release, tools_);
    EXPECT_EQ(cmds[0].argv.back(), "clean");
}

TEST_F(BuildPlanTest, WebProjectsTypeCheckWithTsc) {
    auto p = project(ProjectKind::WebTs);
    tools_.node = "/usr/bin/node";
    tools_.tsc = "/tools/node_modules/typescript/bin/tsc";
    EXPECT_TRUE(buildCommands(p, root_, BuildAction::Build, BuildConfig::Debug, tools_).empty()); // no tsconfig
    std::ofstream(root_ / "tsconfig.json") << "{}";
    auto cmds = buildCommands(p, root_, BuildAction::Build, BuildConfig::Debug, tools_);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].argv, (std::vector<std::string>{"/usr/bin/node", "/tools/node_modules/typescript/bin/tsc",
                                                      "--noEmit", "--pretty", "false", "-p", root_.string()}));
    EXPECT_TRUE(buildCommands(p, root_, BuildAction::Clean, BuildConfig::Debug, tools_).empty());
    tools_.tsc.clear();
    EXPECT_TRUE(buildCommands(p, root_, BuildAction::Build, BuildConfig::Debug, tools_).empty());
}

TEST_F(BuildPlanTest, RunnableOutput) {
    auto p = project(ProjectKind::Cpp);
    p.outputName = "hello";
    EXPECT_EQ(runnableOutput(p, root_, BuildConfig::Release), root_ / "build/release/hello");
    p.targetType = TargetType::Static;
    EXPECT_FALSE(runnableOutput(p, root_, BuildConfig::Debug));
    EXPECT_FALSE(runnableOutput(project(ProjectKind::WebTs), root_, BuildConfig::Debug));
}

TEST_F(BuildPlanTest, LinkCompileCommandsNeverReplacesRealFile) {
    EXPECT_FALSE(linkCompileCommands(root_, BuildConfig::Debug)); // nothing built yet

    fs::create_directories(root_ / "build/debug");
    std::ofstream(root_ / "build/debug/compile_commands.json") << "[]";
    EXPECT_TRUE(linkCompileCommands(root_, BuildConfig::Debug));
    EXPECT_TRUE(fs::is_symlink(root_ / "compile_commands.json"));
    EXPECT_EQ(fs::read_symlink(root_ / "compile_commands.json"), fs::path("build/debug/compile_commands.json"));

    fs::remove(root_ / "compile_commands.json");
    std::ofstream(root_ / "compile_commands.json") << "[\"mine\"]";
    EXPECT_TRUE(linkCompileCommands(root_, BuildConfig::Debug));
    EXPECT_FALSE(fs::is_symlink(fs::symlink_status(root_ / "compile_commands.json")));
}

TEST(DisplayCommand, QuotesOnlyWhenNeeded) {
    EXPECT_EQ(displayCommand({"cmake", "--build", "/a b/it's", "-DX=1"}), "cmake --build '/a b/it'\\''s' -DX=1");
}
