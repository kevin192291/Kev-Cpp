#pragma once

#include "build/BuildRunner.h"
#include "core/Packages.h"
#include "core/Project.h"
#include "core/Toolchain.h"

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace kevc {

// Project Options (Alt+P): General, Compiler, Linker and Packages tabs, like Dev-C++'s dialog.
// Edits a copy of the project; `onApply` receives it when the user presses OK.
class ProjectOptionsDialog : public Gtk::Window {
public:
    enum class Page { General, Compiler, Linker, Packages };
    using ApplyHandler = std::function<void(const Project&)>;

    ProjectOptionsDialog(Gtk::Window& parent, const Project& project, const Toolchain& toolchain, Page start,
                         ApplyHandler onApply);

private:
    Gtk::Widget& buildGeneral();
    Gtk::Widget& buildCompiler();
    Gtk::Widget& buildLinker();
    Gtk::Widget& buildPackages();
    Gtk::Widget& buildCatalogPage();
    Gtk::Widget& buildGitPage();
    Gtk::Widget& buildSystemPage();

    void addPackage(Package package);
    void refreshInstalled();
    void refreshAddButtons();
    void loadSystemModules();
    void detectTargets();
    void refreshCacheSize();
    void cleanCache();
    void addGitPackage();
    void apply();

    Project project_;
    const Toolchain& toolchain_;
    ApplyHandler onApply_;
    std::vector<CatalogEntry> catalog_;

    Gtk::Notebook notebook_;

    // General
    Gtk::DropDown* targetType_ = nullptr;
    Gtk::DropDown* standard_ = nullptr;
    std::vector<std::string> standards_;
    Gtk::Entry outputName_;

    // Compiler
    Gtk::TextView defines_;
    Gtk::TextView includeDirs_;
    std::vector<std::pair<std::string, Gtk::CheckButton*>> warningFlags_;
    Gtk::Entry extraFlags_;
    Gtk::DropDown* debugOpt_ = nullptr;
    Gtk::DropDown* releaseOpt_ = nullptr;
    std::vector<std::string> debugOpts_;
    std::vector<std::string> releaseOpts_;

    // Linker
    Gtk::TextView libraries_;
    Gtk::TextView libraryDirs_;
    Gtk::Entry linkerFlags_;
    Gtk::CheckButton staticRuntime_{"Link libstdc++ and libgcc statically (-static-libstdc++ -static-libgcc)"};

    // Packages
    Gtk::ListBox installed_;
    Gtk::ScrolledWindow installedScroller_;
    Gtk::Label installedEmpty_;
    std::vector<std::pair<std::string, Gtk::Button*>> catalogButtons_;
    std::vector<std::pair<std::string, Gtk::Button*>> systemButtons_;
    Gtk::Entry gitName_;
    Gtk::Entry gitUrl_;
    Gtk::Entry gitTag_;
    Gtk::Entry gitTargets_;
    Gtk::Button detect_{"Detect Targets"};
    Gtk::Label gitStatus_;
    Gtk::Label cacheLabel_;
    Gtk::Button cleanCache_{"Clean Package Cache"};
    BuildRunner detector_;
    std::vector<std::string> detected_;
    Gtk::SearchEntry systemSearch_;
    Gtk::ListBox systemList_;
    bool systemLoaded_ = false;
};

} // namespace kevc
