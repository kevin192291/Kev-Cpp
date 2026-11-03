#pragma once

#include "ui/WelcomePage.ui.h"

#include "core/RecentProjects.h"

#include <filesystem>
#include <map>

namespace kevc {

// The Welcome tab, shown while no file is open: the pinned and recent projects and folders beside
// the main keys. Its layout is WelcomePage.ui (edit it on Kev-C++'s Design page); this class lists
// the folders, each with a pin.
class WelcomePage : public WelcomePageUi {
public:
    static Glib::RefPtr<WelcomePage> create(RecentProjects& recent) {
        return WelcomePageUi::create<WelcomePage>(recent);
    }
    WelcomePage(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, RecentProjects& recent);

    // Lists the folders again (after one is opened, pinned or unpinned).
    void refresh();

    // A folder that exists was chosen.
    sigc::signal<void(const std::filesystem::path&)>& signalOpen() { return open_; }
    // Something for the status bar ("… no longer exists").
    sigc::signal<void(const Glib::ustring&)>& signalStatus() { return status_; }

private:
    Gtk::Widget& row(const std::filesystem::path& folder, bool pinned);

    RecentProjects& recent_;
    std::map<Gtk::ListBoxRow*, std::filesystem::path> rows_;
    sigc::signal<void(const std::filesystem::path&)> open_;
    sigc::signal<void(const Glib::ustring&)> status_;
};

} // namespace kevc
