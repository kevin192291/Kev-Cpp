#pragma once

#include "core/NpmPackages.h"

#include <gtkmm.h>

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
class NpmPackagesDialog : public Gtk::Window {
public:
    NpmPackagesDialog(Gtk::Window& parent, std::filesystem::path root, std::string packageManager);

    // After Save; `install`: Save and Install was pressed.
    sigc::signal<void(bool install)>& signalSaved() { return saved_; }
    // Log In… for a registry ("" for npm's own): run `npm login` where the user can type.
    sigc::signal<void(const std::string& url)>& signalLogin() { return login_; }

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

    Gtk::Widget& buildSource();
    Gtk::Widget& buildPackages();
    Gtk::Widget& buildAdd();
    void addRow(npm::Dependency dep, bool added);
    void update(Row& row);
    void addScopeRow(const std::string& scope, const std::string& url);
    npm::Registries registries() const;
    // `npm view` arguments for this registry set-up (what the dialog shows, saved or not).
    std::vector<std::string> registryArgs(const std::string& package) const;
    void checkNewest();
    void runNextCheck();
    void addPackage();
    void save(bool install);

    std::filesystem::path root_;
    std::string packageManager_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0); // for npm view callbacks
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<ScopeRow> scopes_;
    npm::Registries savedRegistries_;
    std::string npmrcText_;

    Gtk::Entry source_;
    Gtk::Box scopeList_{Gtk::Orientation::VERTICAL, 4};
    Gtk::Grid grid_;
    int nextGridRow_ = 1;
    Gtk::Button checkButton_{"Check for Newer Versions"};
    Gtk::Label checkStatus_;
    std::deque<Row*> checkQueue_;
    int checksRunning_ = 0;
    Gtk::Entry addName_, addVersion_;
    Gtk::CheckButton addDev_{"Only for development"};
    Gtk::Button addButton_{"Add"};
    Gtk::Label addStatus_;
    Gtk::Label error_;
    sigc::signal<void(bool)> saved_;
    sigc::signal<void(const std::string&)> login_;
};

} // namespace kevc
