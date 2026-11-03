#pragma once

#include "core/Search.h"
#include "process/BackgroundJob.h"

#include "ui/SearchPanel.ui.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace kevc {

// One row of the results: a file (its matches are its children) or one match.
class SearchItem : public Glib::Object {
public:
    static Glib::RefPtr<SearchItem> create() { return Glib::make_refptr_for_instance<SearchItem>(new SearchItem()); }

    std::string rel;                         // the file, relative to the folder searched
    size_t count = 0;                        // file rows: how many matches
    std::optional<search::LineMatch> match;  // match rows
    Glib::RefPtr<Gio::ListStore<SearchItem>> children; // file rows

protected:
    SearchItem() = default;
};

// The left dock's Search tab: finds text in every file of the open project or folder as you type
// (Ctrl+Shift+F), with match case, whole word and regular expression switches and a "files to
// include" filter. Open editors are searched as they are, unsaved changes included. Results are
// grouped by file; clicking a match opens it with the text selected.
class SearchPanel : public SearchPanelUi {
public:
    static Glib::RefPtr<SearchPanel> create() { return SearchPanelUi::create<SearchPanel>(); }
    SearchPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);
    ~SearchPanel() override;

    void setRoot(const std::filesystem::path& root);
    // The cursor in the search field; a non-empty `text` replaces what's there and searches.
    void focusSearch(const Glib::ustring& text = {});
    // Open editors' unsaved text by absolute path.
    void setUnsavedText(std::function<std::map<std::filesystem::path, std::string>()> provider) {
        unsaved_ = std::move(provider);
    }

    sigc::signal<void(const std::filesystem::path& file, int line, size_t column, size_t length)>& signalOpen() {
        return open_;
    }
    // Escape in the search field: back to the editor.
    sigc::signal<void()>& signalDone() { return done_; }
    // Replace All in a file that's open with unsaved changes: its new text, for the editor.
    sigc::signal<void(const std::filesystem::path&, const std::string&)>& signalReplaceInEditor() {
        return replaceInEditor_;
    }

protected:
    void on_options_changed() override; // what to find, or how: search again
    void on_query_stop_search() override;
    void on_replace_all_clicked() override;
    void on_include_changed() override;

private:
    void search();
    void clearResults();
    void addResults(std::vector<search::FileResult> results);
    void finish(const search::Totals& totals);
    void onSetupItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onBindItem(const Glib::RefPtr<Gtk::ListItem>& item);
    void onActivate(guint position);
    void replaceAll(); // asks first
    void doReplaceAll(const std::string& text, const std::string& with);
    std::shared_ptr<const search::Matcher> matcher(const std::string& text) const;

    std::filesystem::path root_;
    Glib::RefPtr<Gio::ListStore<SearchItem>> files_;
    Glib::RefPtr<Gtk::TreeListModel> tree_;
    Glib::RefPtr<Gtk::SingleSelection> selection_;
    sigc::connection includeTimer_;
    // The previous results stay until the new search finds something, so typing doesn't flicker.
    bool replaceResults_ = false;
    size_t shownFiles_ = 0, shownMatches_ = 0;
    std::function<std::map<std::filesystem::path, std::string>()> unsaved_;
    sigc::signal<void(const std::filesystem::path&, int, size_t, size_t)> open_;
    sigc::signal<void()> done_;
    sigc::signal<void(const std::filesystem::path&, const std::string&)> replaceInEditor_;
    BackgroundJob job_; // last: stopped before anything it posts to goes away
};

} // namespace kevc
