#include "core/ProcessTree.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using kevc::proc::descendants;

namespace {

void writeStat(const fs::path& root, int pid, const std::string& comm, int ppid) {
    fs::create_directories(root / std::to_string(pid));
    std::ofstream(root / std::to_string(pid) / "stat") << pid << " (" << comm << ") S " << ppid << " " << pid << " 0 0\n";
}

} // namespace

TEST(ProcessTree, FollowsChildrenOfChildren) {
    const fs::path root = fs::temp_directory_path() / ("kevc-proc-" + std::to_string(getpid()));
    fs::remove_all(root);
    writeStat(root, 1, "systemd", 0);
    writeStat(root, 100, "kev-cpp", 1);
    writeStat(root, 200, "node", 100);           // js-debug
    writeStat(root, 300, "node", 200);           // Vite
    writeStat(root, 400, "node (watchdog)", 300); // a comm with spaces and parentheses
    writeStat(root, 500, "bash", 1);             // unrelated
    fs::create_directories(root / "self");       // not a pid
    auto found = descendants(100, root);
    std::sort(found.begin(), found.end());
    EXPECT_EQ(found, (std::vector<int>{200, 300, 400}));
    EXPECT_TRUE(descendants(400, root).empty());
    EXPECT_TRUE(descendants(100, root / "missing").empty());
    fs::remove_all(root);
}

// The real /proc: a shell with a child in a session of its own (as js-debug's watchdog is).
TEST(ProcessTree, FindsRealChildrenInOtherSessions) {
    const pid_t shell = fork();
    ASSERT_GE(shell, 0);
    if (shell == 0) {
        execl("/bin/sh", "sh", "-c", "setsid sleep 30 & sleep 30", static_cast<char*>(nullptr));
        _exit(127);
    }
    std::vector<int> found;
    for (int tries = 0; tries < 100 && found.size() < 2; ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        found = descendants(shell);
    }
    EXPECT_EQ(found.size(), 2u);
    for (int pid : found) kill(pid, SIGKILL);
    kill(shell, SIGKILL);
    waitpid(shell, nullptr, 0);
}
