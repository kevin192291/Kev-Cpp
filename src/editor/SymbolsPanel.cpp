#include "editor/SymbolsPanel.h"

#include "core/Icons.h"

namespace kevc {

SymbolsPanel::SymbolsPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : SymbolsPanelUi(object, builder) {
    list_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const int i = row->get_index();
        if (i >= 0 && static_cast<size_t>(i) < positions_.size()) activated_.emit(positions_[i].first, positions_[i].second);
    });
    showMessage("Symbols of the current file appear here.");
}

void SymbolsPanel::showMessage(const Glib::ustring& text) {
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
    positions_.clear();
    message_->set_text(text);
    message_->set_visible(true);
    list_->set_visible(false);
}

void SymbolsPanel::setSymbols(const lsp::json& symbols) {
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
    positions_.clear();
    for (const auto& s : symbols) addSymbol(s, 0, 0);
    const bool empty = positions_.empty();
    if (empty) message_->set_text("No symbols in this file.");
    message_->set_visible(empty);
    list_->set_visible(!empty);
}

void SymbolsPanel::addSymbol(const lsp::json& symbol, int depth, int parentKind) {
    // DocumentSymbol has selectionRange/children; SymbolInformation has location.range.
    const lsp::json& range = symbol.contains("selectionRange") ? symbol["selectionRange"]
                           : symbol.contains("location")       ? symbol["location"]["range"]
                                                               : symbol.value("range", lsp::json::object());
    const lsp::json start = range.value("start", lsp::json::object());

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    row->set_margin_start(6 + depth * 14);
    row->set_margin_top(1);
    row->set_margin_bottom(1);
    const int kindValue = symbol.value("kind", 0);
    auto* kind = Gtk::make_managed<Gtk::Image>();
    const lsp::json detail = symbol.value("detail", lsp::json());
    kind->set_from_icon_name(icons::forSymbolKind(kindValue, parentKind, detail.is_string() ? detail.get<std::string>() : ""));
    auto* name = Gtk::make_managed<Gtk::Label>(symbol.value("name", ""));
    name->set_xalign(0.0f);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    row->append(*kind);
    row->append(*name);
    if (symbol.contains("detail") && symbol["detail"].is_string() && !symbol["detail"].get<std::string>().empty()) {
        auto* detail = Gtk::make_managed<Gtk::Label>(symbol["detail"].get<std::string>());
        detail->add_css_class("dim-label");
        detail->set_ellipsize(Pango::EllipsizeMode::END);
        detail->set_xalign(0.0f);
        row->append(*detail);
    }
    list_->append(*row);
    positions_.emplace_back(start.value("line", 0), start.value("character", 0));

    for (const auto& child : symbol.value("children", lsp::json::array())) addSymbol(child, depth + 1, kindValue);
}

} // namespace kevc
