#include "ui/QuickSearch.h"

#include "core/Icons.h"
#include "ui/SearchMarkup.h"

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr size_t kFiles = 8;   // file names shown under the field
constexpr size_t kMatches = 8; // places the text appears

Gtk::Image& fileIcon(const std::string& rel) {
    auto* image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name(icons::forFile(fs::path(rel).filename()));
    return *image;
}

} // namespace

QuickSearch::QuickSearch() : Gtk::Box(Gtk::Orientation::HORIZONTAL) {
    add_css_class("kevc-quick-search");
    entry_.set_placeholder_text("Search files and text   (Ctrl+P)");
    entry_.set_width_chars(42);
    entry_.set_valign(Gtk::Align::CENTER);
    entry_.signal_changed().connect(sigc::mem_fun(*this, &QuickSearch::onChanged));
    entry_.signal_activate().connect([this] {
        Gtk::ListBoxRow* row = list_.get_selected_row();
        if (auto it = row ? rows_.find(row) : rows_.end(); it != rows_.end()) activate(it->second);
    });
    entry_.signal_stop_search().connect([this] {
        const bool hadText = !entry_.get_text().empty();
        close();
        if (!hadText) done_.emit();
    });
    // Up and Down choose a result while the cursor stays in the field.
    auto keys = Gtk::EventControllerKey::create();
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (!popover_.get_visible()) return false;
            if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) moveSelection(1);
            else if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) moveSelection(-1);
            else return false;
            return true;
        },
        false);
    entry_.add_controller(keys);
    // The list comes back with the cursor. It goes on Escape, on a choice, when another widget in
    // the window takes the keyboard, or on a click elsewhere in the window (see onRealize); not
    // when the window merely loses focus, which happens on the way into the list with
    // focus-follows-mouse.
    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_enter().connect([this] {
        if (!root_.empty() && !filesListed_ && !listing_) listFiles();
        // Back after a click in the list: leave its rows alone, or the click would miss its row.
        if (!entry_.get_text().empty() && !popover_.get_visible()) rebuild();
    });
    entry_.add_controller(focus);
    signal_realize().connect(sigc::mem_fun(*this, &QuickSearch::onRealize));
    append(entry_);

    // The list doesn't take the keyboard: typing stays in the field (as in GTK's suggestion entry).
    list_.set_selection_mode(Gtk::SelectionMode::BROWSE);
    list_.set_can_focus(false);
    list_.set_activate_on_single_click(true);
    list_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        auto it = rows_.find(row);
        if (it == rows_.end()) return;
        // Not while the list is still handling the click: activating empties it.
        Glib::signal_idle().connect_once(sigc::track_obj([this, chosen = it->second] { activate(chosen); }, *this));
    });
    scroller_.set_child(list_);
    scroller_.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroller_.set_propagate_natural_height(true);
    scroller_.set_max_content_height(460);
    scroller_.set_size_request(520, -1);
    popover_.set_child(scroller_);
    popover_.set_autohide(false);
    popover_.set_has_arrow(false);
    popover_.set_can_focus(false);
    popover_.set_position(Gtk::PositionType::BOTTOM);
    popover_.add_css_class("kevc-quick-results");
    popover_.set_parent(*this);
}

void QuickSearch::onRealize() {
    auto* window = dynamic_cast<Gtk::Window*>(get_root());
    if (!window || watching_ == window) return;
    watching_ = window;
    window->property_focus_widget().signal_changed().connect(sigc::track_obj(
        [this, window] {
            Gtk::Widget* focused = window->get_focus();
            if (focused && focused != &entry_ && !focused->is_ancestor(entry_)) popover_.popdown();
        },
        *this));
    auto click = Gtk::GestureClick::create();
    click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    click->set_button(0);
    // Presses in the list pass through here too (it's in the window's widget tree, on a surface of its
    // own): only a press on the window's own surface, outside the field, closes the list.
    GtkGesture* gesture = GTK_GESTURE(click->gobj());
    click->signal_pressed().connect(sigc::track_obj(
        [this, window, gesture](int, double x, double y) {
            if (!popover_.get_visible()) return;
            GdkEvent* event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
            if (!event || gdk_event_get_surface(event) != gtk_native_get_surface(GTK_NATIVE(window->gobj()))) return;
            graphene_rect_t bounds;
            const bool inField = gtk_widget_compute_bounds(GTK_WIDGET(entry_.gobj()), GTK_WIDGET(window->gobj()), &bounds) &&
                                 x >= bounds.origin.x && x < bounds.origin.x + bounds.size.width &&
                                 y >= bounds.origin.y && y < bounds.origin.y + bounds.size.height;
            if (!inField) popover_.popdown();
        },
        *this));
    window->add_controller(click);
}

QuickSearch::~QuickSearch() {
    textTimer_.disconnect();
    lister_.cancel();
    texter_.cancel();
    popover_.unparent();
}

void QuickSearch::setRoot(const fs::path& root) {
    root_ = root;
    lister_.cancel();
    texter_.cancel();
    files_.clear();
    filesListed_ = listing_ = false;
    textFor_.clear();
    textMatches_.clear();
    close();
}

void QuickSearch::focusSearch() {
    entry_.grab_focus();
    entry_.select_region(0, -1);
}

void QuickSearch::close() {
    textTimer_.disconnect();
    texter_.cancel();
    entry_.set_text("");
    popover_.popdown();
}

void QuickSearch::onChanged() {
    const std::string text = entry_.get_text();
    textTimer_.disconnect();
    if (text.empty() || root_.empty()) {
        rebuild();
        return;
    }
    if (!filesListed_ && !listing_) listFiles();
    // File names rank at once; the text search waits for a pause in the typing.
    textDone_ = false;
    textTimer_ = Glib::signal_timeout().connect(
        [this] {
            searchText();
            return false;
        },
        150);
    rebuild();
}

void QuickSearch::listFiles() {
    listing_ = true;
    lister_.start([this, root = root_](const std::atomic<bool>& cancelled, const BackgroundJob::Post& post) {
        auto files = search::listFiles(root, 200000, &cancelled);
        post([this, files = std::move(files)]() mutable {
            files_ = std::move(files);
            filesListed_ = true;
            listing_ = false;
            if (!entry_.get_text().empty()) rebuild();
        });
    });
}

void QuickSearch::searchText() {
    const std::string text = entry_.get_text();
    std::map<std::string, std::string> unsaved;
    if (unsaved_) {
        std::error_code ec;
        const fs::path root = fs::weakly_canonical(root_, ec);
        for (auto& [path, contents] : unsaved_()) {
            const fs::path rel = path.lexically_relative(root);
            if (!rel.empty() && *rel.begin() != "..") unsaved[rel.generic_string()] = std::move(contents);
        }
    }
    texter_.start([this, root = root_, text, unsaved = std::move(unsaved)](const std::atomic<bool>& cancelled,
                                                                           const BackgroundJob::Post& post) {
        const auto files = search::listFiles(root, 200000, &cancelled);
        std::vector<std::pair<std::string, search::LineMatch>> found;
        search::searchFiles(root, files, search::Matcher({text}), search::PathFilter(""), unsaved, kMatches, &cancelled,
                            [&found](search::FileResult&& r) {
                                for (auto& m : r.matches) found.emplace_back(r.path, std::move(m));
                            });
        post([this, text, found = std::move(found)]() mutable {
            textFor_ = text;
            textMatches_ = std::move(found);
            textDone_ = true;
            if (std::string(entry_.get_text()) == text) rebuild();
        });
    });
}

void QuickSearch::addHeading(const char* text) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(std::string("<small><b>") + text + "</b></small>");
    label->add_css_class("dim-label");
    label->set_xalign(0.0f);
    label->set_margin_start(8);
    label->set_margin_top(6);
    label->set_margin_bottom(2);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*label);
    row->set_selectable(false);
    row->set_activatable(false);
    row->set_can_focus(false);
    list_.append(*row);
}

void QuickSearch::addNote(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("dim-label");
    label->set_xalign(0.0f);
    label->set_wrap(true);
    label->set_margin_start(30);
    label->set_margin_top(2);
    label->set_margin_bottom(2);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*label);
    row->set_selectable(false);
    row->set_activatable(false);
    row->set_can_focus(false);
    list_.append(*row);
}

Gtk::ListBoxRow& QuickSearch::addRow(Gtk::Widget& content, Row data) {
    content.set_margin_start(8);
    content.set_margin_end(8);
    content.set_margin_top(2);
    content.set_margin_bottom(2);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(content);
    row->set_can_focus(false);
    list_.append(*row);
    rows_[row] = std::move(data);
    return *row;
}

void QuickSearch::rebuild() {
    const std::string text = entry_.get_text();
    rows_.clear();
    while (auto* row = list_.get_row_at_index(0)) list_.remove(*row);
    if (text.empty()) {
        popover_.popdown();
        return;
    }
    if (root_.empty()) {
        addNote("Open a project or folder (File → Open, Ctrl+O) to search its files.");
    } else {
        addHeading("FILES");
        if (!filesListed_) {
            addNote("Listing files…");
        } else if (auto ranked = search::rankFiles(files_, text, kFiles); ranked.empty()) {
            addNote("No file names match.");
        } else {
            for (const auto& match : ranked) {
                const std::set<size_t> positions(match.positions.begin(), match.positions.end());
                const size_t slash = match.path.rfind('/');
                const size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
                const std::string_view path = match.path;
                auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
                box->append(fileIcon(match.path));
                auto* name = Gtk::make_managed<Gtk::Label>();
                name->set_markup(highlightLetters(path.substr(nameStart), nameStart, positions));
                box->append(*name);
                if (nameStart > 0) {
                    auto* folder = Gtk::make_managed<Gtk::Label>();
                    folder->set_markup("<span alpha=\"60%\">" + highlightLetters(path.substr(0, nameStart - 1), 0, positions) +
                                       "</span>");
                    folder->set_xalign(0.0f);
                    folder->set_hexpand(true);
                    folder->set_ellipsize(Pango::EllipsizeMode::START);
                    box->append(*folder);
                }
                addRow(*box, {Row::Kind::File, match.path, {}});
            }
        }

        addHeading("TEXT IN FILES");
        // Until the new results come, the last ones stay (fewer jumps while typing).
        if (textFor_.empty() || (!textDone_ && textMatches_.empty())) {
            addNote("Searching…");
        } else if (textMatches_.empty()) {
            addNote(textDone_ ? Glib::ustring("Not found in any file.") : Glib::ustring("Searching…"));
        } else {
            for (const auto& [rel, match] : textMatches_) {
                auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
                box->append(fileIcon(rel));
                auto* where = Gtk::make_managed<Gtk::Label>(fs::path(rel).filename().string());
                box->append(*where);
                auto* line = Gtk::make_managed<Gtk::Label>();
                line->set_markup(matchMarkup(match));
                line->set_xalign(0.0f);
                line->set_hexpand(true);
                line->set_ellipsize(Pango::EllipsizeMode::END);
                box->append(*line);
                addRow(*box, {Row::Kind::Match, rel, match}).set_tooltip_text(rel + ":" + std::to_string(match.line));
            }
        }

        auto* all = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name("kevc-find-in-files");
        all->append(*icon);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup("Show every place “" + Glib::Markup::escape_text(text) +
                          "” appears  <span alpha=\"60%\">Search tab · Ctrl+Shift+F</span>");
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        all->append(*label);
        all->set_margin_top(4);
        addRow(*all, {Row::Kind::ShowAll, {}, {}});
    }
    // The first result is chosen, so Enter opens it.
    for (int i = 0; auto* row = list_.get_row_at_index(i); ++i)
        if (row->get_selectable()) {
            list_.select_row(*row);
            break;
        }
    scroller_.get_vadjustment()->set_value(0);
    if (!popover_.get_visible()) popover_.popup();
}

void QuickSearch::moveSelection(int step) {
    Gtk::ListBoxRow* selected = list_.get_selected_row();
    int i = selected ? selected->get_index() : -1;
    while (auto* row = list_.get_row_at_index(i += step)) {
        if (!row->get_selectable()) continue;
        list_.select_row(*row);
        auto adjustment = scroller_.get_vadjustment();
        graphene_rect_t bounds;
        if (gtk_widget_compute_bounds(GTK_WIDGET(row->gobj()), GTK_WIDGET(list_.gobj()), &bounds)) {
            const double top = bounds.origin.y, bottom = top + bounds.size.height;
            if (top < adjustment->get_value()) adjustment->set_value(top);
            else if (bottom > adjustment->get_value() + adjustment->get_page_size())
                adjustment->set_value(bottom - adjustment->get_page_size());
        }
        return;
    }
}

void QuickSearch::activate(const Row& row) {
    const Glib::ustring text = entry_.get_text();
    const Row chosen = row; // close() clears rows_, which `row` may be part of
    close();
    switch (chosen.kind) {
    case Row::Kind::File: openFile_.emit(root_ / chosen.rel); break;
    case Row::Kind::Match:
        openMatch_.emit(root_ / chosen.rel, chosen.match.line, chosen.match.column, chosen.match.length);
        break;
    case Row::Kind::ShowAll: showAll_.emit(text); break;
    }
}

} // namespace kevc
