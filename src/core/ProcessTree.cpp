#include "core/ProcessTree.h"

#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace kevc::proc {

namespace {

// The parent pid from a stat line: "pid (comm) state ppid …". comm may hold spaces and
// parentheses, so read from the last ')'.
int parentOf(const std::string& stat) {
    const size_t close = stat.rfind(')');
    if (close == std::string::npos) return -1;
    std::istringstream rest(stat.substr(close + 1));
    std::string state;
    int ppid = -1;
    rest >> state >> ppid;
    return rest ? ppid : -1;
}

} // namespace

std::vector<int> descendants(int pid, const std::filesystem::path& procRoot) {
    std::multimap<int, int> children; // parent -> child
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(procRoot, ec)) {
        const std::string name = entry.path().filename().string();
        int child = 0;
        const auto [end, error] = std::from_chars(name.data(), name.data() + name.size(), child);
        if (error != std::errc() || end != name.data() + name.size()) continue;
        std::ifstream in(entry.path() / "stat");
        std::string stat;
        if (!std::getline(in, stat)) continue; // gone meanwhile
        if (const int parent = parentOf(stat); parent > 0) children.emplace(parent, child);
    }
    std::vector<int> out;
    std::vector<int> pending{pid};
    while (!pending.empty()) {
        const int parent = pending.back();
        pending.pop_back();
        for (auto [it, end] = children.equal_range(parent); it != end; ++it) {
            out.push_back(it->second);
            pending.push_back(it->second);
        }
    }
    return out;
}

} // namespace kevc::proc
