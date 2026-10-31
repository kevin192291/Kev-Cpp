#include "core/RecentProjects.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr const char* kRecent = "/recent/projects";
constexpr const char* kPinned = "/recent/pinned";

// One spelling per folder, so ~/a and ~/a/ (or a symlinked path) don't show twice.
std::string key(const fs::path& folder) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(folder, ec), ec);
    if (ec) p = fs::absolute(folder).lexically_normal();
    std::string s = p.string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

std::vector<fs::path> paths(const std::vector<std::string>& list) {
    return {list.begin(), list.end()};
}

} // namespace

std::vector<std::string> RecentProjects::list(const char* pointer) const {
    return settings_.get<std::vector<std::string>>(pointer, {});
}

std::vector<fs::path> RecentProjects::pinned() const {
    return paths(list(kPinned));
}

std::vector<fs::path> RecentProjects::recent() const {
    const auto pins = list(kPinned);
    std::vector<std::string> out;
    for (const auto& p : list(kRecent))
        if (std::find(pins.begin(), pins.end(), p) == pins.end()) out.push_back(p);
    return paths(out);
}

bool RecentProjects::isPinned(const fs::path& folder) const {
    const auto pins = list(kPinned);
    return std::find(pins.begin(), pins.end(), key(folder)) != pins.end();
}

void RecentProjects::opened(const fs::path& folder) {
    const std::string k = key(folder);
    auto recent = list(kRecent);
    std::erase(recent, k);
    recent.insert(recent.begin(), k);
    if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
    settings_.set(kRecent, recent);
}

void RecentProjects::setPinned(const fs::path& folder, bool pinned) {
    const std::string k = key(folder);
    auto pins = list(kPinned);
    const bool has = std::find(pins.begin(), pins.end(), k) != pins.end();
    if (pinned == has) return;
    if (pinned) {
        pins.push_back(k);
    } else {
        std::erase(pins, k);
        // Unpinned, it goes back among the recent ones (at the front: it was just used).
        auto recent = list(kRecent);
        if (std::find(recent.begin(), recent.end(), k) == recent.end()) {
            recent.insert(recent.begin(), k);
            if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
            settings_.set(kRecent, recent);
        }
    }
    settings_.set(kPinned, pins);
}

void RecentProjects::forget(const fs::path& folder) {
    auto recent = list(kRecent);
    std::erase(recent, key(folder));
    settings_.set(kRecent, recent);
}

} // namespace kevc
