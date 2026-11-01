#include "editor/EditorTab.h"

#include "editor/CppIndenter.h"

#include "lsp/LspManager.h"

#include <algorithm>
#include <array>
#include <tuple>
#include <stdexcept>

namespace kevc {

namespace {

constexpr const char* kBreakpointCategory = "kevc-breakpoint";      // standard
constexpr const char* kConditionalCategory = "kevc-breakpoint-if"; // stops when its condition holds
constexpr const char* kLiveCategory = "kevc-breakpoint-live";      // shows values, never stops
constexpr const char* kBreakpointData = "kevc-breakpoint";         // the Breakpoint on a mark
constexpr std::array kBreakpointCategories{kBreakpointCategory, kConditionalCategory, kLiveCategory};

const char* categoryOf(kevc::BreakKind kind) {
    switch (kind) {
    case kevc::BreakKind::Conditional: return kConditionalCategory;
    case kevc::BreakKind::Live: return kLiveCategory;
    case kevc::BreakKind::Standard: break;
    }
    return kBreakpointCategory;
}
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
    // The other kinds: their own icons, and a tint that says which (red: stops; teal: never stops).
    for (const auto& [category, icon, tint] :
         {std::tuple{kConditionalCategory, "kevc-breakpoint-if", "rgba(224,27,36,0.12)"},
          std::tuple{kLiveCategory, "kevc-breakpoint-live", "rgba(26,170,196,0.14)"}}) {
        GdkRGBA background;
        gdk_rgba_parse(&background, tint);
        GtkSourceMarkAttributes* attributes = gtk_source_mark_attributes_new();
        gtk_source_mark_attributes_set_icon_name(attributes, icon);
        gtk_source_mark_attributes_set_background(attributes, &background);
        gtk_source_view_set_mark_attributes(view_, category, attributes, 10);
        g_object_unref(attributes);
    }
    // Live values stay after their line as lines above them change.
    g_signal_connect_swapped(buffer_, "changed", G_CALLBACK(+[](EditorTab* tab) { tab->placeLiveLabels(); }), this);
    GtkSourceMarkAttributes* exec = gtk_source_mark_attributes_new();
    gtk_source_mark_attributes_set_icon_name(exec, "kevc-exec-arrow");
    gtk_source_mark_attributes_set_background(exec, &executionBg);
    gtk_source_view_set_mark_attributes(view_, kExecutionCategory, exec, 20);
    g_object_unref(exec);
    g_signal_connect(view_, "line-mark-activated", G_CALLBACK(&EditorTab::onLineMarkActivated), this);
    // A right-click on the line numbers or marks: the breakpoint kinds for that line (setGutterMenu),
    // not the text's menu, which the view would otherwise open there too.
    GtkGesture* gutterClick = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gutterClick), GDK_BUTTON_SECONDARY);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gutterClick), GTK_PHASE_CAPTURE);
    auto onGutterPress = +[](GtkGestureClick* gesture, int, double x, double y, gpointer self) {
        auto* tab = static_cast<EditorTab*>(self);
        GtkTextView* text = GTK_TEXT_VIEW(tab->view_);
        GtkWidget* gutter = gtk_text_view_get_gutter(text, GTK_TEXT_WINDOW_LEFT);
        if (!tab->gutterPopover_ || !gutter || x >= gtk_widget_get_width(gutter)) return;
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
        int bx = 0, by = 0;
        gtk_text_view_window_to_buffer_coords(text, GTK_TEXT_WINDOW_WIDGET, 0, static_cast<int>(y), &bx, &by);
        GtkTextIter line;
        gtk_text_view_get_line_at_y(text, &line, by, nullptr);
        tab->showGutterMenu(gtk_text_iter_get_line(&line));
    };
    g_signal_connect(gutterClick, "pressed", G_CALLBACK(onGutterPress), this);
    gtk_widget_add_controller(GTK_WIDGET(view_), GTK_EVENT_CONTROLLER(gutterClick));

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
    for (auto& [mark, label] : liveLabels_) g_object_unref(mark);
    if (gutterPopover_) gtk_widget_unparent(GTK_WIDGET(gutterPopover_));
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
    const FileBreakpoints breakpointsBefore = breakpoints();
    if (undoable) gtk_text_buffer_begin_user_action(text);
    else gtk_text_buffer_begin_irreversible_action(text);
    gtk_text_buffer_set_text(text, contents.data(), static_cast<int>(contents.size()));
    if (undoable) gtk_text_buffer_end_user_action(text);
    else gtk_text_buffer_end_irreversible_action(text);
    FileBreakpoints kept;
    for (const auto& [l, bp] : breakpointsBefore)
        if (l <= gtk_text_buffer_get_line_count(text)) kept[l] = bp;
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

void EditorTab::setBreakpoints(const FileBreakpoints& breakpoints) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(buffer_), &start, &end);
    clearLiveValues();
    for (const char* category : kBreakpointCategories) gtk_source_buffer_remove_source_marks(buffer_, &start, &end, category);
    const int lines = gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(buffer_));
    for (const auto& [line, bp] : breakpoints) {
        if (line < 1 || line > lines) continue;
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &at, line - 1);
        GtkSourceMark* mark = gtk_source_buffer_create_source_mark(buffer_, nullptr, categoryOf(bp.kind), &at);
        g_object_set_data_full(G_OBJECT(mark), kBreakpointData, new Breakpoint(bp),
                               [](gpointer data) { delete static_cast<Breakpoint*>(data); });
    }
}

FileBreakpoints EditorTab::breakpoints() const {
    FileBreakpoints out;
    for (const char* category : kBreakpointCategories) {
        GtkTextIter iter;
        gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(buffer_), &iter);
        // A mark at the very start isn't reached by "forward to mark", so look there first.
        bool first = true;
        while (first || gtk_source_buffer_forward_iter_to_source_mark(buffer_, &iter, category)) {
            first = false;
            GSList* marks = gtk_source_buffer_get_source_marks_at_iter(buffer_, &iter, category);
            for (GSList* m = marks; m; m = m->next) {
                const auto* bp = static_cast<const Breakpoint*>(g_object_get_data(G_OBJECT(m->data), kBreakpointData));
                out[gtk_text_iter_get_line(&iter) + 1] = bp ? *bp : Breakpoint{};
            }
            g_slist_free(marks);
        }
    }
    return out;
}

std::optional<Breakpoint> EditorTab::breakpointAt(int line) const {
    for (const char* category : kBreakpointCategories) {
        GSList* marks = gtk_source_buffer_get_source_marks_at_line(buffer_, line - 1, category);
        std::optional<Breakpoint> found;
        if (marks) {
            const auto* bp = static_cast<const Breakpoint*>(g_object_get_data(G_OBJECT(marks->data), kBreakpointData));
            found = bp ? *bp : Breakpoint{};
        }
        g_slist_free(marks);
        if (found) return found;
    }
    return std::nullopt;
}

void EditorTab::removeBreakpointMarks(int line) {
    for (const char* category : kBreakpointCategories) {
        GSList* marks = gtk_source_buffer_get_source_marks_at_line(buffer_, line - 1, category);
        for (GSList* m = marks; m; m = m->next) {
            GtkTextMark* mark = GTK_TEXT_MARK(m->data);
            if (auto it = liveLabels_.find(mark); it != liveLabels_.end()) {
                retireLiveLabel(it->second);
                liveLabels_.erase(it);
                g_object_unref(mark);
            }
            gtk_text_buffer_delete_mark(GTK_TEXT_BUFFER(buffer_), mark);
        }
        g_slist_free(marks);
    }
}

void EditorTab::setBreakpoint(int line, const std::optional<Breakpoint>& breakpoint) {
    if (line < 1 || line > gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(buffer_))) return;
    removeBreakpointMarks(line);
    if (breakpoint) {
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(buffer_), &at, line - 1);
        GtkSourceMark* mark = gtk_source_buffer_create_source_mark(buffer_, nullptr, categoryOf(breakpoint->kind), &at);
        g_object_set_data_full(G_OBJECT(mark), kBreakpointData, new Breakpoint(*breakpoint),
                               [](gpointer data) { delete static_cast<Breakpoint*>(data); });
    }
    breakpointChanged_.emit(line, breakpoint);
}

void EditorTab::toggleBreakpoint(int line) {
    setBreakpoint(line, breakpointAt(line) ? std::nullopt : std::optional<Breakpoint>(Breakpoint{}));
}

void EditorTab::showLiveValues(int line, const std::string& text) {
    GSList* marks = gtk_source_buffer_get_source_marks_at_line(buffer_, line - 1, kLiveCategory);
    GtkTextMark* mark = marks ? GTK_TEXT_MARK(marks->data) : nullptr;
    g_slist_free(marks);
    if (!mark) return;
    Gtk::Label*& label = liveLabels_[mark];
    if (!label) {
        // Ours until the label goes: deleting its line deletes the mark, and placeLiveLabels must
        // still be able to ask whether it was.
        g_object_ref(mark);
        if (!spareLiveLabels_.empty()) {
            label = spareLiveLabels_.back();
            spareLiveLabels_.pop_back();
            label->set_visible(true);
        } else {
            label = Gtk::make_managed<Gtk::Label>();
            label->add_css_class("kevc-live-values");
            label->set_can_target(false); // clicks go to the text under it
            gtk_text_view_add_overlay(GTK_TEXT_VIEW(view_), GTK_WIDGET(label->gobj()), 0, 0);
        }
    }
    label->set_text(text);
    placeLiveLabels();
}

void EditorTab::placeLiveLabels() {
    // Each label just after the end of its breakpoint's line.
    for (auto it = liveLabels_.begin(); it != liveLabels_.end();) {
        if (gtk_text_mark_get_deleted(it->first)) {
            retireLiveLabel(it->second);
            g_object_unref(it->first);
            it = liveLabels_.erase(it);
            continue;
        }
        GtkTextIter end;
        gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(buffer_), &end, it->first);
        if (!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
        GdkRectangle where;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view_), &end, &where);
        gtk_text_view_move_overlay(GTK_TEXT_VIEW(view_), GTK_WIDGET(it->second->gobj()), where.x + 28, where.y);
        ++it;
    }
}

void EditorTab::clearLiveValues() {
    for (auto& [mark, label] : liveLabels_) {
        retireLiveLabel(label);
        g_object_unref(mark);
    }
    liveLabels_.clear();
}

void EditorTab::retireLiveLabel(Gtk::Label* label) {
    // Hidden and kept for the next live breakpoint: GTK 4.14's gtk_text_view_remove() refuses
    // overlay children ("not a child of GtkTextView"), and unparenting one behind the view's back
    // would leave the view a dangling entry.
    label->set_visible(false);
    spareLiveLabels_.push_back(label);
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

void EditorTab::setGutterMenu(const Glib::RefPtr<Gio::MenuModel>& menu) {
    if (!gutterPopover_) {
        gutterPopover_ = GTK_POPOVER(gtk_popover_menu_new_from_model(nullptr));
        gtk_widget_set_parent(GTK_WIDGET(gutterPopover_), GTK_WIDGET(view_));
        gtk_popover_set_position(gutterPopover_, GTK_POS_RIGHT);
        gtk_popover_set_has_arrow(gutterPopover_, FALSE);
    }
    gtk_popover_menu_set_menu_model(GTK_POPOVER_MENU(gutterPopover_), menu ? menu->gobj() : nullptr);
}

void EditorTab::showGutterMenu(int line) {
    // The cursor goes to that line: the menu's items (IF, Live…) act on the cursor's line.
    GtkTextBuffer* buffer = GTK_TEXT_BUFFER(buffer_);
    GtkTextIter at;
    gtk_text_buffer_get_iter_at_line(buffer, &at, line);
    gtk_text_buffer_place_cursor(buffer, &at);
    if (!gutterPopover_) return;
    GdkRectangle where;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view_), &at, &where);
    int x = 0, y = 0;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view_), GTK_TEXT_WINDOW_WIDGET, where.x, where.y, &x, &y);
    GtkWidget* gutter = gtk_text_view_get_gutter(GTK_TEXT_VIEW(view_), GTK_TEXT_WINDOW_LEFT);
    const GdkRectangle point{gutter ? gtk_widget_get_width(gutter) : 0, y, 1, std::max(where.height, 1)};
    gtk_popover_set_pointing_to(gutterPopover_, &point);
    // After the click that opened it has finished, or the popover would see its release.
    g_idle_add_once([](gpointer p) { gtk_popover_popup(GTK_POPOVER(p)); }, gutterPopover_);
}

void EditorTab::onLineMarkActivated(GtkSourceView*, const GtkTextIter* iter, guint button, GdkModifierType, gint,
                                    gpointer self) {
    if (button != GDK_BUTTON_PRIMARY) return;
    static_cast<EditorTab*>(self)->toggleBreakpoint(gtk_text_iter_get_line(iter) + 1);
}

void EditorTab::onLineNumberActivated(GtkSourceGutterRenderer* renderer, GtkTextIter* iter, GdkRectangle*,
                                      guint button, GdkModifierType, gint, gpointer self) {
    auto* tab = static_cast<EditorTab*>(self);
    if (button != GDK_BUTTON_PRIMARY) return;
    tab->toggleBreakpoint(gtk_text_iter_get_line(iter) + 1);
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
