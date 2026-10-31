#pragma once

#include "core/GitStatus.h"

#include <gtkmm.h>

#include <filesystem>
#include <map>
#include <set>

namespace kevc {

// The Project tab: a live file tree of the project root (updates itself as files change on disk).
class ProjectTree : public Gtk::Stack {
public:
    ProjectTree();

    // An empty path shows the "no project" placeholder.
    void setRoot(const std::filesystem::path& root);

    sigc::signal<void(const std::filesystem::path&)>& signalFileActivated() { return fileActivated_; }

    // Files with changes git hasn't committed (absolute paths as the tree spells them), and the
    // folders that hold one: each gets an icon after its name. Empty for a folder outside git.
    void setGitChanges(std::map<std::string, git::FileState> files, std::set<std::string> folders);

private:
    Glib::RefPtr<Gio::ListModel> directoryModel(const Glib::RefPtr<Gio::File>& dir);
    void onSetupItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onBindItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onUnbindItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onActivate(guint position);

    Gtk::Label placeholder_;
    Gtk::ScrolledWindow scroller_;
    Gtk::ListView list_;
    Glib::RefPtr<Gtk::TreeListModel> tree_;
    Glib::RefPtr<Gtk::SingleSelection> selection_;
    Glib::RefPtr<Gtk::Filter> filter_;
    Glib::RefPtr<Gtk::Sorter> sorter_;
    sigc::signal<void(const std::filesystem::path&)> fileActivated_;
    // Swaps a folder row's icon between open and closed; one per bound row, since GTK reuses rows.
    std::map<Gtk::ListItem*, sigc::connection> expandWatch_;

    struct BoundRow {
        std::string path;
        bool folder = false;
        Gtk::Image* git = nullptr;
    };
    void showGitState(const BoundRow& row) const;
    std::map<Gtk::ListItem*, BoundRow> bound_; // the rows on screen, to update when git's view changes
    std::map<std::string, git::FileState> gitFiles_;
    std::set<std::string> gitFolders_;
};

} // namespace kevc
