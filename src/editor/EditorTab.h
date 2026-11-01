#pragma once

#include "core/Breakpoints.h"
#include "editor/DocumentPage.h"

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>

#include <filesystem>
#include <map>
#include <optional>
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

    // Breakpoints are gutter marks (1-based lines), one kind per line; they move with the text as
    // lines are edited. Each kind has its own mark category and icon; a mark carries its condition
    // or the values a live breakpoint shows.
    void setBreakpoints(const FileBreakpoints& breakpoints);
    FileBreakpoints breakpoints() const;
    std::optional<Breakpoint> breakpointAt(int line) const;
    // Adds, replaces or (nullopt) removes the breakpoint on a line.
    void setBreakpoint(int line, const std::optional<Breakpoint>& breakpoint);
    void toggleBreakpoint(int line); // a standard breakpoint, or none
    // A live breakpoint's latest values, shown after its line until cleared (and kept on the
    // breakpoint's line as the text above it changes).
    void showLiveValues(int line, const std::string& text);
    void clearLiveValues();
    // What a right-click on a line number shows (the breakpoint kinds), after moving the cursor there.
    void setGutterMenu(const Glib::RefPtr<Gio::MenuModel>& menu);
    // The debugger's current line (1-based), or 0 to clear it.
    void setExecutionLine(int line);
    std::string lineText(int line) const; // 1-based, without the newline

    // Find support; returns true if a match was selected. `incremental` searches from the start of
    // the current selection so the match under the cursor stays selected while typing.
    bool find(const Glib::ustring& text, bool forward, bool caseSensitive, bool incremental = false);
    int matchCount() const;
    void clearSearch();
    // Replace: the selected match (when the selection is one) becomes `with`, then the next match is
    // selected; false when there's nothing to replace or find. replaceAll returns how many it replaced
    // (one undo step).
    bool replace(const Glib::ustring& needle, const Glib::ustring& with, bool caseSensitive);
    int replaceAll(const Glib::ustring& needle, const Glib::ustring& with, bool caseSensitive);
    // New text for the whole buffer as one undo step (Find in Files' Replace All in an open file),
    // keeping the cursor's line and the breakpoints.
    void replaceText(const std::string& contents);

    sigc::signal<void()>& signalCloseRequested() override { return closeRequested_; }
    sigc::signal<void(bool)>& signalModifiedChanged() { return modifiedChanged_; }
    sigc::signal<void(int)>& signalMatchCountChanged() { return matchCountChanged_; }
    sigc::signal<void()>& signalSaved() { return saved_; }
    // A breakpoint was added, changed or (nullopt) removed: the gutter, toggleBreakpoint() or setBreakpoint().
    sigc::signal<void(int line, std::optional<Breakpoint>)>& signalBreakpointChanged() { return breakpointChanged_; }

private:
    static void onModifiedChanged(GtkTextBuffer* buffer, gpointer self);
    // Reloads the file when it changes on disk, as long as the buffer has no unsaved edits.
    void onFileChanged(Gio::FileMonitor::Event event);
    void watchFile();
    void applyIndenter(); // the C/C++ one for C/C++ files
    void reloadFromDisk();
    void setTextKeepingPlace(const std::string& contents, bool undoable);
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
    sigc::signal<void(int, std::optional<Breakpoint>)> breakpointChanged_;
    void removeBreakpointMarks(int line); // and their live values
    void placeLiveLabels();
    void showGutterMenu(int line); // 0-based
    GtkPopover* gutterPopover_ = nullptr;
    std::map<GtkTextMark*, Gtk::Label*> liveLabels_; // on live breakpoints' marks
    std::vector<Gtk::Label*> spareLiveLabels_;        // hidden, for reuse (see retireLiveLabel)
    void retireLiveLabel(Gtk::Label* label);
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
