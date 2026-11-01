#pragma once

#include "core/Search.h"
#include "process/BackgroundJob.h"

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace kevc {

// The search field in the middle of the menu bar row, where anyone looks for one: typing shows a
// list under it of the project's files whose names match and the first places the text appears,
// with a last row that opens every result in the Search tab. Ctrl+P puts the cursor in it.
class QuickSearch : public Gtk::Box {
public:
    QuickSearch();
    ~QuickSearch() override;

    void setRoot(const std::filesystem::path& root);
    void focusSearch();
    // Open editors' unsaved text by absolute path, searched instead of the files on disk.
    void setUnsavedText(std::function<std::map<std::filesystem::path, std::string>()> provider) {
        unsaved_ = std::move(provider);
    }

    sigc::signal<void(const std::filesystem::path&)>& signalOpenFile() { return openFile_; }
    sigc::signal<void(const std::filesystem::path&, int line, size_t column, size_t length)>& signalOpenMatch() {
        return openMatch_;
    }
    // "All results": the text, for the Search tab.
    sigc::signal<void(const Glib::ustring&)>& signalShowAll() { return showAll_; }
    // Escape: back to the editor.
    sigc::signal<void()>& signalDone() { return done_; }

private:
    struct Row {
        enum class Kind { File, Match, ShowAll } kind = Kind::File;
        std::string rel;
        search::LineMatch match;
    };

    void onChanged();
    void listFiles();
    void searchText();
    void rebuild();
    void addHeading(const char* text);
    void addNote(const Glib::ustring& text);
    Gtk::ListBoxRow& addRow(Gtk::Widget& content, Row row);
    void moveSelection(int step);
    void activate(const Row& row);
    void close();
    void onRealize(); // watches the window for clicks and focus elsewhere

    std::filesystem::path root_;
    Gtk::SearchEntry entry_;
    Gtk::Popover popover_;
    Gtk::ScrolledWindow scroller_;
    Gtk::ListBox list_;
    std::map<Gtk::ListBoxRow*, Row> rows_;
    std::vector<std::string> files_; // relative to root_
    bool filesListed_ = false, listing_ = false;
    std::string textFor_; // the text textMatches_ were found for
    std::vector<std::pair<std::string, search::LineMatch>> textMatches_;
    bool textDone_ = false;
    sigc::connection textTimer_;
    Gtk::Window* watching_ = nullptr;
    std::function<std::map<std::filesystem::path, std::string>()> unsaved_;
    sigc::signal<void(const std::filesystem::path&)> openFile_;
    sigc::signal<void(const std::filesystem::path&, int, size_t, size_t)> openMatch_;
    sigc::signal<void(const Glib::ustring&)> showAll_;
    sigc::signal<void()> done_;
    BackgroundJob lister_, texter_; // last: stopped before what they post to goes away
};

} // namespace kevc
