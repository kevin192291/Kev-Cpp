#pragma once

#include "ui/ProjectOptionsDialog.ui.h"

#include "build/BuildRunner.h"
#include "core/Packages.h"
#include "core/Project.h"
#include "core/Toolchain.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace kevc {

// Project Options (Alt+P): General, Compiler, Linker and Packages tabs, like Dev-C++'s dialog.
// Edits a copy of the project; `onApply` receives it when the user presses OK. Its layout is
// ProjectOptionsDialog.ui (edit it on Kev-C++'s Design page); this class fills it from the project,
// makes the package lists, and shows the parts that apply to C or C++. It deletes itself when closed.
class ProjectOptionsDialog : public ProjectOptionsDialogUi {
public:
    enum class Page { General, Compiler, Linker, Packages };
    using ApplyHandler = std::function<void(const Project&)>;

    static ProjectOptionsDialog* create(Gtk::Window& parent, const Project& project, const Toolchain& toolchain,
                                        Page start, ApplyHandler onApply) {
        return ProjectOptionsDialogUi::create<ProjectOptionsDialog>(parent, project, toolchain, start,
                                                                    std::move(onApply));
    }
    ProjectOptionsDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                         const Project& project, const Toolchain& toolchain, Page start, ApplyHandler onApply);

protected:
    void on_sources_page_changed() override;
    void on_git_url_changed() override;
    void on_detect_clicked() override;
    void on_add_git_clicked() override;
    void on_system_search_changed() override;
    void on_clean_cache_clicked() override;
    void on_cancel_clicked() override;
    void on_ok_clicked() override;

private:
    void fillGeneral();
    void fillCompiler();
    void fillLinker();
    void fillPackages();
    void fillCatalog();

    void addPackage(Package package);
    void refreshInstalled();
    void refreshAddButtons();
    void loadSystemModules();
    void refreshCacheSize();
    void apply();

    Project project_;
    const Toolchain& toolchain_;
    ApplyHandler onApply_;
    std::vector<CatalogEntry> catalog_;

    // The values behind the drop-downs' choices, in their order.
    std::vector<std::string> standards_;
    std::vector<std::string> debugOpts_;
    std::vector<std::string> releaseOpts_;
    // The warning check boxes and the flags they stand for.
    std::vector<std::pair<std::string, Gtk::CheckButton*>> warningFlags_;

    std::vector<std::pair<std::string, Gtk::Button*>> catalogButtons_;
    std::vector<std::pair<std::string, Gtk::Button*>> systemButtons_;
    BuildRunner detector_;
    std::vector<std::string> detected_;
    bool systemLoaded_ = false;
};

} // namespace kevc
