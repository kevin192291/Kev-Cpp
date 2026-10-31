#include "core/ProjectKinds.h"

#include "core/Settings.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace kevc {

namespace {

// Toolbar groups and panels that only make sense for some kinds of project; everything else is
// shown for every profile unless the user hides it.
struct Default {
    std::string_view id;
    std::array<std::string_view, 2> profiles; // the profiles it is shown for
};
constexpr std::array kToolbarDefaults{
    Default{"web", {"web", ""}},
};
constexpr std::array kPanelDefaults{
    Default{"dev-server", {"web", ""}},
    Default{"env", {"web", ""}},
    Default{"tests", {"web", ""}},
    Default{"traces", {"web", ""}},
    Default{"deps", {"web", ""}},
};

template <typename Table>
bool shownByDefault(const Table& table, const std::string& id, const std::string& profile) {
    for (const auto& d : table)
        if (d.id == id) return std::find(d.profiles.begin(), d.profiles.end(), profile) != d.profiles.end();
    return true;
}

bool resolve(const Settings& settings, const std::string& key, bool fallback) {
    const std::string v = settings.getString(key, "auto");
    if (v == "shown") return true;
    if (v == "hidden") return false;
    return fallback;
}

// Group and panel ids are plain words, so they're safe as JSON pointer segments.
std::string key(const char* section, const std::string& id, const std::string& profile) {
    return std::string("/") + section + "/" + id + "/" + profile;
}

} // namespace

const KindTraits& traitsOf(ProjectKind kind) {
    static const KindTraits cpp{"C++ · CMake", "cpp", "20", "src/untitled.cpp", "build/\n.cache/\ncompile_commands.json\n"};
    // .cache/ holds clangd's background index; compile_commands.json is generated.
    static const KindTraits c{"C · Make", "c", "17", "src/untitled.c", "build/\n.cache/\ncompile_commands.json\n"};
    static const KindTraits web{"TypeScript", "web", "20", "src/untitled.ts", "node_modules/\n"};
    switch (kind) {
    case ProjectKind::Cpp: return cpp;
    case ProjectKind::C: return c;
    case ProjectKind::WebTs: return web;
    }
    return cpp;
}

std::string profileFor(const Project* project, bool folderOpen) {
    if (project) return traitsOf(project->kind).profile;
    return folderOpen ? "folder" : "none";
}

bool toolbarGroupShown(const Settings& settings, const std::string& group, const std::string& profile) {
    return resolve(settings, key("toolbars", group, profile), shownByDefault(kToolbarDefaults, group, profile));
}

void setToolbarGroupShown(Settings& settings, const std::string& group, const std::string& profile, bool shown) {
    settings.set(key("toolbars", group, profile), shown ? "shown" : "hidden");
}

bool panelShown(const Settings& settings, const std::string& panel, const std::string& profile) {
    return resolve(settings, key("panels", panel, profile), shownByDefault(kPanelDefaults, panel, profile));
}

void setPanelShown(Settings& settings, const std::string& panel, const std::string& profile, bool shown) {
    settings.set(key("panels", panel, profile), shown ? "shown" : "hidden");
}

} // namespace kevc
