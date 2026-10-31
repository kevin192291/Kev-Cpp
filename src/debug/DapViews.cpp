#include "debug/DapViews.h"

namespace kevc {

DapVarNode::DapVarNode(dap::Variable variable, int depth, Fetch fetch, bool expand)
    : Gtk::Box(Gtk::Orientation::VERTICAL), variable_(std::move(variable)), depth_(depth), fetch_(std::move(fetch)) {
    header_.set_margin_start(4 + depth * 16);
    expander_.set_has_frame(false);
    expander_.set_icon_name("pan-end-symbolic");
    expander_.add_css_class("flat");
    expander_.set_valign(Gtk::Align::START);
    expander_.set_focus_on_click(false);
    expander_.signal_clicked().connect([this] { toggle(); });
    const bool expandable = variable_.reference > 0 && fetch_;
    // Keep leaf rows aligned: hide the icon, not the button.
    expander_.set_opacity(expandable ? 1.0 : 0.0);
    expander_.set_sensitive(expandable);

    text_.set_xalign(0.0f);
    text_.set_ellipsize(Pango::EllipsizeMode::END);
    text_.set_max_width_chars(110);
    text_.set_width_chars(std::min<int>(110, static_cast<int>(variable_.name.size() + variable_.value.size() + 4)));
    text_.set_selectable(true);
    text_.add_css_class("kevc-log");
    text_.set_markup("<b>" + Glib::Markup::escape_text(variable_.name) + "</b> = " +
                     Glib::Markup::escape_text(variable_.value.empty() ? "{…}" : variable_.value));
    text_.set_tooltip_text(variable_.value);
    type_.add_css_class("dim-label");
    type_.set_xalign(0.0f);
    type_.set_valign(Gtk::Align::START);
    type_.set_margin_top(2);
    type_.set_text(variable_.type);

    header_.append(expander_);
    header_.append(text_);
    header_.append(type_);
    append(header_);
    children_.set_visible(false);
    append(children_);
    if (expand && expandable) toggle();
}

DapVarNode* DapVarNode::error(const std::string& name, const std::string& message) {
    auto* node = Gtk::make_managed<DapVarNode>(dap::Variable{name, "<" + message + ">", "", 0}, 0, Fetch{});
    return node;
}

void DapVarNode::toggle() {
    expanded_ = !expanded_;
    expander_.set_icon_name(expanded_ ? "pan-down-symbolic" : "pan-end-symbolic");
    children_.set_visible(expanded_);
    if (!expanded_ || loaded_ || !fetch_) return;
    loaded_ = true;
    std::weak_ptr<char> alive = alive_;
    fetch_(variable_.reference, [this, alive](std::vector<dap::Variable> vars) {
        if (!alive.lock()) return;
        for (auto& v : vars) children_.append(*Gtk::make_managed<DapVarNode>(std::move(v), depth_ + 1, fetch_));
        if (vars.empty()) {
            auto* none = Gtk::make_managed<Gtk::Label>("(no properties)");
            none->add_css_class("dim-label");
            none->set_xalign(0.0f);
            none->set_margin_start(24 + (depth_ + 1) * 16);
            children_.append(*none);
        }
    });
}

} // namespace kevc
