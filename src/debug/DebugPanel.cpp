#include "debug/DebugPanel.h"

#include "core/Breakpoints.h"

namespace kevc {

namespace {

std::string esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

void clearBox(Gtk::Box& box) {
    while (auto* child = box.get_first_child()) box.remove(*child);
}

void clearList(Gtk::ListBox& list) {
    while (auto* row = list.get_row_at_index(0)) list.remove(*row);
}

Gtk::Label& dim(const Glib::ustring& text) {
    auto* l = Gtk::make_managed<Gtk::Label>(text);
    l->add_css_class("dim-label");
    l->set_xalign(0.0f);
    l->set_margin(8);
    l->set_wrap(true);
    return *l;
}

} // namespace

DebugPanel::DebugPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : DebugPanelUi(object, builder) {
    // Docked on the right the panel can be dragged narrow: its tabs scroll left and right (the
    // design's scrollable) instead of holding the panel as wide as all four side by side.
    stack_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < stackLevels_.size()) frameActivated_.emit(stackLevels_[i]);
    });
    // The Breakpoints tab's buttons (in the design) act on the line the cursor is on, as Debug's
    // menu items and the editor's right-click menu do.
    breakpoints_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < bpItems_.size())
            bpActivated_.emit(bpItems_[i].first, bpItems_[i].second);
    });
    showMessage("Press F5 to debug. Click a line number to set a breakpoint.");
    setBreakpoints({}, {});
}

void DebugPanel::on_console_entry_activate() {
    const std::string text = consoleEntry_->get_text();
    if (text.empty()) return;
    appendConsole("(gdb) " + text + "\n");
    consoleEntry_->set_text("");
    consoleCommand_.emit(text);
}

void DebugPanel::showMessage(const Glib::ustring& text) {
    clearBox(*locals_);
    localsMessage_->set_text(text);
    localsMessage_->set_visible(true);
    clearList(*stack_);
    stackLevels_.clear();
}

void DebugPanel::setLocals(const std::vector<Gtk::Widget*>& nodes) {
    // Stay where the user had scrolled to; restore once the new rows have been laid out.
    auto adjustment = localsScroller_->get_vadjustment();
    const double scrolled = adjustment->get_value();
    Glib::signal_idle().connect_once(
        sigc::track_obj([adjustment, scrolled] { adjustment->set_value(scrolled); }, *this), Glib::PRIORITY_LOW);
    clearBox(*locals_);
    localsMessage_->set_text("No local variables in this frame.");
    localsMessage_->set_visible(nodes.empty());
    for (auto* n : nodes) locals_->append(*n);
}

void DebugPanel::setFrames(const std::vector<StackFrame>& frames, int selected) {
    clearList(*stack_);
    stackLevels_.clear();
    for (const auto& f : frames) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        std::string where = f.file.empty() ? "no source" : f.file.filename().string() + ":" + std::to_string(f.line);
        label->set_markup((f.level == selected ? "<b>" : "") + std::string("#") + std::to_string(f.level) + "  " +
                          esc(f.function) + "  <span foreground=\"#a3b1b7\">" + esc(where) + "</span>" +
                          (f.level == selected ? "</b>" : ""));
        label->set_xalign(0.0f);
        label->set_margin(4);
        stack_->append(*label);
        stackLevels_.push_back(f.level);
    }
}

namespace {

Gtk::Button* smallButton(const char* icon, const char* tip) {
    auto* button = Gtk::make_managed<Gtk::Button>();
    button->set_icon_name(icon);
    button->set_has_frame(false);
    button->set_tooltip_text(tip);
    button->set_valign(Gtk::Align::CENTER);
    return button;
}

} // namespace

void DebugPanel::setBreakpoints(const std::vector<BreakpointItem>& items, const std::vector<ValueWatchItem>& watches) {
    clearList(*breakpoints_);
    bpItems_.clear();
    liveLabels_.clear();
    if (items.empty() && watches.empty()) {
        breakpoints_->append(dim("No breakpoints. Click a line number, or use the buttons above."));
        return;
    }
    for (const auto& item : items) {
        const auto& bp = item.breakpoint;
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_margin(2);
        const char* icon = bp.kind == BreakKind::Conditional ? "kevc-breakpoint-if"
                           : bp.kind == BreakKind::Live      ? "kevc-breakpoint-live"
                                                             : "kevc-breakpoint";
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name(icon);
        image->set_valign(Gtk::Align::START);
        image->set_margin_top(2);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        auto* label = Gtk::make_managed<Gtk::Label>();
        std::string detail;
        if (bp.kind == BreakKind::Conditional) detail = "if " + bp.condition;
        else if (bp.kind == BreakKind::Live)
            detail = "live: " + (bp.show.empty() ? std::string("its local variables") : joinExpressions(bp.show));
        label->set_markup("<b>" + esc(item.file.filename().string() + ":" + std::to_string(item.line)) + "</b>" +
                          (detail.empty() ? "" : "  <span foreground=\"#a3b1b7\">" + esc(detail) + "</span>"));
        label->set_tooltip_text(item.file.string());
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        text->append(*label);
        if (bp.kind == BreakKind::Live) {
            auto* values = Gtk::make_managed<Gtk::Label>();
            values->add_css_class("kevc-live-values");
            values->set_xalign(0.0f);
            values->set_ellipsize(Pango::EllipsizeMode::END);
            values->set_selectable(true);
            const auto key = std::make_pair(item.file, item.line);
            const auto latest = liveTexts_.find(key);
            values->set_text(latest == liveTexts_.end() ? std::string("not run yet") : latest->second);
            liveLabels_[key] = values;
            text->append(*values);
        }
        row->append(*image);
        row->append(*text);
        if (bp.kind != BreakKind::Standard) {
            auto* edit = smallButton("document-edit-symbolic", bp.kind == BreakKind::Live ? "Change the values it shows"
                                                                                          : "Change the condition");
            edit->signal_clicked().connect([this, f = item.file, l = item.line] {
                Glib::signal_idle().connect_once([this, f, l] { bpEdit_.emit(f, l); });
            });
            row->append(*edit);
        }
        auto* remove = smallButton("edit-delete-symbolic", "Remove breakpoint");
        remove->signal_clicked().connect([this, f = item.file, l = item.line] {
            Glib::signal_idle().connect_once([this, f, l] { bpRemoved_.emit(f, l); });
        });
        row->append(*remove);
        breakpoints_->append(*row);
        bpItems_.emplace_back(item.file, item.line);
    }
    for (const auto& watch : watches) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_margin(2);
        auto* image = Gtk::make_managed<Gtk::Image>();
        image->set_from_icon_name("kevc-breakpoint-change");
        image->set_valign(Gtk::Align::START);
        image->set_margin_top(2);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        text->set_hexpand(true);
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup("Break when <b>" + esc(watch.expression) + "</b> changes");
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        auto* state = Gtk::make_managed<Gtk::Label>(watch.state);
        state->add_css_class(watch.armed ? "kevc-live-values" : "dim-label");
        state->set_xalign(0.0f);
        state->set_ellipsize(Pango::EllipsizeMode::END);
        state->set_tooltip_text(watch.state);
        text->append(*label);
        text->append(*state);
        auto* remove = smallButton("edit-delete-symbolic", "Stop watching it");
        remove->signal_clicked().connect(
            [this, e = watch.expression] { Glib::signal_idle().connect_once([this, e] { watchRemoved_.emit(e); }); });
        row->append(*image);
        row->append(*text);
        row->append(*remove);
        breakpoints_->append(*row);
    }
}

void DebugPanel::setLiveValues(const std::filesystem::path& file, int line, const std::string& text) {
    const auto key = std::make_pair(file, line);
    liveTexts_[key] = text;
    if (auto it = liveLabels_.find(key); it != liveLabels_.end()) it->second->set_text(text);
}

void DebugPanel::clearLiveValues() {
    liveTexts_.clear();
    for (auto& [key, label] : liveLabels_) label->set_text("not run yet");
}

void DebugPanel::appendConsole(const std::string& text) {
    auto buffer = consoleView_->get_buffer();
    buffer->insert(buffer->end(), text);
    auto end = buffer->create_mark(buffer->end());
    consoleView_->scroll_to(end);
    buffer->delete_mark(end);
}

void DebugPanel::clearConsole() {
    consoleView_->get_buffer()->set_text("");
}

} // namespace kevc
