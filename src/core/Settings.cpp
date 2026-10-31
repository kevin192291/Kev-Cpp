#include "core/Settings.h"

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace kevc {

Settings::Settings(fs::path file) : file_(std::move(file)) {}

fs::path Settings::defaultFile() {
    fs::path config;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) config = xdg;
    else if (const char* home = std::getenv("HOME")) config = fs::path(home) / ".config";
    return config / "kev-cpp" / "settings.json";
}

void Settings::load() {
    data_ = nlohmann::json::object();
    std::ifstream in(file_);
    if (!in) return;
    try {
        nlohmann::json parsed = nlohmann::json::parse(in);
        if (parsed.is_object()) data_ = std::move(parsed);
    } catch (const std::exception&) {
        // Damaged: start from the defaults; the next set() rewrites the file.
    }
}

void Settings::set(const std::string& pointer, const nlohmann::json& value) {
    try {
        data_[nlohmann::json::json_pointer(pointer)] = value;
    } catch (const std::exception&) {
        return; // e.g. a parent that holds a string; leave the settings as they were
    }
    save();
}

void Settings::remove(const std::string& pointer) {
    try {
        const nlohmann::json::json_pointer ptr(pointer);
        if (!data_.contains(ptr)) return;
        data_.at(ptr.parent_pointer()).erase(ptr.back());
    } catch (const std::exception&) {
        return;
    }
    save();
}

void Settings::save() const {
    if (file_.empty()) return;
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    // Write a temp file and rename it, so a crash never leaves half a settings file.
    fs::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp);
        if (!out) return;
        out << data_.dump(2) << '\n';
    }
    fs::rename(tmp, file_, ec);
}

} // namespace kevc
