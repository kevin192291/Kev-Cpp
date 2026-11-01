#include "core/Toolchain.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class ToolchainTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-toolchain-" + std::to_string(::getpid()));
        fs::create_directories(dir_ / "a");
        fs::create_directories(dir_ / "b");
    }
    void TearDown() override { fs::remove_all(dir_); }

    void makeExe(const fs::path& p, bool executable = true) {
        std::ofstream(p) << "#!/bin/sh\n";
        fs::permissions(p, executable ? fs::perms::owner_all : fs::perms::owner_read | fs::perms::owner_write);
    }

    fs::path dir_;
};

} // namespace

TEST_F(ToolchainTest, FindsFirstMatchInPathOrder) {
    makeExe(dir_ / "a" / "g++");
    makeExe(dir_ / "b" / "g++");
    auto found = kevc::findInPath("g++", (dir_ / "a").string() + ":" + (dir_ / "b").string());
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, dir_ / "a" / "g++");
}

TEST(Toolchain, PrependsADirectoryToPath) {
    EXPECT_EQ(kevc::prependToPath("/usr/bin:/bin", "/home/me/.local/bin"), "/home/me/.local/bin:/usr/bin:/bin");
    EXPECT_EQ(kevc::prependToPath("/usr/bin:/home/me/.local/bin", "/home/me/.local/bin"), "/usr/bin:/home/me/.local/bin");
    EXPECT_EQ(kevc::prependToPath("", "/opt/kev/bin"), "/opt/kev/bin");
    EXPECT_EQ(kevc::prependToPath("/usr/bin", ""), "/usr/bin");
}

TEST_F(ToolchainTest, SkipsNonExecutableFiles) {
    makeExe(dir_ / "a" / "gcc", false);
    makeExe(dir_ / "b" / "gcc");
    auto found = kevc::findInPath("gcc", (dir_ / "a").string() + ":" + (dir_ / "b").string());
    ASSERT_TRUE(found);
    EXPECT_EQ(*found, dir_ / "b" / "gcc");
}

TEST_F(ToolchainTest, ReportsMissingRequiredAndOptionalTools) {
    makeExe(dir_ / "a" / "gcc");
    makeExe(dir_ / "a" / "clangd");
    auto tc = kevc::Toolchain::detect((dir_ / "a").string(), false);

    ASSERT_NE(tc.get("gcc"), nullptr);
    EXPECT_TRUE(tc.get("gcc")->found());

    std::vector<std::string> missing;
    for (const auto* t : tc.missingRequired()) missing.push_back(t->name);
    EXPECT_EQ(missing, (std::vector<std::string>{"g++", "cmake", "make", "gdb", "node"}));

    std::vector<std::string> optional;
    for (const auto* t : tc.missingOptional()) optional.push_back(t->name);
    EXPECT_EQ(optional, (std::vector<std::string>{"git", "ninja"}));
}

TEST_F(ToolchainTest, PathOfFallsBackToBareName) {
    auto tc = kevc::Toolchain::detect("", false);
    EXPECT_EQ(tc.pathOf("g++"), fs::path("g++"));
}

TEST(Toolchain, NodeMajorFromVersionLine) {
    EXPECT_EQ(kevc::nodeMajor("v18.19.1"), 18);
    EXPECT_EQ(kevc::nodeMajor("v24.22.1\n"), 24);
    EXPECT_EQ(kevc::nodeMajor(""), 0);
    EXPECT_EQ(kevc::nodeMajor("node: not found"), 0);
}

// A menu or file manager starts the IDE with the session's PATH: Ubuntu's node 18 there, nvm's
// node 24 only in terminals. The IDE should use nvm's.
TEST_F(ToolchainTest, TerminalLikePathFindsAVersionManagersNode) {
    const fs::path home = dir_ / "home";
    for (const char* v : {"v18.20.0", "v24.22.1", "v20.19.0", "v22.12.0"}) {
        fs::create_directories(home / ".nvm/versions/node" / v / "bin");
        makeExe(home / ".nvm/versions/node" / v / "bin/node");
    }
    const std::string session = "/usr/local/bin:/usr/bin:/bin";
    const std::string nvm24 = (home / ".nvm/versions/node/v24.22.1/bin").string();
    // Only this test's folders: the machine running it may have node installed with n (/usr/local/n).
    auto in = [this](const fs::path& h, const fs::path& nvm = {}) {
        return kevc::NodeSearch{h, nvm, {}, dir_ / "no-n"};
    };

    // Node 18 on the PATH (or none at all): the newest nvm node goes first.
    EXPECT_EQ(kevc::terminalLikePath(session, 18, in(home)), nvm24 + ":" + session);
    EXPECT_EQ(kevc::terminalLikePath(session, 0, in(home)), nvm24 + ":" + session);
    // A new enough node on the PATH (a terminal launch with nvm active): left alone.
    EXPECT_EQ(kevc::terminalLikePath(session, 22, in(home)), session);
    // $NVM_DIR elsewhere, and fnm's layout.
    EXPECT_EQ(kevc::terminalLikePath(session, 18, in(dir_ / "nobody", home / ".nvm")), nvm24 + ":" + session);
    const fs::path fnm = dir_ / "fnmhome/.local/share/fnm/node-versions/v22.3.0/installation/bin";
    fs::create_directories(fnm);
    makeExe(fnm / "node");
    EXPECT_EQ(kevc::terminalLikePath(session, 18, in(dir_ / "fnmhome")), fnm.string() + ":" + session);
    // n's layout, under its prefix.
    const fs::path n = dir_ / "nprefix/versions/node/23.1.0/bin";
    fs::create_directories(n);
    makeExe(n / "node");
    EXPECT_EQ(kevc::terminalLikePath(session, 18, kevc::NodeSearch{dir_ / "nobody", {}, {}, dir_ / "nprefix"}),
              n.string() + ":" + session);
    // Nothing newer anywhere: unchanged.
    EXPECT_EQ(kevc::terminalLikePath(session, 18, in(dir_ / "nobody")), session);
}

TEST_F(ToolchainTest, TerminalLikePathAddsPnpmAndBunFolders) {
    const fs::path home = dir_ / "home2";
    fs::create_directories(home / ".local/share/pnpm");
    fs::create_directories(home / ".bun/bin");
    const std::string pnpm = (home / ".local/share/pnpm").string(), bun = (home / ".bun/bin").string();
    const kevc::NodeSearch where{home, {}, {}, dir_ / "no-n"};
    EXPECT_EQ(kevc::terminalLikePath("/usr/bin", 22, where), "/usr/bin:" + pnpm + ":" + bun);
    // Already there: not twice. $PNPM_HOME elsewhere: that one.
    EXPECT_EQ(kevc::terminalLikePath("/usr/bin:" + pnpm, 22, where), "/usr/bin:" + pnpm + ":" + bun);
    fs::create_directories(dir_ / "pnpm-home");
    EXPECT_EQ(kevc::terminalLikePath("/usr/bin", 22, kevc::NodeSearch{home, {}, dir_ / "pnpm-home", dir_ / "no-n"}),
              "/usr/bin:" + (dir_ / "pnpm-home").string() + ":" + bun);
}
