#include "ui/WelcomePage.h"

#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;

namespace kevc {

WelcomePage::WelcomePage(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, RecentProjects& recent)
    : WelcomePageUi(object, builder), recent_(recent) {
    refresh();
}

void WelcomePage::refresh() {
    while (Gtk::Widget* child = lists_->get_first_child()) lists_->remove(*child);
    rows_.clear();
    std::error_code ec;
    auto section = [&](const Glib::ustring& title, const std::vector<fs::path>& folders, bool pinned) {
        if (folders.empty()) return;
        auto* heading = Gtk::make_managed<Gtk::Label>();
        heading->set_markup("<b>" + Glib::Markup::escape_text(title) + "</b>");
        heading->set_xalign(0);
        heading->set_margin_top(lists_->get_first_child() ? 12 : 0);
        lists_->append(*heading);
        auto* list = Gtk::make_managed<Gtk::ListBox>();
        list->set_selection_mode(Gtk::SelectionMode::NONE);
        list->add_css_class("kevc-welcome-list");
        for (const auto& folder : folders) list->append(row(folder, pinned));
        list->signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
            auto it = rows_.find(row);
            if (it == rows_.end()) return;
            const fs::path folder = it->second;
            std::error_code missing;
            if (!fs::is_directory(folder, missing)) {
                status_.emit(folder.string() + " no longer exists");
                return;
            }
            open_.emit(folder);
        });
        lists_->append(*list);
    };
    section("Pinned", recent_.pinned(), true);
    std::vector<fs::path> recent;
    for (const auto& folder : recent_.recent())
        if (fs::is_directory(folder, ec)) recent.push_back(folder); // gone folders aren't worth a row
    section("Recent", recent, false);
    if (!lists_->get_first_child()) {
        auto* hint = Gtk::make_managed<Gtk::Label>("Projects and folders you open are listed here.\n"
                                                   "Pin one to keep it at the top.");
        hint->add_css_class("dim-label");
        hint->set_xalign(0);
        lists_->append(*hint);
    }
}

Gtk::Widget& WelcomePage::row(const fs::path& folder, bool pinned) {
    std::error_code ec;
    const bool exists = fs::is_directory(folder, ec);
    auto* pin = Gtk::make_managed<Gtk::Button>();
    pin->set_icon_name(pinned ? "kevc-pinned" : "kevc-unpinned");
    pin->set_has_frame(false);
    pin->set_valign(Gtk::Align::CENTER);
    pin->set_tooltip_text(pinned ? "Unpin" : "Pin, to keep it at the top of this list");
    pin->add_css_class("kevc-pin");
    pin->signal_clicked().connect([this, folder, pinned] {
        recent_.setPinned(folder, !pinned);
        // Later: rebuilding the list now would destroy this button inside its own handler.
        Glib::signal_idle().connect_once(sigc::track_obj([this] { refresh(); }, *this));
    });

    // ~ for the home folder, so the paths stay short.
    std::string shown = folder.string();
    if (const char* home = std::getenv("HOME"); home && *home && shown.rfind(std::string(home) + "/", 0) == 0)
        shown = "~" + shown.substr(std::strlen(home));
    auto* name = Gtk::make_managed<Gtk::Label>();
    name->set_markup("<b>" + Glib::Markup::escape_text(folder.filename().string()) + "</b>" +
                     (exists ? "" : "  <span alpha=\"60%\">(not found)</span>"));
    name->set_xalign(0);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    name->set_max_width_chars(48);
    auto* path = Gtk::make_managed<Gtk::Label>(shown);
    path->set_xalign(0);
    path->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    path->set_max_width_chars(48); // long paths are shortened in the middle, not widening the list
    path->add_css_class("dim-label");
    auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    text->set_hexpand(true);
    text->append(*name);
    text->append(*path);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->set_margin(4);
    box->append(*pin);
    box->append(*text);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*box);
    row->set_tooltip_text(exists ? "Open " + folder.string() : folder.string() + " no longer exists");
    rows_[row] = folder;
    return *row;
}

} // namespace kevc
