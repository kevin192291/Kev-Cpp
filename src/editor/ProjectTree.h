#pragma once

#include "editor/ProjectTree.ui.h"

#include "core/GitStatus.h"
#include "process/BackgroundJob.h"

#include <gtkmm.h>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace kevc {

// The Project tab: a live file tree of the project root (updates itself as files change on disk),
// with a search field on top that lists the files matching what's typed (Ctrl+P). Its context menu
// and drag and drop only ask for file operations; MainWindow does them, since it knows which files
// are open in editors. The layout and the right-click menu are ProjectTree.ui (edit them on Kev-C++'s
// Design page); the tree's rows are made here.
class ProjectTree : public ProjectTreeUi {
public:
    static Glib::RefPtr<ProjectTree> create() { return ProjectTreeUi::create<ProjectTree>(); }
    ProjectTree(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);
    ~ProjectTree() override;

    // An empty path shows the "no project" placeholder.
    void setRoot(const std::filesystem::path& root);
    // Go to File: the cursor in the search field, its text selected.
    void focusSearch();
    // Escape in an empty search field: back to the editor.
    sigc::signal<void()>& signalSearchDone() { return searchDone_; }

    sigc::signal<void(const std::filesystem::path&)>& signalFileActivated() { return fileActivated_; }
    // From the context menu (and F2 / Delete on the selected row).
    sigc::signal<void(const std::filesystem::path&)>& signalRename() { return rename_; }
    sigc::signal<void(const std::filesystem::path&)>& signalDelete() { return delete_; }
    // The folder a new file or folder goes in.
    sigc::signal<void(const std::filesystem::path& dir)>& signalNewFile() { return newFile_; }
    sigc::signal<void(const std::filesystem::path& dir)>& signalNewFolder() { return newFolder_; }
    // A row was dragged onto a folder (or onto a file in it, or the empty space below the rows: the root).
    sigc::signal<void(const std::filesystem::path& source, const std::filesystem::path& targetDir)>& signalMove() {
        return move_;
    }

    // Files with changes git hasn't committed (absolute paths as the tree spells them), and the
    // folders that hold one: each gets an icon after its name. Empty for a folder outside git.
    void setGitChanges(std::map<std::string, git::FileState> files, std::set<std::string> folders);

protected:
    void on_search_changed() override { onSearchChanged(); }
    void on_search_activate() override { openSelectedResult(); }
    void on_search_stop_search() override;

private:
    struct BoundRow {
        std::string path;
        bool folder = false;
        Gtk::Image* git = nullptr;
    };

    Glib::RefPtr<Gio::ListModel> directoryModel(const Glib::RefPtr<Gio::File>& dir);
    void onSetupItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onBindItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onUnbindItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onActivate(guint position);
    void showGitState(const BoundRow& row) const;

    // Context menu: the row it was opened on (none: the empty space, meaning the root folder).
    void setupContextMenu();
    void showContextMenu(Gtk::ListItem* item, double x, double y);
    Gtk::ListItem* itemAt(double x, double y); // the row under a point in list_ coordinates
    // The folder new files go in for the menu's row: the row itself if it's a folder, else its folder.
    std::filesystem::path menuFolder() const;
    void expandMenuRow();
    // Drag and drop: a row's own drag source and drop target.
    void setupDrag(Gtk::Widget& row, Gtk::ListItem* item);
    bool dropOn(const Glib::ValueBase& value, const std::filesystem::path& targetDir);

    // Search: every file in the project (listed in the background when a search starts), ranked.
    void onSearchChanged();
    void listFiles();
    void showResults();
    void moveResultSelection(int step);
    bool openSelectedResult();

    std::filesystem::path root_;
    std::map<Gtk::ListBoxRow*, std::string> resultPaths_;
    std::vector<std::string> files_; // relative to root_
    bool filesListed_ = false;
    bool searching_ = false; // the field has text; a new search lists the files again
    BackgroundJob lister_;
    sigc::signal<void()> searchDone_;
    Glib::RefPtr<Gtk::TreeListModel> tree_;
    Glib::RefPtr<Gtk::SingleSelection> selection_;
    Glib::RefPtr<Gtk::Filter> filter_;
    Glib::RefPtr<Gtk::Sorter> sorter_;
    sigc::signal<void(const std::filesystem::path&)> fileActivated_, rename_, delete_, newFile_, newFolder_;
    sigc::signal<void(const std::filesystem::path&, const std::filesystem::path&)> move_;
    // Swaps a folder row's icon between open and closed; one per bound row, since GTK reuses rows.
    std::map<Gtk::ListItem*, sigc::connection> expandWatch_;
    std::map<Gtk::Widget*, Gtk::ListItem*> rowItems_; // each row widget's list item, for right-clicks

    std::map<Gtk::ListItem*, BoundRow> bound_; // the rows on screen, to update when git's view changes
    std::map<std::string, git::FileState> gitFiles_;
    std::set<std::string> gitFolders_;

    Gtk::PopoverMenu menu_; // shows the design's contextMenu_
    Glib::RefPtr<Gio::SimpleActionGroup> actions_;
    std::optional<BoundRow> menuRow_;              // empty: the root
    Glib::RefPtr<Gtk::TreeListRow> menuTreeRow_;   // to expand a folder that gets a new file
    sigc::connection hoverExpand_;                 // opens a folder a drag rests on
};

} // namespace kevc
