#include "debug/DebugViews.h"

#include "core/Mi.h"

#include <algorithm>

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

    header_.append(expander_);
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
    text_.set_markup("<b>" + esc(name) + "</b>  <span foreground=\"#c01c28\">" + esc(message) + "</span>");
    header_.append(expander_);
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
        markup += "<span foreground=\"#9a9996\">nullptr</span>";
    } else {
        markup += esc(info_.value.empty() ? "{…}" : info_.value);
        if (!pointee_.empty())
            markup += pointeeOk_ ? "  <span foreground=\"#26a269\">→ " + esc(pointee_) + "</span>"
                                 : "  <span foreground=\"#c01c28\">→ &lt;" + esc(pointee_) + "&gt;</span>";
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
    localsMessage_.add_css_class("dim-label");
    localsMessage_.set_margin(12);
    localsMessage_.set_wrap(true);
    locals_.append(localsMessage_);
    localsScroller_.set_child(locals_);
    append_page(localsScroller_, "Locals");

    stack_.set_activate_on_single_click(true);
    stack_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < stackLevels_.size()) frameActivated_.emit(stackLevels_[i]);
    });
    stackScroller_.set_child(stack_);
    append_page(stackScroller_, "Call Stack");

    breakpoints_.set_activate_on_single_click(false);
    breakpoints_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < bpItems_.size()) bpActivated_.emit(bpItems_[i].first, bpItems_[i].second);
    });
    bpScroller_.set_child(breakpoints_);
    append_page(bpScroller_, "Breakpoints");

    consoleView_.set_editable(false);
    consoleView_.set_cursor_visible(false);
    consoleView_.set_monospace(true);
    consoleView_.add_css_class("kevc-log");
    consoleScroller_.set_child(consoleView_);
    consoleScroller_.set_vexpand(true);
    consoleEntry_.set_placeholder_text("gdb command, e.g. info registers rip   (Enter to run)");
    consoleEntry_.signal_activate().connect([this] {
        const std::string text = consoleEntry_.get_text();
        if (text.empty()) return;
        appendConsole("(gdb) " + text + "\n");
        consoleEntry_.set_text("");
        consoleCommand_.emit(text);
    });
    console_.append(consoleScroller_);
    console_.append(consoleEntry_);
    append_page(console_, "GDB Console");

    showMessage("Press F5 to debug. Click a line number to set a breakpoint.");
    setBreakpoints({});
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
                          esc(f.function) + "  <span foreground=\"#9a9996\">" + esc(where) + "</span>" +
                          (f.level == selected ? "</b>" : ""));
        label->set_xalign(0.0f);
        label->set_margin(4);
        stack_.append(*label);
        stackLevels_.push_back(f.level);
    }
}

void DebugPanel::setBreakpoints(const std::vector<std::pair<std::filesystem::path, int>>& items) {
    clearList(breakpoints_);
    bpItems_ = items;
    if (items.empty()) {
        breakpoints_.append(dim("No breakpoints. Click a line number, or press Ctrl+F5."));
        return;
    }
    for (const auto& [file, line] : items) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_margin(2);
        auto* dot = Gtk::make_managed<Gtk::Label>();
        dot->set_markup("<span foreground=\"#e01b24\">●</span>");
        auto* label = Gtk::make_managed<Gtk::Label>(file.filename().string() + ":" + std::to_string(line));
        label->set_tooltip_text(file.string());
        label->set_xalign(0.0f);
        label->set_hexpand(true);
        auto* remove = Gtk::make_managed<Gtk::Button>();
        remove->set_icon_name("edit-delete-symbolic");
        remove->set_has_frame(false);
        remove->set_tooltip_text("Remove breakpoint");
        remove->signal_clicked().connect([this, f = file, l = line] {
            Glib::signal_idle().connect_once([this, f, l] { bpRemoved_.emit(f, l); });
        });
        row->append(*dot);
        row->append(*label);
        row->append(*remove);
        breakpoints_.append(*row);
    }
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

void WatchPanel::showPlaceholders() {
    std::vector<Gtk::Widget*> rows;
    for (const auto& e : expressions_) {
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup("<b>" + esc(e) + "</b>  <span foreground=\"#9a9996\">(not debugging)</span>");
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
