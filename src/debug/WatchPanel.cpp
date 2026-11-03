#include "debug/WatchPanel.h"

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

WatchPanel::WatchPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : WatchPanelUi(object, builder) {
    showPlaceholders();
}

void WatchPanel::on_entry_activate() {
    const std::string text = entry_->get_text();
    if (text.empty()) return;
    entry_->set_text("");
    addExpression(text);
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
    clearBox(*list_);
    if (expressions_.empty()) {
        list_->append(dim("Watches are evaluated each time the program pauses."));
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
        list_->append(*line);
    }
}

} // namespace kevc
