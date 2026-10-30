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
