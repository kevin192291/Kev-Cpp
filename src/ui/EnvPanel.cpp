#include "ui/EnvPanel.h"

#include <set>

namespace fs = std::filesystem;

namespace kevc {

namespace {

Glib::ustring esc(const std::string& s) {
    return Glib::Markup::escape_text(s);
}

} // namespace

EnvPanel::EnvPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder) : EnvPanelUi(object, builder) {
    list_->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = targets_.find(row); it != targets_.end()) open_.emit(it->second.file, it->second.line);
    });
    showMessage("Open a SvelteKit project to see its environment variables.");
}

void EnvPanel::on_edit_env_clicked() {
    editEnv_.emit();
}

void EnvPanel::clear() {
    targets_.clear();
    while (auto* row = list_->get_row_at_index(0)) list_->remove(*row);
}

void EnvPanel::showMessage(const Glib::ustring& text) {
    clear();
    summary_->set_text("");
    message_->set_text(text);
}

void EnvPanel::addRow(const char* icon, const Glib::ustring& markup, std::optional<Target> target) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    box->set_margin_start(6);
    box->set_margin_top(1);
    box->set_margin_bottom(1);
    auto* image = Gtk::make_managed<Gtk::Image>();
    if (icon) image->set_from_icon_name(icon);
    image->set_pixel_size(16);
    box->append(*image);
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_markup(markup);
    label->set_xalign(0.0f);
    label->set_hexpand(true);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    box->append(*label);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*box);
    list_->append(*row);
    if (target) targets_[row] = *target;
}

void EnvPanel::setReport(const kit::EnvReport& report, const fs::path& root) {
    clear();
    message_->set_text("No environment variables: nothing in .env files or $env imports in src/.");
    int serious = 0;
    for (const auto& p : report.problems) {
        const bool unused = p.kind == kit::EnvProblem::Kind::Unused;
        if (!unused) ++serious;
        const std::string where = p.file.lexically_relative(root).generic_string() + ":" + std::to_string(p.line);
        addRow(unused ? "kevc-about" : p.kind == kit::EnvProblem::Kind::PrivateInClient ? "kevc-error" : "kevc-warning",
               esc(p.message) + "  <span alpha=\"60%\">" + esc(where) + "</span>", Target{p.file, p.line});
    }
    for (const auto& row : report.rows) {
        Glib::ustring markup = "<tt><b>" + esc(row.name) + "</b></tt>  ";
        markup += row.isPublic ? "<span foreground=\"#7bd88f\">public</span>" : "<span foreground=\"#f0c050\">private</span>";
        if (!row.definitions.empty()) {
            const auto& last = row.definitions.back();
            // Show public values; never show a private one on screen.
            markup += "  = " + (row.isPublic ? esc(last.value.empty() ? std::string("\"\"") : last.value)
                                             : Glib::ustring(last.value.empty() ? "\"\"" : "••••••"));
            std::set<std::string> files;
            std::string defined;
            for (const auto& d : row.definitions)
                if (files.insert(d.file.filename().string()).second) defined += (defined.empty() ? "" : ", ") + d.file.filename().string();
            markup += "  <span alpha=\"60%\">in " + esc(defined) + "</span>";
        } else {
            markup += "  <span alpha=\"60%\">not in any .env file</span>";
        }
        std::set<std::string> modules;
        for (const auto& u : row.uses) modules.insert(u.module.substr(5)); // without "$env/"
        if (!row.uses.empty()) {
            std::string list;
            for (const auto& m : modules) list += (list.empty() ? "" : ", ") + m;
            markup += "  <span alpha=\"60%\">· used " + std::to_string(row.uses.size()) + "× via " + esc(list) + "</span>";
        }
        std::optional<Target> target;
        if (!row.definitions.empty()) target = Target{row.definitions.back().file, row.definitions.back().line};
        else if (!row.uses.empty()) target = Target{row.uses.front().file, row.uses.front().line};
        addRow("kevc-const-variable", markup, target);
    }
    summary_->set_markup(std::to_string(report.rows.size()) + " variables" +
                        (serious ? "  ·  <span foreground=\"#ff6b6b\">" + std::to_string(serious) + " problem" +
                                       (serious == 1 ? "" : "s") + "</span>"
                                 : std::string("  ·  no problems")));
}

} // namespace kevc
