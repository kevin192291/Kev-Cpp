#pragma once

#include "core/Project.h"

#include <string>

namespace kevc {

class Settings;

// Facts about each project kind that are used all over the IDE, in one table, so a new kind is
// mostly a new row here.
struct KindTraits {
    const char* label;       // shown in the title bar, e.g. "C++ · CMake"
    const char* profile;     // key for toolbar and panel defaults (see profileFor)
    const char* standard;    // default language standard
    const char* newFileName; // New Source File suggestion
    const char* gitignore;   // written for new projects
};

const KindTraits& traitsOf(ProjectKind kind);

// What toolbars and panels show by default depends on what is open: "none", "folder" (a plain
// folder), or the project kind's profile ("cpp", "c", "web").
std::string profileFor(const Project* project, bool folderOpen);

// Toolbar groups and panels are Auto (the profile's default), Shown or Hidden, set per profile in
// settings under /toolbars/<group>/<profile> and /panels/<id>/<profile>.
bool toolbarGroupShown(const Settings& settings, const std::string& group, const std::string& profile);
void setToolbarGroupShown(Settings& settings, const std::string& group, const std::string& profile, bool shown);
bool panelShown(const Settings& settings, const std::string& panel, const std::string& profile);
void setPanelShown(Settings& settings, const std::string& panel, const std::string& profile, bool shown);

} // namespace kevc
