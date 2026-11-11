#include "build/CompilerPanel.h"

#include <algorithm>
#include <filesystem>

namespace kevc {

namespace {

const char* severityIcon(Diagnostic::Severity s) {
    switch (s) {
    case Diagnostic::Severity::Error: return "kevc-error";
    case Diagnostic::Severity::Warning: return "kevc-warning";
    case Diagnostic::Severity::Note: break;
    }
    return "kevc-about";
}

const char* severityName(Diagnostic::Severity s) {
    switch (s) {
    case Diagnostic::Severity::Error: return "Error";
    case Diagnostic::Severity::Warning: return "Warning";
    case Diagnostic::Severity::Note: break;
    }
    return "Note";
}

// "main.cpp:12:5"; the full path is in the tooltip.
std::string location(const Diagnostic& d) {
    if (d.file.empty()) return {};
    std::string text = std::filesystem::path(d.file).filename().string();
    if (d.line > 0) text += ":" + std::to_string(d.line);
    if (d.line > 0 && d.column > 0) text += ":" + std::to_string(d.column);
    return text;
}

} // namespace

CompilerPanel::CompilerPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : CompilerPanelUi(object, builder) {
    // Double-click (or Enter) jumps, so a single click can just select a row.
    list_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i < 0 || static_cast<size_t>(i) >= diagnostics_.size()) return;
        // GtkListBox activates on the second press and refocuses the row on release, which would
        // take the focus back from the editor we jump to. So report it once the click is over.
        Glib::signal_idle().connect_once(
            sigc::track_obj([this, d = diagnostics_[i]] { activated_.emit(d); }, *this));
    });
    setEmptyText("Errors and warnings from builds appear here.");
    setupContextMenu();
}

CompilerPanel::~CompilerPanel() {
    menu_.unparent();
}

void CompilerPanel::setupContextMenu() {
    actions_ = Gio::SimpleActionGroup::create();
    actions_->add_action("copy", [this] { copy(selectedText()); });
    actions_->add_action("copy-all", [this] { copy(allText()); });
    insert_action_group("compiler", actions_);

    menu_.set_menu_model(contextMenu_);
    menu_.set_has_arrow(false);
    menu_.set_halign(Gtk::Align::START);
    menu_.set_parent(*this);

    // On the panel, not the list: the menu (Copy All) is there for the empty space below the rows too.
    auto rightClick = Gtk::GestureClick::create();
    rightClick->set_button(GDK_BUTTON_SECONDARY);
    rightClick->signal_pressed().connect([this](int, double x, double y) {
        if (diagnostics_.empty()) return;
        // A right-click on a row outside the selection selects it, as file managers do.
        double listX = 0, listY = 0;
        if (translate_coordinates(*list_, x, y, listX, listY)) {
            if (auto* row = list_->get_row_at_y(static_cast<int>(listY)); row && !row->is_selected()) {
                list_->unselect_all();
                list_->select_row(*row);
            }
        }
        if (auto action = std::dynamic_pointer_cast<Gio::SimpleAction>(actions_->lookup_action("copy")))
            action->set_enabled(!list_->get_selected_rows().empty());
        menu_.set_pointing_to(Gdk::Rectangle(static_cast<int>(x), static_cast<int>(y), 1, 1));
        menu_.popup();
    });
    add_controller(rightClick);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint key, guint, Gdk::ModifierType state) {
            const bool control = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
            if (!control || (key != GDK_KEY_c && key != GDK_KEY_C)) return false;
            copy(selectedText());
            return true;
        },
        false);
    list_->add_controller(keys);
}

std::string CompilerPanel::selectedText() const {
    std::string text;
    for (const auto* row : list_->get_selected_rows()) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < diagnostics_.size()) text += toText(diagnostics_[i]) + "\n";
    }
    return text;
}

std::string CompilerPanel::allText() const {
    std::string text;
    for (const Diagnostic& d : diagnostics_) text += toText(d) + "\n";
    return text;
}

void CompilerPanel::copy(const std::string& text) {
    if (text.empty()) return;
    // One row is pasted into a line; several keep their line ends.
    const bool one = std::count(text.begin(), text.end(), '\n') == 1;
    get_clipboard()->set_text(one ? text.substr(0, text.size() - 1) : text);
}

void CompilerPanel::clear() {
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
    diagnostics_.clear();
    updateVisibility();
}

void CompilerPanel::setEmptyText(const Glib::ustring& text) {
    message_->set_text(text);
    updateVisibility();
}

void CompilerPanel::add(const Diagnostic& d) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_start(6);
    row->set_margin_end(6);
    row->set_margin_top(2);
    row->set_margin_bottom(2);

    auto* severity = Gtk::make_managed<Gtk::Image>();
    severity->set_from_icon_name(severityIcon(d.severity));
    severity->set_tooltip_text(severityName(d.severity));

    // Fixed width so the messages line up like a table.
    auto* where = Gtk::make_managed<Gtk::Label>(location(d));
    where->set_width_chars(22);
    where->set_max_width_chars(22);
    where->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    where->set_xalign(0.0f);
    if (!d.file.empty()) where->set_tooltip_text(d.file);

    auto* text = Gtk::make_managed<Gtk::Label>(d.message);
    text->set_xalign(0.0f);
    text->set_hexpand(true);
    text->set_ellipsize(Pango::EllipsizeMode::END);
    text->set_tooltip_text(d.message);
    if (d.severity == Diagnostic::Severity::Note) text->add_css_class("dim-label");

    row->append(*severity);
    row->append(*where);
    row->append(*text);
    list_->append(*row);
    diagnostics_.push_back(d);
    updateVisibility();
}

void CompilerPanel::updateVisibility() {
    const bool empty = diagnostics_.empty();
    message_->set_visible(empty);
    list_->set_visible(!empty);
}

} // namespace kevc
