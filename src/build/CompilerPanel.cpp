#include "build/CompilerPanel.h"

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

CompilerPanel::CompilerPanel() {
    message_.add_css_class("dim-label");
    message_.set_wrap(true);
    message_.set_margin(12);
    // Double-click (or Enter) jumps, so a single click can just select a row.
    list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    list_.set_activate_on_single_click(false);
    list_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i < 0 || static_cast<size_t>(i) >= diagnostics_.size()) return;
        // GtkListBox activates on the second press and refocuses the row on release, which would
        // take the focus back from the editor we jump to. So report it once the click is over.
        Glib::signal_idle().connect_once(
            sigc::track_obj([this, d = diagnostics_[i]] { activated_.emit(d); }, *this));
    });
    box_.append(message_);
    box_.append(list_);
    set_child(box_);
    setEmptyText("Errors and warnings from builds appear here.");
}

void CompilerPanel::clear() {
    while (auto* row = list_.get_row_at_index(0)) list_.remove(*row);
    diagnostics_.clear();
    updateVisibility();
}

void CompilerPanel::setEmptyText(const Glib::ustring& text) {
    message_.set_text(text);
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
    list_.append(*row);
    diagnostics_.push_back(d);
    updateVisibility();
}

void CompilerPanel::updateVisibility() {
    const bool empty = diagnostics_.empty();
    message_.set_visible(empty);
    list_.set_visible(!empty);
}

} // namespace kevc
