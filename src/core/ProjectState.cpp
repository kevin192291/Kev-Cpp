#include "core/ProjectState.h"

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace kevc {

namespace {

std::string keyOf(const fs::path& root) {
    fs::path p = root.lexically_normal();
    if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) p = p.parent_path();
    return p.string();
}

} // namespace

ProjectStateStore::ProjectStateStore(fs::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    if (in) {
        try {
            data_ = json::parse(in);
        } catch (const std::exception&) {
            data_ = json::object(); // damaged: start again rather than refuse to save
        }
    }
    if (!data_.is_object()) data_ = json::object();
    for (const auto& [key, entry] : data_.items())
        if (entry.is_object()) counter_ = std::max(counter_, entry.value("saved", 0LL));
}

fs::path ProjectStateStore::defaultFile() {
    const char* state = std::getenv("XDG_STATE_HOME");
    const char* home = std::getenv("HOME");
    const fs::path base = state && *state ? fs::path(state) : fs::path(home ? home : ".") / ".local/state";
    return base / "kev-cpp" / "projects.json";
}

ProjectState ProjectStateStore::load(const fs::path& root) const {
    ProjectState state;
    const auto it = data_.find(keyOf(root));
    if (it == data_.end() || !it->is_object()) return state;
    if (const auto bps = it->find("breakpoints"); bps != it->end() && bps->is_object()) {
        for (const auto& [name, lines] : bps->items()) {
            fs::path file = name;
            if (file.is_relative()) file = root / file;
            std::error_code ec;
            if (!lines.is_array() || !fs::is_regular_file(file, ec)) continue;
            std::set<int> set;
            for (const auto& line : lines)
                if (line.is_number_integer() && line.get<int>() > 0) set.insert(line.get<int>());
            if (!set.empty()) state.breakpoints[file] = std::move(set);
        }
    }
    if (const auto watches = it->find("watches"); watches != it->end() && watches->is_array())
        for (const auto& w : *watches)
            if (w.is_string() && !w.get<std::string>().empty()) state.watches.push_back(w.get<std::string>());
    state.ui = uiFromJson(it->value("ui", json::object()));
    return state;
}

void ProjectStateStore::save(const fs::path& root, const ProjectState& state, size_t limit) {
    const std::string key = keyOf(root);
    json breakpoints = json::object();
    for (const auto& [file, lines] : state.breakpoints) {
        if (lines.empty()) continue;
        const fs::path rel = file.lexically_relative(root);
        const bool inside = !rel.empty() && *rel.begin() != "..";
        breakpoints[inside ? rel.generic_string() : file.string()] = json(lines);
    }
    const json ui = uiToJson(state.ui);
    if (breakpoints.empty() && state.watches.empty() && state.ui.empty()) {
        if (!data_.contains(key)) return;
        data_.erase(key);
    } else {
        json entry = {{"breakpoints", breakpoints}, {"watches", state.watches}, {"saved", ++counter_}};
        if (!state.ui.empty()) entry["ui"] = ui;
        if (data_.contains(key) && data_[key].value("breakpoints", json::object()) == breakpoints &&
            data_[key].value("watches", json::array()) == json(state.watches) &&
            data_[key].value("ui", json::object()) == ui)
            return; // nothing new: don't rewrite the file
        data_[key] = std::move(entry);
    }
    // The projects saved longest ago go first.
    while (data_.size() > limit) {
        auto oldest = data_.begin();
        for (auto it = data_.begin(); it != data_.end(); ++it)
            if (it->value("saved", 0LL) < oldest->value("saved", 0LL)) oldest = it;
        data_.erase(oldest);
    }
    write();
}

void ProjectStateStore::write() const {
    if (file_.empty()) return;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    // Write a copy and rename it over, so a crash mid-write can't leave half a file.
    const fs::path tmp = file_.string() + ".kevc-tmp";
    {
        std::ofstream out(tmp);
        if (!out) return;
        out << data_.dump(2) << '\n';
        if (!out) return;
    }
    fs::rename(tmp, file_, ec);
}

} // namespace kevc
