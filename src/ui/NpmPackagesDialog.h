#pragma once

#include "ui/NpmPackagesDialog.ui.h"

#include "core/NpmPackages.h"

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace kevc {

// Project → Packages for web projects: every dependency in package.json with the version it asks for,
// what's installed and the newest one, a Lock box (exactly this version) and the update rule in plain
// words (~, ^, >=, *); add and remove packages; and where packages come from (the registry, also per
// @scope, for a company's own npm server), saved in the project's .npmrc. Nothing changes until Save.
// Its frame is NpmPackagesDialog.ui (edit it on Kev-C++'s Design page); this class adds a row per
// package and per scope. It deletes itself when closed.
class NpmPackagesDialog : public NpmPackagesDialogUi {
public:
    static NpmPackagesDialog* create(Gtk::Window& parent, std::filesystem::path root, std::string packageManager) {
        return NpmPackagesDialogUi::create<NpmPackagesDialog>(parent, std::move(root), std::move(packageManager));
    }
    NpmPackagesDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                      std::filesystem::path root, std::string packageManager);

    // After Save; `install`: Save and Install was pressed.
    sigc::signal<void(bool install)>& signalSaved() { return saved_; }
    // Log In… for a registry ("" for npm's own): run `npm login` where the user can type.
    sigc::signal<void(const std::string& url)>& signalLogin() { return login_; }

protected:
    void on_login_clicked() override;
    void on_add_scope_clicked() override;
    void on_check_clicked() override;
    void on_add_clicked() override;
    void on_cancel_clicked() override;
    void on_save_clicked() override;
    void on_install_clicked() override;

private:
    struct Row {
        npm::Dependency dep;
        npm::VersionSpec original;
        npm::Updates updates = npm::Updates::Features;
        npm::Updates lastRange = npm::Updates::Features; // what unlocking goes back to
        std::string version;
        std::string spec;
        std::string newest;
        bool added = false, removed = false;
        std::vector<Gtk::Widget*> widgets; // to hide the row when removed
        Gtk::CheckButton* lock = nullptr;
        Gtk::Entry* versionEntry = nullptr;
        Gtk::DropDown* rule = nullptr;
        Gtk::Label* words = nullptr;
        Gtk::Button* newestButton = nullptr;
    };
    struct ScopeRow {
        Gtk::Entry* scope;
        Gtk::Entry* url;
        Gtk::Box* box;
    };

    void fillSource();
    void fillPackages();
    void addRow(npm::Dependency dep, bool added);
    void update(Row& row);
    void addScopeRow(const std::string& scope, const std::string& url);
    npm::Registries registries() const;
    // `npm view` arguments for this registry set-up (what the dialog shows, saved or not).
    std::vector<std::string> registryArgs(const std::string& package) const;
    void runNextCheck();
    void save(bool install);

    std::filesystem::path root_;
    std::string packageManager_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0); // for npm view callbacks
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<ScopeRow> scopes_;
    npm::Registries savedRegistries_;
    std::string npmrcText_;

    int nextGridRow_ = 1; // row 0 holds the design's headings
    std::deque<Row*> checkQueue_;
    int checksRunning_ = 0;
    sigc::signal<void(bool)> saved_;
    sigc::signal<void(const std::string&)> login_;
};

} // namespace kevc
