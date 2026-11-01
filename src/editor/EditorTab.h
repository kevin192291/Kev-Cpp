#pragma once

#include "editor/DocumentPage.h"

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>

#include <filesystem>
#include <vector>

namespace kevc {

struct LspDiagnostic;

// One open file: a GtkSourceView in a scroller, plus the widget shown on its notebook tab.
class EditorTab : public Gtk::ScrolledWindow, public DocumentPage {
public:
    // Throws Glib::Error / std::runtime_error if the file cannot be read or is not UTF-8.
    // `cppHeaders` highlights .h files as C++ rather than C.
    EditorTab(const std::filesystem::path& file, bool cppHeaders);
    ~EditorTab() override;

    const std::filesystem::path& path() const override { return path_; }
    bool modified() const override;
    // Throws Glib::Error on failure.
    void save() override;
    void moveTo(const std::filesystem::path& file) override;

    Gtk::Widget& pageWidget() override { return *this; }
    Gtk::Widget& tabLabel() override { return tabBox_; }
    void focusPage() override { focusEditor(); }
    GtkSourceView* view() const { return view_; }
    GtkSourceBuffer* buffer() const { return buffer_; }

    void focusEditor();
    void gotoLine(int line, int column = 0); // 1-based line and column
    void activateTextAction(const char* action) override; // "text.undo", "clipboard.copy", ...
    void placeCursor(const GtkTextIter& iter);   // moves, scrolls to and focuses
    // Selects text on a 1-based line given in bytes (as Find in Files reports it), scrolls to and focuses it.
    void selectRange(int line, size_t byteColumn, size_t byteLength);
    // Squiggly underlines for language-server diagnostics (replaces the previous set).
    void showDiagnostics(const std::vector<LspDiagnostic>& diagnostics);

    // Breakpoints are gutter marks (1-based lines); they move with the text as lines are edited.
    void setBreakpoints(const std::vector<int>& lines);
    std::vector<int> breakpoints() const;
    void toggleBreakpoint(int line);
    // The debugger's current line (1-based), or 0 to clear it.
    void setExecutionLine(int line);
    std::string lineText(int line) const; // 1-based, without the newline

    // Find support; returns true if a match was selected. `incremental` searches from the start of
    // the current selection so the match under the cursor stays selected while typing.
    bool find(const Glib::ustring& text, bool forward, bool caseSensitive, bool incremental = false);
    int matchCount() const;
    void clearSearch();

    sigc::signal<void()>& signalCloseRequested() override { return closeRequested_; }
    sigc::signal<void(bool)>& signalModifiedChanged() { return modifiedChanged_; }
    sigc::signal<void(int)>& signalMatchCountChanged() { return matchCountChanged_; }
    sigc::signal<void()>& signalSaved() { return saved_; }
    // A breakpoint was added (true) or removed (false) by clicking the gutter or toggleBreakpoint().
    sigc::signal<void(int line, bool on)>& signalBreakpointToggled() { return breakpointToggled_; }

private:
    static void onModifiedChanged(GtkTextBuffer* buffer, gpointer self);
    // Reloads the file when it changes on disk, as long as the buffer has no unsaved edits.
    void onFileChanged(Gio::FileMonitor::Event event);
    void watchFile();
    void reloadFromDisk();
    static void onMatchCountChanged(GObject* search, GParamSpec*, gpointer self);
    void updateTitle();
    void scrollToCursor();

    std::filesystem::path path_;
    bool cppHeaders_ = true;
    GtkSourceBuffer* buffer_ = nullptr;
    GtkSourceView* view_ = nullptr;
    GtkSourceSearchSettings* searchSettings_ = nullptr;
    GtkSourceSearchContext* search_ = nullptr;
    Glib::RefPtr<Gio::FileMonitor> monitor_;

    Gtk::Box tabBox_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label tabTitle_;
    Gtk::Button tabClose_;

    sigc::signal<void()> closeRequested_;
    sigc::signal<void(bool)> modifiedChanged_;
    sigc::signal<void(int)> matchCountChanged_;
    sigc::signal<void()> saved_;
    sigc::signal<void(int, bool)> breakpointToggled_;
    static void onLineMarkActivated(GtkSourceView*, const GtkTextIter* iter, guint button, GdkModifierType,
                                    gint, gpointer self);
    static void onLineNumberActivated(GtkSourceGutterRenderer* renderer, GtkTextIter* iter, GdkRectangle*,
                                      guint button, GdkModifierType, gint, gpointer self);
    GtkWidget* lineNumbers_ = nullptr; // GtkSourceView's own line-number renderer
    GtkTextTag* errorTag_ = nullptr;
    GtkTextTag* warningTag_ = nullptr;
};

// Applies the light or dark Adwaita scheme depending on the desktop theme.
GtkSourceStyleScheme* preferredStyleScheme();

} // namespace kevc
