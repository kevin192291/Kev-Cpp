#include "ui/DependenciesPanel.h"

namespace kevc {

namespace {

Glib::ustring esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

const char* severityColour(const std::string& severity) {
    switch (severityRank(severity)) {
    case 4:
    case 3: return "#ff6b6b";
    case 2: return "#f0c050";
    default: return "#a3b1b7";
    }
}

} // namespace

DependenciesPanel::DependenciesPanel() : Gtk::Box(Gtk::Orientation::VERTICAL) {
    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    bar->set_margin(6);
    summary_.set_xalign(0.0f);
    summary_.set_hexpand(true);
    summary_.set_ellipsize(Pango::EllipsizeMode::END);
    refreshButton_.set_tooltip_text("Runs npm outdated and npm audit (asks the npm registry)");
    refreshButton_.signal_clicked().connect([this] { refresh_.emit(); });
    bar->append(summary_);
    bar->append(refreshButton_);
    append(*bar);
    append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

    list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    list_.set_activate_on_single_click(false);
    list_.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = packages_.find(row); it != packages_.end()) openPackage_.emit(it->second);
    });
    message_.add_css_class("dim-label");
    message_.set_wrap(true);
    message_.set_margin(12);
    list_.set_placeholder(message_);
    scroller_.set_child(list_);
    scroller_.set_vexpand(true);
    append(scroller_);
    showMessage("Open a web project with a package.json, then press Check for updates.");
}

void DependenciesPanel::clear() {
    packages_.clear();
    while (auto* row = list_.get_row_at_index(0)) list_.remove(*row);
}

void DependenciesPanel::showMessage(const Glib::ustring& text) {
    clear();
    summary_.set_text("");
    message_.set_text(text);
}

void DependenciesPanel::setBusy(bool busy) {
    refreshButton_.set_sensitive(!busy);
    if (busy) summary_.set_text("Asking the npm registry…");
}

void DependenciesPanel::addRow(const Glib::ustring& markup, const std::string& package) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    label->set_xalign(0.0f);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    label->set_margin_start(6);
    label->set_margin_top(1);
    label->set_margin_bottom(1);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*label);
    list_.append(*row);
    if (!package.empty()) packages_[row] = package;
}

void DependenciesPanel::setReports(const std::vector<OutdatedPackage>& outdated, const AuditReport& audit) {
    clear();
    message_.set_text("Everything is up to date and npm audit found no known vulnerabilities.");
    int vulns = 0;
    for (const auto& [severity, n] : audit.counts) vulns += n;
    summary_.set_markup((vulns ? "<span foreground=\"#ff6b6b\">" + std::to_string(vulns) + " vulnerable</span>"
                               : std::string("<span foreground=\"#7bd88f\">no known vulnerabilities</span>")) +
                        "  ·  " + std::to_string(outdated.size()) + " outdated" +
                        (audit.error.empty() ? "" : "  ·  " + esc(audit.error)));
    if (!audit.vulnerabilities.empty()) addRow("<b>Vulnerabilities</b>", {});
    for (const auto& v : audit.vulnerabilities) {
        std::string via;
        for (const auto& item : v.via) via += (via.empty() ? "" : ", ") + item;
        addRow(Glib::ustring("<span foreground=\"") + severityColour(v.severity) + "\">" + esc(v.severity) + "</span>  <b>" +
                   esc(v.name) + "</b>  <span alpha=\"60%\">" + esc(v.range) + (v.direct ? " · direct" : "") +
                   (via.empty() ? "" : " · via " + esc(via)) + (v.fix.empty() ? " · no fix yet" : " · fix: " + esc(v.fix)) + "</span>",
               v.name);
    }
    if (!outdated.empty()) addRow("<b>Updates</b>", {});
    for (const auto& p : outdated) {
        const bool major = p.latest.substr(0, p.latest.find('.')) != p.current.substr(0, p.current.find('.'));
        addRow("<b>" + esc(p.name) + "</b>  " + esc(p.current.empty() ? "missing" : p.current) + " → " +
                   (p.wanted != p.current ? esc(p.wanted) + " (in range), " : "") + esc(p.latest) + " latest" +
                   (major ? "  <span foreground=\"#f0c050\">major</span>" : ""),
               p.name);
    }
}

} // namespace kevc
