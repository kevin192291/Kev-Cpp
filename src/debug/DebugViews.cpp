#include "debug/DebugViews.h"

#include "core/Icons.h"
#include "core/Mi.h"
#include "ui/IconWidgets.h"

#include <algorithm>
#include <tuple>

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

// --- VarNode -------------------------------------------------------------------------------------

VarNode::VarNode(std::weak_ptr<DebugController> debugger, VarInfo info, int depth, bool ownsVarobj, std::string path,
                 ExpandedRows expanded)
    : Gtk::Box(Gtk::Orientation::VERTICAL), debugger_(std::move(debugger)), info_(std::move(info)), depth_(depth),
      ownsVarobj_(ownsVarobj), path_(std::move(path)), expandedRows_(std::move(expanded)) {
    header_.set_margin_start(4 + depth * 16);
    expander_.set_has_frame(false);
    expander_.set_icon_name("pan-end-symbolic");
    expander_.add_css_class("flat");
    expander_.set_valign(Gtk::Align::START);
    expander_.set_focus_on_click(false);
    expander_.signal_clicked().connect([this] { toggle(); });
    // Keep alignment for leaf rows by hiding the icon rather than the button. A null pointer has
    // nothing to expand, although gdb still reports a child for it.
    const bool expandable = info_.children > 0 && !(info_.isPointer && mi::isNullPointer(info_.value));
    expander_.set_opacity(expandable ? 1.0 : 0.0);
    expander_.set_sensitive(expandable);
    const bool reopen = expandable && expandedRows_ && !path_.empty() && expandedRows_->count(path_);

    // One line per value: wrapping labels get squeezed to their minimum width in scrolled panels
    // and popovers. Long values are ellipsized; the tooltip and expanding show everything.
    text_.set_xalign(0.0f);
    text_.set_ellipsize(Pango::EllipsizeMode::END);
    text_.set_max_width_chars(110);
    text_.set_width_chars(std::min<int>(110, static_cast<int>(info_.name.size() + info_.value.size() + 4)));
    text_.set_selectable(true);
    text_.add_css_class("kevc-log");
    type_.add_css_class("dim-label");
    type_.set_xalign(0.0f);
    type_.set_valign(Gtk::Align::START);
    type_.set_margin_top(2);
    type_.set_text(info_.type);
    icon_.set_from_icon_name(icons::forType(info_.type));

    header_.append(expander_);
    header_.append(icon_);
    header_.append(text_);
    header_.append(type_);
    append(header_);
    children_.set_visible(false);
    append(children_);
    updateHeader();
    if (reopen) setExpanded(true); // it was open before the program moved on

    // Follow pointers right away: the address alone isn't useful.
    if (info_.isPointer && !mi::isNullPointer(info_.value)) {
        if (auto dbg = debugger_.lock()) {
            std::weak_ptr<char> alive = alive_;
            dbg->pointee(info_.varobj, [this, alive](std::string text, bool ok) {
                if (!alive.lock()) return;
                pointee_ = std::move(text);
                pointeeOk_ = ok;
                text_.set_width_chars(std::min<int>(
                    110, static_cast<int>(info_.name.size() + info_.value.size() + pointee_.size() + 8)));
                updateHeader();
            });
        }
    }
}

VarNode::VarNode(const std::string& name, const std::string& message) : Gtk::Box(Gtk::Orientation::VERTICAL) {
    header_.set_margin_start(4);
    expander_.set_has_frame(false);
    expander_.set_opacity(0.0);
    expander_.set_sensitive(false);
    expander_.set_icon_name("pan-end-symbolic");
    text_.set_xalign(0.0f);
    text_.set_wrap(true);
    text_.add_css_class("kevc-log");
    text_.set_markup("<b>" + esc(name) + "</b>  <span foreground=\"#ff6b6b\">" + esc(message) + "</span>");
    icon_.set_from_icon_name("kevc-error");
    header_.append(expander_);
    header_.append(icon_);
    header_.append(text_);
    append(header_);
}

VarNode* VarNode::error(const std::string& name, const std::string& message) {
    return Gtk::make_managed<VarNode>(name, message);
}

VarNode::~VarNode() {
    if (ownsVarobj_)
        if (auto dbg = debugger_.lock()) dbg->deleteVar(info_.varobj);
}

void VarNode::updateHeader() {
    std::string markup = "<b>" + esc(info_.name) + "</b> = ";
    if (info_.isPointer && mi::isNullPointer(info_.value)) {
        markup += "<span foreground=\"#a3b1b7\">nullptr</span>";
    } else {
        markup += esc(info_.value.empty() ? "{…}" : info_.value);
        if (!pointee_.empty())
            markup += pointeeOk_ ? "  <span foreground=\"#7bd88f\">→ " + esc(pointee_) + "</span>"
                                 : "  <span foreground=\"#ff6b6b\">→ &lt;" + esc(pointee_) + "&gt;</span>";
    }
    text_.set_markup(markup);
    text_.set_tooltip_text(info_.value + (pointee_.empty() ? "" : "\n→ " + pointee_));
}

void VarNode::toggle() {
    const bool expand = !children_.get_visible();
    if (expandedRows_ && !path_.empty()) {
        if (expand) expandedRows_->insert(path_);
        else expandedRows_->erase(path_);
    }
    setExpanded(expand);
}

void VarNode::setExpanded(bool expand) {
    expander_.set_icon_name(expand ? "pan-down-symbolic" : "pan-end-symbolic");
    children_.set_visible(expand);
    if (!expand || loaded_) return;
    loaded_ = true;
    auto dbg = debugger_.lock();
    if (!dbg) return;
    children_.append(dim("Loading…"));
    std::weak_ptr<char> alive = alive_;
    dbg->listChildren(info_.varobj, [this, alive](std::vector<VarInfo> children) {
        if (!alive.lock()) return;
        clearBox(children_);
        if (children.empty()) children_.append(dim("(no members)"));
        for (auto& c : children) {
            const std::string childPath = path_.empty() ? std::string() : path_ + "/" + c.name;
            children_.append(*Gtk::make_managed<VarNode>(debugger_, std::move(c), depth_ + 1, false, childPath,
                                                         expandedRows_));
        }
    });
}

// --- DebugPanel ----------------------------------------------------------------------------------

DebugPanel::DebugPanel() {
    // Docked on the right the panel can be dragged narrow: its tabs then scroll left and right
    // (arrows at the ends) instead of holding the panel as wide as all four side by side.
    set_scrollable(true);
    add_css_class("kevc-compact-tabs");
    localsMessage_.add_css_class("dim-label");
    localsMessage_.set_margin(12);
    localsMessage_.set_wrap(true);
    locals_.append(localsMessage_);
    localsScroller_.set_child(locals_);
    append_page(localsScroller_, *makeIconLabel("kevc-local-variable", "Locals"));

    stack_.set_activate_on_single_click(true);
    stack_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < stackLevels_.size()) frameActivated_.emit(stackLevels_[i]);
    });
    stackScroller_.set_child(stack_);
    append_page(stackScroller_, *makeIconLabel("kevc-call-stack", "Call Stack"));

    breakpoints_.set_activate_on_single_click(false);
    breakpoints_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < bpItems_.size()) bpActivated_.emit(bpItems_[i].first, bpItems_[i].second);
    });
    // The kinds of breakpoint, where anyone looking at the tab sees them. They act on the line the
    // cursor is on (as Debug's menu items and the editor's right-click menu do).
    bpButtons_.set_selection_mode(Gtk::SelectionMode::NONE);
    bpButtons_.set_max_children_per_line(4);
    bpButtons_.set_column_spacing(4);
    bpButtons_.set_row_spacing(2);
    bpButtons_.set_margin(4);
    for (const auto& [icon, text, action, tip] :
         {std::tuple{"kevc-breakpoint", "Breakpoint", "win.toggle-breakpoint", "Stop at the cursor's line (Ctrl+F5)"},
          std::tuple{"kevc-breakpoint-if", "IF Breakpoint…", "win.if-breakpoint", "Stop at the cursor's line only when a condition is true"},
          std::tuple{"kevc-breakpoint-live", "Live Breakpoint…", "win.live-breakpoint",
                     "Show values each time the cursor's line runs, without stopping"},
          std::tuple{"kevc-breakpoint-change", "Break When Value Changes…", "win.value-watch",
                     "Stop when a variable's value changes"}}) {
        auto* button = Gtk::make_managed<Gtk::Button>();
        button->set_child(*makeIconLabel(icon, text));
        button->set_action_name(action);
        button->set_tooltip_text(tip);
        button->set_has_frame(false);
        bpButtons_.append(*button);
    }
    bpPage_.append(bpButtons_);
    bpPage_.append(*Gtk::make_managed<Gtk::Separator>());
    bpScroller_.set_child(breakpoints_);
    bpScroller_.set_vexpand(true);
    bpPage_.append(bpScroller_);
    append_page(bpPage_, *makeIconLabel("kevc-breakpoint", "Breakpoints"));

    consoleView_.set_editable(false);
    consoleView_.set_cursor_visible(false);
    consoleView_.set_monospace(true);
    consoleView_.add_css_class("kevc-log");
    consoleScroller_.set_child(consoleView_);
    consoleScroller_.set_vexpand(true);
    consoleEntry_.set_placeholder_text("gdb command, e.g. info registers rip   (Enter to run)");
    consoleEntry_.set_width_chars(8); // the placeholder mustn't set how narrow the panel can be
    consoleEntry_.signal_activate().connect([this] {
        const std::string text = consoleEntry_.get_text();
        if (text.empty()) return;
        appendConsole("(gdb) " + text + "\n");
        consoleEntry_.set_text("");
        consoleCommand_.emit(text);
    });
    console_.append(consoleScroller_);
    console_.append(consoleEntry_);
    append_page(console_, *makeIconLabel("kevc-terminal", "GDB Console"));

    showMessage("Press F5 to debug. Click a line number to set a breakpoint.");
    setBreakpoints({}, {});
}

void DebugPanel::showMessage(const Glib::ustring& text) {
    clearBox(locals_);
    localsMessage_.set_text(text);
    locals_.append(localsMessage_);
    clearList(stack_);
    stackLevels_.clear();
}

void DebugPanel::setLocals(const std::vector<Gtk::Widget*>& nodes) {
    // Stay where the user had scrolled to; restore once the new rows have been laid out.
    auto adjustment = localsScroller_.get_vadjustment();
    const double scrolled = adjustment->get_value();
    Glib::signal_idle().connect_once(sigc::track_obj([adjustment, scrolled] { adjustment->set_value(scrolled); }, *this),
                                     Glib::PRIORITY_LOW);
    clearBox(locals_);
    if (nodes.empty()) {
        localsMessage_.set_text("No local variables in this frame.");
        locals_.append(localsMessage_);
    }
    for (auto* n : nodes) locals_.append(*n);
}

void DebugPanel::setFrames(const std::vector<StackFrame>& frames, int selected) {
    clearList(stack_);
    stackLevels_.clear();
    for (const auto& f : frames) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        std::string where = f.file.empty() ? "no source" : f.file.filename().string() + ":" + std::to_string(f.line);
        label->set_markup((f.level == selected ? "<b>" : "") + std::string("#") + std::to_string(f.level) + "  " +
                          esc(f.function) + "  <span foreground=\"#a3b1b7\">" + esc(where) + "</span>" +
                          (f.level == selected ? "</b>" : ""));
        label->set_xalign(0.0f);
        label->set_margin(4);
        stack_.append(*label);
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
    clearList(breakpoints_);
    bpItems_.clear();
    liveLabels_.clear();
    if (items.empty() && watches.empty()) {
        breakpoints_.append(dim("No breakpoints. Click a line number, or use the buttons above."));
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
        else if (bp.kind == BreakKind::Live) detail = "live: " + (bp.show.empty() ? std::string("its local variables") : joinExpressions(bp.show));
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
            auto* edit = smallButton("document-edit-symbolic", bp.kind == BreakKind::Live ? "Change the values it shows" : "Change the condition");
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
        breakpoints_.append(*row);
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
        remove->signal_clicked().connect([this, e = watch.expression] {
            Glib::signal_idle().connect_once([this, e] { watchRemoved_.emit(e); });
        });
        row->append(*image);
        row->append(*text);
        row->append(*remove);
        breakpoints_.append(*row);
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
    auto buffer = consoleView_.get_buffer();
    buffer->insert(buffer->end(), text);
    auto end = buffer->create_mark(buffer->end());
    consoleView_.scroll_to(end);
    buffer->delete_mark(end);
}

void DebugPanel::clearConsole() {
    consoleView_.get_buffer()->set_text("");
}

// --- WatchPanel ----------------------------------------------------------------------------------

WatchPanel::WatchPanel() : Gtk::Box(Gtk::Orientation::VERTICAL, 4) {
    set_margin(4);
    entry_.set_placeholder_text("Add watch, e.g. p->next");
    entry_.signal_activate().connect([this] {
        const std::string text = entry_.get_text();
        if (text.empty()) return;
        entry_.set_text("");
        addExpression(text);
    });
    scroller_.set_child(list_);
    scroller_.set_vexpand(true);
    append(entry_);
    append(scroller_);
    showPlaceholders();
}

void WatchPanel::addExpression(const std::string& expression) {
    expressions_.push_back(expression);
    changed_.emit();
}

void WatchPanel::setExpressions(std::vector<std::string> expressions) {
    expressions_ = std::move(expressions);
    showPlaceholders();
}

void WatchPanel::showPlaceholders() {
    std::vector<Gtk::Widget*> rows;
    for (const auto& e : expressions_) {
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup("<b>" + esc(e) + "</b>  <span foreground=\"#a3b1b7\">(not debugging)</span>");
        l->set_xalign(0.0f);
        l->add_css_class("kevc-log");
        rows.push_back(l);
    }
    rebuild(rows);
}

void WatchPanel::setRows(const std::vector<Gtk::Widget*>& rows) {
    rebuild(rows);
}

void WatchPanel::rebuild(const std::vector<Gtk::Widget*>& rows) {
    clearBox(list_);
    if (expressions_.empty()) {
        list_.append(dim("Watches are evaluated each time the program pauses."));
        return;
    }
    for (size_t i = 0; i < rows.size() && i < expressions_.size(); ++i) {
        auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
        rows[i]->set_hexpand(true);
        line->append(*rows[i]);
        auto* remove = Gtk::make_managed<Gtk::Button>();
        remove->set_icon_name("window-close-symbolic");
        remove->set_has_frame(false);
        remove->set_valign(Gtk::Align::START);
        remove->set_tooltip_text("Remove watch");
        remove->signal_clicked().connect([this, i] {
            Glib::signal_idle().connect_once([this, i] {
                if (i < expressions_.size()) expressions_.erase(expressions_.begin() + static_cast<long>(i));
                changed_.emit();
            });
        });
        line->append(*remove);
        list_.append(*line);
    }
}

} // namespace kevc
