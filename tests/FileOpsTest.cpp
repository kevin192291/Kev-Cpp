#include "core/FileOps.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace kevc;

namespace {

class FileOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / ("kevc-fileops-" + std::to_string(::getpid()) + "-" +
                                            ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "src/core");
        fs::create_directories(dir_ / "docs");
        std::ofstream(dir_ / "src/main.cpp") << "int main() {}\n";
        std::ofstream(dir_ / "src/core/a.h") << "// a\n";
    }
    void TearDown() override { fs::remove_all(dir_); }
    fs::path dir_;
};

} // namespace

TEST_F(FileOpsTest, CheckNameRejectsBadAndTakenNames) {
    EXPECT_EQ(fileops::checkName(dir_ / "src", "util.cpp"), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", ""), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", "   "), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", "a/b.cpp"), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", ".."), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", std::string(256, 'x')), "");
    EXPECT_NE(fileops::checkName(dir_ / "src", "main.cpp"), "");   // taken
    EXPECT_NE(fileops::checkName(dir_ / "src", "core"), "");       // a folder takes the name too
    EXPECT_EQ(fileops::checkName(dir_ / "src", "Main.cpp"), "");   // Linux names are case-sensitive
    // Renaming a file to its own name is allowed (and does nothing).
    EXPECT_EQ(fileops::checkName(dir_ / "src", "main.cpp", dir_ / "src/main.cpp"), "");
}

TEST_F(FileOpsTest, CheckMove) {
    EXPECT_EQ(fileops::checkMove(dir_ / "src/main.cpp", dir_ / "docs"), "");
    EXPECT_EQ(fileops::checkMove(dir_ / "src/core", dir_ / "docs"), "");
    // Into itself or one of its own folders.
    EXPECT_NE(fileops::checkMove(dir_ / "src", dir_ / "src"), "");
    EXPECT_NE(fileops::checkMove(dir_ / "src", dir_ / "src/core"), "");
    // A file of the same name is already there.
    std::ofstream(dir_ / "docs/main.cpp") << "\n";
    EXPECT_NE(fileops::checkMove(dir_ / "src/main.cpp", dir_ / "docs"), "");
    // Gone, or the target isn't a folder.
    EXPECT_NE(fileops::checkMove(dir_ / "src/missing.cpp", dir_ / "docs"), "");
    EXPECT_NE(fileops::checkMove(dir_ / "src/core", dir_ / "src/main.cpp"), "");
    // Its own folder: not an error, and sameFolder says so.
    EXPECT_EQ(fileops::checkMove(dir_ / "src/main.cpp", dir_ / "src/"), "");
    EXPECT_TRUE(fileops::sameFolder(dir_ / "src/main.cpp", dir_ / "src/"));
    EXPECT_FALSE(fileops::sameFolder(dir_ / "src/main.cpp", dir_ / "docs"));
}

TEST_F(FileOpsTest, MoveRenamesFilesAndFolders) {
    EXPECT_EQ(fileops::move(dir_ / "src/main.cpp", dir_ / "docs/main.cpp"), "");
    EXPECT_TRUE(fs::exists(dir_ / "docs/main.cpp"));
    EXPECT_FALSE(fs::exists(dir_ / "src/main.cpp"));
    EXPECT_EQ(fileops::move(dir_ / "src/core", dir_ / "src/engine"), "");
    EXPECT_TRUE(fs::exists(dir_ / "src/engine/a.h"));
    EXPECT_NE(fileops::move(dir_ / "src/nothing", dir_ / "src/x"), "");
}

TEST(FileOps, Relocated) {
    const fs::path from = "/p/src/core", to = "/p/lib/core";
    EXPECT_EQ(fileops::relocated("/p/src/core", from, to), to);
    EXPECT_EQ(fileops::relocated("/p/src/core/", from, to), to);
    EXPECT_EQ(fileops::relocated("/p/src/core/x/a.h", from, to), fs::path("/p/lib/core/x/a.h"));
    EXPECT_EQ(fileops::relocated("/p/src/corefile.h", from, to), fs::path()); // not inside, just a longer name
    EXPECT_EQ(fileops::relocated("/p/src/main.cpp", from, to), fs::path());
}

TEST(FileOps, IsInside) {
    EXPECT_TRUE(fileops::isInside("/a/b/c", "/a/b"));
    EXPECT_TRUE(fileops::isInside("/a/b", "/a/b/"));
    EXPECT_FALSE(fileops::isInside("/a/bc", "/a/b"));
    EXPECT_FALSE(fileops::isInside("/a", "/a/b"));
}

TEST(FileOps, StemLength) {
    EXPECT_EQ(fileops::stemLength("main.cpp", false), 4);
    EXPECT_EQ(fileops::stemLength("archive.tar.gz", false), 11);
    EXPECT_EQ(fileops::stemLength(".gitignore", false), 10);
    EXPECT_EQ(fileops::stemLength("Makefile", false), 8);
    EXPECT_EQ(fileops::stemLength("v1.2", true), 4);          // folders: the whole name
    EXPECT_EQ(fileops::stemLength("café.txt", false), 4); // characters, not bytes
}

TEST(FileOps, CdCommand) {
    EXPECT_EQ(fileops::cdCommand("/p/src", "/p"), "cd ./src");
    EXPECT_EQ(fileops::cdCommand("/p/src/app/", "/p/"), "cd ./src/app");
    EXPECT_EQ(fileops::cdCommand("/p", "/p"), "");
    EXPECT_EQ(fileops::cdCommand("/p/src/", "/p/src"), "");
    EXPECT_EQ(fileops::cdCommand("/p", "/p/src"), "cd /p");        // up: the full path
    EXPECT_EQ(fileops::cdCommand("/q/lib", "/p"), "cd /q/lib");
    EXPECT_EQ(fileops::cdCommand("/p/srcx", "/p/src"), "cd /p/srcx"); // not inside, just a longer name
    EXPECT_EQ(fileops::cdCommand("/p/src", ""), "cd /p/src");      // where the shell is isn't known
    EXPECT_EQ(fileops::cdCommand("/p/my dir", "/p"), "cd './my dir'");
    EXPECT_EQ(fileops::cdCommand("/x/it's", "/p"), "cd '/x/it'\\''s'");
    EXPECT_EQ(fileops::cdCommand("/p/café", "/p"), "cd './café'");
}
