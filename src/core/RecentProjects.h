#pragma once

#include "core/Settings.h"

#include <filesystem>
#include <vector>

namespace kevc {

// The Welcome page's lists: the projects and folders opened lately, and the ones pinned so they stay.
// Kept in the settings file (/recent/projects, /recent/pinned) as absolute paths.
class RecentProjects {
public:
    static constexpr size_t kMaxRecent = 10;

    explicit RecentProjects(Settings& settings) : settings_(settings) {}

    // Pinned folders, in the order they were pinned.
    std::vector<std::filesystem::path> pinned() const;
    // The others, most recent first.
    std::vector<std::filesystem::path> recent() const;
    bool isPinned(const std::filesystem::path& folder) const;

    // A project or folder was opened: it moves to the front of the recent list.
    void opened(const std::filesystem::path& folder);
    void setPinned(const std::filesystem::path& folder, bool pinned);
    // Drops a folder from the recent list (one that no longer exists).
    void forget(const std::filesystem::path& folder);

private:
    std::vector<std::string> list(const char* key) const;
    Settings& settings_;
};

} // namespace kevc
