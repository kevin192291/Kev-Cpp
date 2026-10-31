#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace kevc {

// IDE-wide preferences, kept in $XDG_CONFIG_HOME/kev-cpp/settings.json. Keys are JSON pointers such
// as "/toolbars/debug/cpp". Keys this version doesn't use are kept on save, so an older Kev-C++
// never erases a newer one's settings.
class Settings {
public:
    Settings() = default; // in memory only: nothing is saved
    explicit Settings(std::filesystem::path file);

    // ~/.config/kev-cpp/settings.json, honouring $XDG_CONFIG_HOME.
    static std::filesystem::path defaultFile();

    // A missing or damaged file just means default settings.
    void load();
    const std::filesystem::path& file() const { return file_; }

    template <typename T>
    T get(const std::string& pointer, T fallback) const {
        try {
            const nlohmann::json::json_pointer ptr(pointer);
            if (data_.contains(ptr)) return data_.at(ptr).get<T>();
        } catch (const std::exception&) {
            // A malformed pointer or a value of the wrong type: use the default.
        }
        return fallback;
    }
    std::string getString(const std::string& pointer, const std::string& fallback = {}) const {
        return get<std::string>(pointer, fallback);
    }
    // Sets the value (creating parent objects) and saves at once: settings change rarely.
    void set(const std::string& pointer, const nlohmann::json& value);
    void remove(const std::string& pointer);

private:
    void save() const;

    std::filesystem::path file_;
    nlohmann::json data_ = nlohmann::json::object();
};

} // namespace kevc
