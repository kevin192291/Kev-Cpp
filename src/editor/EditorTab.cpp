#include "editor/EditorTab.h"

#include "editor/CppIndenter.h"

#include "lsp/LspManager.h"

#include <algorithm>
#include <stdexcept>

namespace kevc {

namespace {

constexpr const char* kBreakpointCategory = "kevc-breakpoint";
constexpr const char* kExecutionCategory = "kevc-execution";

GtkSourceLanguage* languageFor(const std::filesystem::path& file, bool cppHeaders) {
    GtkSourceLanguageManager* lm = gtk_source_language_manager_get_default();
    const std::string ext = file.extension().string();
    if (cppHeaders && ext == ".h") return gtk_source_language_manager_get_language(lm, "cpphdr");
    return gtk_source_language_manager_guess_language(lm, file.filename().c_str(), nullptr);
}

} // namespace

GtkSourceStyleScheme* preferredStyleScheme() {
    // Kev-C++'s own scheme (data/styles/kevc-slate.xml), to match the window's Slate palette.
    auto* manager = gtk_source_style_scheme_manager_get_default();
    if (auto* slate = gtk_source_style_scheme_manager_get_scheme(manager, "kevc-slate")) return slate;
    return gtk_source_style_scheme_manager_get_scheme(manager, "Adwaita-dark");
}

EditorTab::EditorTab(const std::filesystem::path& file, bool cppHeaders) : path_(file), cppHeaders_(cppHeaders) {
    std::string contents = Glib::file_get_contents(file.string());
    if (!g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr))
        throw std::runtime_error(file.filename().string() + " is not valid UTF-8 text");

    buffer_ = gtk_source_buffer_new(nullptr);
    gtk_source_buffer_set_language(buffer_, languageFor(file, cppHeaders));
    gtk_source_buffer_set_style_scheme(buffer_, preferredStyleScheme());
    gtk_source_buffer_set_highlight_matching_brackets(buffer_, TRUE);

    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    gtk_text_buffer_begin_irreversible_action(text);
    gtk_text_buffer_set_text(text, contents.data(), static_cast<int>(contents.size()));
    gtk_text_buffer_end_irreversible_action(text);
    gtk_text_buffer_set_modified(text, FALSE);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(text, &start);
    gtk_text_buffer_place_cursor(text, &start);

    view_ = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(buffer_));
    applyIndenter();
    const bool isMakefile = file.filename() == "Makefile" || file.extension() == ".mk";
    gtk_source_view_set_show_line_numbers(view_, TRUE);
    gtk_source_view_set_highlight_current_line(view_, TRUE);
    gtk_source_view_set_auto_indent(view_, TRUE);
    gtk_source_view_set_indent_on_tab(view_, TRUE);
    gtk_source_view_set_tab_width(view_, 4);
    gtk_source_view_set_insert_spaces_instead_of_tabs(view_, !isMakefile);
    gtk_source_view_set_smart_backspace(view_, TRUE);
    gtk_source_view_set_show_line_marks(view_, TRUE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view_), TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(view_), "kevc-editor");
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view_), 4);

    GdkRGBA breakpointBg, executionBg;
    gdk_rgba_parse(&breakpointBg, "rgba(224,27,36,0.18)");
    gdk_rgba_parse(&executionBg, "rgba(246,211,45,0.45)");
    GtkSourceMarkAttributes* bp = gtk_source_mark_attributes_new();
    gtk_source_mark_attributes_set_icon_name(bp, "kevc-breakpoint-dot");
    gtk_source_mark_attributes_set_background(bp, &breakpointBg);
    gtk_source_view_set_mark_attributes(view_, kBreakpointCategory, bp, 10);
    g_object_unref(bp);
    GtkSourceMarkAttributes* exec = gtk_source_mark_attributes_new();
    gtk_source_mark_attributes_set_icon_name(exec, "kevc-exec-arrow");
    gtk_source_mark_attributes_set_background(exec, &executionBg);
    gtk_source_view_set_mark_attributes(view_, kExecutionCategory, exec, 20);
    g_object_unref(exec);
    g_signal_connect(view_, "line-mark-activated", G_CALLBACK(&EditorTab::onLineMarkActivated), this);

    // Clicking a line number toggles a breakpoint, as in Dev-C++. The line-number renderer is
    // private to GtkSourceView, so find it among the gutter's children by type name.
    GtkWidget* gutter = GTK_WIDGET(gtk_source_view_get_gutter(view_, GTK_TEXT_WINDOW_LEFT));
    for (GtkWidget* child = gtk_widget_get_first_child(gutter); child; child = gtk_widget_get_next_sibling(child)) {
        if (g_strcmp0(G_OBJECT_TYPE_NAME(child), "GtkSourceGutterRendererLines") != 0) continue;
        lineNumbers_ = child;
        g_signal_connect(child, "activate", G_CALLBACK(&EditorTab::onLineNumberActivated), this);
        gtk_widget_set_cursor_from_name(child, "pointer");
    }

    searchSettings_ = gtk_source_search_settings_new();
    gtk_source_search_settings_set_wrap_around(searchSettings_, TRUE);
    search_ = gtk_source_search_context_new(buffer_, searchSettings_);

    set_child(*Gtk::manage(Glib::wrap(GTK_TEXT_VIEW(view_))));
    set_vexpand(true);
    set_hexpand(true);

    g_signal_connect(buffer_, "modified-changed", G_CALLBACK(&EditorTab::onModifiedChanged), this);
    watchFile();
    g_signal_connect(search_, "notify::occurrences-count", G_CALLBACK(&EditorTab::onMatchCountChanged), this);

    tabTitle_.set_tooltip_text(file.string());
    tabClose_.set_icon_name("window-close-symbolic");
    tabClose_.set_has_frame(false);
    tabClose_.set_focus_on_click(false);
    tabClose_.set_tooltip_text("Close (Ctrl+W)");
    tabClose_.signal_clicked().connect([this] { closeRequested_.emit(); });
    tabBox_.append(tabTitle_);
    tabBox_.append(tabClose_);
    updateTitle();
}

EditorTab::~EditorTab() {
    if (monitor_) monitor_->cancel();
    g_signal_handlers_disconnect_by_data(view_, this);
    if (lineNumbers_) g_signal_handlers_disconnect_by_data(lineNumbers_, this);
    g_signal_handlers_disconnect_by_data(buffer_, this);
    g_signal_handlers_disconnect_by_data(search_, this);
    g_object_unref(search_);
    g_object_unref(searchSettings_);
    g_object_unref(buffer_);
}

bool EditorTab::modified() const {
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(buffer_));
}

void EditorTab::save() {
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text, &start, &end);
    char* contents = gtk_text_buffer_get_text(text, &start, &end, TRUE);
    std::string data(contents);
    g_free(contents);
    // Keep POSIX text files newline-terminated, as compilers and diff tools expect.
    if (!data.empty() && data.back() != '\n') data += '\n';
    Glib::file_set_contents(path_.string(), data);
    gtk_text_buffer_set_modified(text, FALSE);
    saved_.emit();
}

void EditorTab::applyIndenter() {
    // C and C++ keep Kev-C++'s layout as you type (see core/CppStyle); other files use GtkSourceView's.
    if (isCppLanguage(gtk_source_buffer_get_language(buffer_))) {
        GtkSourceIndenter* indenter = newCppIndenter();
        gtk_source_view_set_indenter(view_, indenter);
        g_object_unref(indenter);
    } else {
        gtk_source_view_set_indenter(view_, nullptr);
    }
}

void EditorTab::watchFile() {
    if (monitor_) monitor_->cancel();
    monitor_.reset();
    try {
        monitor_ = Gio::File::create_for_path(path_.string())->monitor_file();
        monitor_->signal_changed().connect(
            [this](const Glib::RefPtr<Gio::File>&, const Glib::RefPtr<Gio::File>&, Gio::FileMonitor::Event event) {
                onFileChanged(event);
            });
    } catch (const Glib::Error&) {
        // Without a monitor the editor just won't notice outside changes.
    }
}

void EditorTab::moveTo(const std::filesystem::path& file) {
    path_ = file;
    watchFile();
    // A new extension can mean a new language (main.c → main.cpp).
    gtk_source_buffer_set_language(buffer_, languageFor(file, cppHeaders_));
    applyIndenter();
    tabTitle_.set_tooltip_text(file.string());
    updateTitle();
}

void EditorTab::onFileChanged(Gio::FileMonitor::Event event) {
    using E = Gio::FileMonitor::Event;
    if (event != E::CHANGES_DONE_HINT && event != E::CREATED) return;
    if (!modified()) reloadFromDisk();
}

void EditorTab::reloadFromDisk() {
    std::string contents;
    try {
        contents = Glib::file_get_contents(path_.string());
    } catch (const Glib::Error&) {
        return;
    }
    if (!g_utf8_validate(contents.data(), static_cast<gssize>(contents.size()), nullptr)) return;

    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text, &start, &end);
    char* current = gtk_text_buffer_get_text(text, &start, &end, TRUE);
    const bool same = contents == current;
    g_free(current);
    if (same) return; // our own save, or a touch without changes

    setTextKeepingPlace(contents, false);
    gtk_text_buffer_set_modified(text, FALSE);
}

void EditorTab::replaceText(const std::string& contents) {
    setTextKeepingPlace(contents, true);
}

void EditorTab::setTextKeepingPlace(const std::string& contents, bool undoable) {
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(text, &cursor, gtk_text_buffer_get_insert(text));
    const int line = gtk_text_iter_get_line(&cursor);
    // Replacing the whole text would slide every mark to line 1; put breakpoints back by line.
    const std::vector<int> breakpointLines = breakpoints();
    if (undoable) gtk_text_buffer_begin_user_action(text);
    else gtk_text_buffer_begin_irreversible_action(text);
    gtk_text_buffer_set_text(text, contents.data(), static_cast<int>(contents.size()));
    if (undoable) gtk_text_buffer_end_user_action(text);
    else gtk_text_buffer_end_irreversible_action(text);
    std::vector<int> kept;
    for (int l : breakpointLines)
        if (l <= gtk_text_buffer_get_line_count(text)) kept.push_back(l);
    setBreakpoints(kept);
    GtkTextIter restored;
    gtk_text_buffer_get_iter_at_line(text, &restored, line);
    gtk_text_buffer_place_cursor(text, &restored);
}

void EditorTab::focusEditor() {
    gtk_widget_grab_focus(GTK_WIDGET(view_));
}

void EditorTab::gotoLine(int line, int column) {
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_line_offset(text, &iter, std::max(0, line - 1), std::max(0, column - 1));
    gtk_text_buffer_place_cursor(text, &iter);
    scrollToCursor();
    focusEditor();
}

void EditorTab::scrollToCursor() {
    auto scroll = [this] {
        GtkTextView* text = GTK_TEXT_VIEW(view_);
        GtkTextBuffer* buffer = GTK_TEXT_BUFFER(buffer_);
        gtk_text_view_scroll_to_mark(text, gtk_text_buffer_get_insert(buffer), 0.2, TRUE, 0.0, 0.3);
        // That also scrolls sideways to put the cursor at the left edge; when the cursor and the
        // selection fit without it, scroll back so the line's start (its indentation) shows.
        GtkTextIter end;
        gtk_text_buffer_get_iter_at_mark(buffer, &end, gtk_text_buffer_get_selection_bound(buffer));
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
        if (gtk_text_iter_compare(&cursor, &end) > 0) end = cursor;
        GdkRectangle where;
        gtk_text_view_get_iter_location(text, &end, &where);
        GtkAdjustment* sideways = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(text));
        if (sideways && where.x + where.width + 48 < gtk_adjustment_get_page_size(sideways))
            gtk_adjustment_set_value(sideways, 0);
    };
    scroll();
    // A freshly opened tab only knows estimated line heights; scroll again once layout has run.
    // sigc::track_obj disconnects the idle handler if this tab is destroyed first.
    Glib::signal_idle().connect_once(sigc::track_obj(scroll, *this), Glib::PRIORITY_LOW);
}

void EditorTab::selectRange(int line, size_t byteColumn, size_t byteLength) {
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    line = std::clamp(line, 1, gtk_text_buffer_get_line_count(text));
    // Bytes to characters, on this line as the buffer has it now (it may differ from the search's copy).
    const std::string s = lineText(line);
    auto chars = [&s](size_t bytes) {
        bytes = std::min(bytes, s.size());
        while (bytes > 0 && bytes < s.size() && (static_cast<unsigned char>(s[bytes]) & 0xC0) == 0x80) --bytes;
        return static_cast<int>(g_utf8_strlen(s.data(), static_cast<gssize>(bytes)));
    };
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line_offset(text, &start, line - 1, chars(byteColumn));
    gtk_text_buffer_get_iter_at_line_offset(text, &end, line - 1, chars(byteColumn + byteLength));
    gtk_text_buffer_select_range(text, &start, &end);
    scrollToCursor();
    focusEditor();
}

void EditorTab::placeCursor(const GtkTextIter& iter) {
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(buffer_), &iter);
    scrollToCursor();
    focusEditor();
}

void EditorTab::showDiagnostics(const std::vector<LspDiagnostic>& diagnostics) {
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    if (!errorTag_) {
        GdkRGBA red, orange;
        gdk_rgba_parse(&red, "#e01b24");
        gdk_rgba_parse(&orange, "#e5a50a");
        errorTag_ = gtk_text_buffer_create_tag(text, nullptr, "underline", PANGO_UNDERLINE_ERROR, "underline-rgba", &red, nullptr);
        warningTag_ = gtk_text_buffer_create_tag(text, nullptr, "underline", PANGO_UNDERLINE_ERROR, "underline-rgba", &orange, nullptr);
    }
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(text, &start, &end);
    gtk_text_buffer_remove_tag(text, errorTag_, &start, &end);
    gtk_text_buffer_remove_tag(text, warningTag_, &start, &end);
    for (const auto& d : diagnostics) {
        if (d.severity > LspDiagnostic::Severity::Warning) continue;
        GtkTextIter s = LspManager::iterAt(text, d.startLine, d.startUtf16);
        GtkTextIter e = LspManager::iterAt(text, d.endLine, d.endUtf16);
        if (gtk_text_iter_equal(&s, &e)) {
            // Zero-width ranges (e.g. "expected ';'") underline the next character, or the previous at line end.
            if (!gtk_text_iter_ends_line(&e)) gtk_text_iter_forward_char(&e);
            else if (!gtk_text_iter_starts_line(&s)) gtk_text_iter_backward_char(&s);
        }
        gtk_text_buffer_apply_tag(text, d.severity == LspDiagnostic::Severity::Error ? errorTag_ : warningTag_, &s, &e);
    }
}

void EditorTab::setBreakpoints(const std::vector<int>& lines) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer_), &start, &end);
    gtk_source_buffer_remove_source_marks(buffer_, &start, &end, kBreakpointCategory);
    for (int line : lines) {
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &at, line - 1);
        gtk_source_buffer_create_source_mark(buffer_, nullptr, kBreakpointCategory, &at);
    }
}

std::vector<int> EditorTab::breakpoints() const {
    std::vector<int> lines;
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(buffer_), &iter);
    // A mark at the very start isn't reached by "forward to mark", so check line 1 first.
    GSList* first = gtk_source_buffer_get_source_marks_at_iter(buffer_, &iter, kBreakpointCategory);
    if (first) lines.push_back(1);
    g_slist_free(first);
    while (gtk_source_buffer_forward_iter_to_source_mark(buffer_, &iter, kBreakpointCategory)) {
        const int line = gtk_text_iter_get_line(&iter) + 1;
        if (lines.empty() || lines.back() != line) lines.push_back(line);
    }
    return lines;
}

void EditorTab::toggleBreakpoint(int line) {
    GSList* marks = gtk_source_buffer_get_source_marks_at_line(buffer_, line - 1, kBreakpointCategory);
    const bool on = marks == nullptr;
    if (on) {
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &at, line - 1);
        gtk_source_buffer_create_source_mark(buffer_, nullptr, kBreakpointCategory, &at);
    } else {
        for (GSList* m = marks; m; m = m->next) gtk_text_buffer_delete_mark(GTK_TEXT_BUFFER(buffer_), GTK_TEXT_MARK(m->data));
    }
    g_slist_free(marks);
    breakpointToggled_.emit(line, on);
}

void EditorTab::setExecutionLine(int line) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer_), &start, &end);
    gtk_source_buffer_remove_source_marks(buffer_, &start, &end, kExecutionCategory);
    if (line <= 0) return;
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &at, line - 1);
    gtk_source_buffer_create_source_mark(buffer_, nullptr, kExecutionCategory, &at);
    placeCursor(at);
}

std::string EditorTab::lineText(int line) const {
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &start, line - 1);
    end = start;
    if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
    char* text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(buffer_), &start, &end, TRUE);
    std::string out(text);
    g_free(text);
    return out;
}

void EditorTab::onLineMarkActivated(GtkSourceView*, const GtkTextIter* iter, guint button, GdkModifierType, gint,
                                    gpointer self) {
    if (button != GDK_BUTTON_PRIMARY) return;
    static_cast<EditorTab*>(self)->toggleBreakpoint(gtk_text_iter_get_line(iter) + 1);
}

void EditorTab::onLineNumberActivated(GtkSourceGutterRenderer* renderer, GtkTextIter* iter, GdkRectangle*,
                                      guint button, GdkModifierType, gint, gpointer self) {
    if (button != GDK_BUTTON_PRIMARY) return;
    static_cast<EditorTab*>(self)->toggleBreakpoint(gtk_text_iter_get_line(iter) + 1);
    // The renderer's default is to select the line; a breakpoint click shouldn't do that.
    g_signal_stop_emission_by_name(renderer, "activate");
}

void EditorTab::activateTextAction(const char* action) {
    gtk_widget_activate_action(GTK_WIDGET(view_), action, nullptr);
}

bool EditorTab::find(const Glib::ustring& needle, bool forward, bool caseSensitive, bool incremental) {
    gtk_source_search_settings_set_case_sensitive(searchSettings_, caseSensitive);
    gtk_source_search_settings_set_search_text(searchSettings_, needle.empty() ? nullptr : needle.c_str());
    if (needle.empty()) return false;

    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter selStart, selEnd, matchStart, matchEnd;
    gtk_text_buffer_get_selection_bounds(text, &selStart, &selEnd);
    gboolean wrapped = FALSE;
    const bool found = forward
        ? gtk_source_search_context_forward(search_, incremental ? &selStart : &selEnd, &matchStart, &matchEnd, &wrapped)
        : gtk_source_search_context_backward(search_, &selStart, &matchStart, &matchEnd, &wrapped);
    if (!found) return false;
    gtk_text_buffer_select_range(text, &matchStart, &matchEnd);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(view_), &matchStart, 0.2, FALSE, 0.0, 0.0);
    return true;
}

bool EditorTab::replace(const Glib::ustring& needle, const Glib::ustring& with, bool caseSensitive) {
    if (needle.empty()) return false;
    gtk_source_search_settings_set_case_sensitive(searchSettings_, caseSensitive);
    gtk_source_search_settings_set_search_text(searchSettings_, needle.c_str());
    GtkTextBuffer* text = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter start, end;
    gtk_text_buffer_get_selection_bounds(text, &start, &end);
    // Only a selection that is a match is replaced; otherwise the first press just finds one.
    if (gtk_source_search_context_get_occurrence_position(search_, &start, &end) > 0) {
        GError* error = nullptr;
        if (!gtk_source_search_context_replace(search_, &start, &end, with.c_str(), -1, &error)) {
            if (error) g_error_free(error);
            return false;
        }
        gtk_text_buffer_place_cursor(text, &end); // after the replacement, so the next match is found
    }
    return find(needle, true, caseSensitive);
}

int EditorTab::replaceAll(const Glib::ustring& needle, const Glib::ustring& with, bool caseSensitive) {
    if (needle.empty()) return 0;
    gtk_source_search_settings_set_case_sensitive(searchSettings_, caseSensitive);
    gtk_source_search_settings_set_search_text(searchSettings_, needle.c_str());
    GError* error = nullptr;
    const guint count = gtk_source_search_context_replace_all(search_, with.c_str(), -1, &error);
    if (error) g_error_free(error);
    return static_cast<int>(count);
}

int EditorTab::matchCount() const {
    return gtk_source_search_context_get_occurrences_count(search_);
}

void EditorTab::clearSearch() {
    gtk_source_search_settings_set_search_text(searchSettings_, nullptr);
}

void EditorTab::onModifiedChanged(GtkTextBuffer*, gpointer self) {
    auto* tab = static_cast<EditorTab*>(self);
    tab->updateTitle();
    tab->modifiedChanged_.emit(tab->modified());
}

void EditorTab::onMatchCountChanged(GObject*, GParamSpec*, gpointer self) {
    auto* tab = static_cast<EditorTab*>(self);
    tab->matchCountChanged_.emit(tab->matchCount());
}

void EditorTab::updateTitle() {
    const std::string name = path_.filename().string();
    tabTitle_.set_text(modified() ? "*" + name : name);
}

} // namespace kevc
