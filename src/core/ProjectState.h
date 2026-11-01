#pragma once

#include "core/Project.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace kevc {

// What a project's debugging set-up was when it was last open: breakpoints (file -> 1-based lines)
// and watch expressions. Not in the .kevc: they change all the time and are yours, not the
// project's (and web projects have no .kevc).
struct ProjectState {
    std::map<std::filesystem::path, std::set<int>> breakpoints; // absolute paths
    std::vector<std::string> watches;
    ProjectUi ui; // only for projects without a .kevc to keep it in (web folders)

    bool operator==(const ProjectState&) const = default;
};

// Every project's state in one file, $XDG_STATE_HOME/kev-cpp/projects.json (~/.local/state/...),
// keyed by the project folder's path. Files inside the project are stored relative to it, files
// elsewhere (a library header) by their full path. Breakpoints in files that are gone are dropped on
// load.
class ProjectStateStore {
public:
    ProjectStateStore() = default; // in memory only
    explicit ProjectStateStore(std::filesystem::path file);
    static std::filesystem::path defaultFile();

    ProjectState load(const std::filesystem::path& root) const;
    // Saves at once (the file is small); an empty state removes the project's entry. Keeps the most
    // recently saved `limit` projects.
    void save(const std::filesystem::path& root, const ProjectState& state, size_t limit = 100);

private:
    void write() const;

    std::filesystem::path file_;
    nlohmann::json data_ = nlohmann::json::object();
    long long counter_ = 0; // orders entries by when they were saved
};

} // namespace kevc
