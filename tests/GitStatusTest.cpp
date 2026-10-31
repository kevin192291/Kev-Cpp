#include "core/GitStatus.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>
#include <unistd.h>

using namespace kevc::git;
namespace fs = std::filesystem;

TEST(GitStatus, ReadsPorcelainOutput) {
    using namespace std::string_literals;
    const std::string out = " M src/main.cpp\0"s
                            "M  src/staged.cpp\0"s
                            "MM both.cpp\0"s
                            "A  src/new/added.h\0"s
                            "?? notes/todo.txt\0"s
                            " D gone.c\0"s
                            "R  src/renamed.cpp\0src/old name.cpp\0"s
                            "UU merge.cpp\0"s
                            "AA both-added.cpp\0"s;
    const Changes c = parseStatus(out);
    EXPECT_EQ(c.at("src/main.cpp"), FileState::Modified);
    EXPECT_EQ(c.at("src/staged.cpp"), FileState::Modified); // staged, still not committed
    EXPECT_EQ(c.at("both.cpp"), FileState::Modified);
    EXPECT_EQ(c.at("src/new/added.h"), FileState::Added);
    EXPECT_EQ(c.at("notes/todo.txt"), FileState::Untracked);
    EXPECT_EQ(c.at("gone.c"), FileState::Deleted);
    EXPECT_EQ(c.at("src/renamed.cpp"), FileState::Modified);
    EXPECT_FALSE(c.count("src/old name.cpp")); // the rename's old name
    EXPECT_EQ(c.at("merge.cpp"), FileState::Conflicted);
    EXPECT_EQ(c.at("both-added.cpp"), FileState::Conflicted);
    EXPECT_EQ(c.size(), 9u);
    EXPECT_TRUE(parseStatus("").empty());

    EXPECT_EQ(changedFolders(c), (std::set<std::string>{"notes", "src", "src/new"}));
}

// The real git agrees with the parser (git is one of the tools Kev-C++ needs).
TEST(GitStatus, MatchesARealRepository) {
    const fs::path dir = fs::temp_directory_path() / ("kevc-git-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "src");
    auto write = [&](const char* name, const char* text) { std::ofstream(dir / name) << text; };
    write("src/main.cpp", "int main() {}\n");
    write("README.md", "hi\n");
    const std::string git = "git -C '" + dir.string() + "' -c user.name=t -c user.email=t@t ";
    ASSERT_EQ(std::system((git + "init -q && " + git + "add . && " + git + "commit -qm one").c_str()), 0);
    write("src/main.cpp", "int main() { return 0; }\n");
    write("src/extra file.cpp", "\n");

    const std::string cmd = "cd '" + dir.string() + "' && git status --porcelain=v1 -z --untracked-files=all";
    FILE* pipe = popen(cmd.c_str(), "r");
    ASSERT_TRUE(pipe);
    std::string out;
    char buffer[4096];
    for (size_t n; (n = fread(buffer, 1, sizeof buffer, pipe)) > 0;) out.append(buffer, n);
    pclose(pipe);
    fs::remove_all(dir);

    const Changes c = parseStatus(out);
    ASSERT_EQ(c.size(), 2u) << out;
    EXPECT_EQ(c.at("src/main.cpp"), FileState::Modified);
    EXPECT_EQ(c.at("src/extra file.cpp"), FileState::Untracked); // spaces, unquoted thanks to -z
    EXPECT_EQ(statusCommand().front(), "git");
}
