#include "debug/DebugViews.h"

#include "core/Icons.h"
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

} // namespace kevc
