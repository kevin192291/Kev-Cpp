#pragma once

#include "core/SvelteKit.h"

#include <gtkmm.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace kevc {

// The Routes tab for SvelteKit projects: every route under src/routes with its files, what each
// exports (load, actions, HTTP verbs), where a page's `data` comes from, and remote functions.
class RoutesPanel : public Gtk::Box {
public:
    RoutesPanel();

    void setApp(const kit::App& app, const std::filesystem::path& root);
    void showMessage(const Glib::ustring& text);

    sigc::signal<void(const std::filesystem::path& file, int line)>& signalOpen() { return open_; }
    // A page route's URL pattern, e.g. "/blog/[slug]".
    sigc::signal<void(const std::string& url)>& signalOpenInBrowser() { return openInBrowser_; }
    sigc::signal<void()>& signalNewRoute() { return newRoute_; }

private:
    struct Target {
        std::filesystem::path file;
        int line = 1;
    };
    Gtk::ListBoxRow& addRow(Gtk::Widget& content, int depth, std::optional<Target> target);
    Gtk::Widget& line(const char* icon, const Glib::ustring& markup);

    Gtk::ScrolledWindow scroller_;
    Gtk::ListBox list_;
    Gtk::Label message_;
    std::map<Gtk::ListBoxRow*, Target> targets_;
    sigc::signal<void(const std::filesystem::path&, int)> open_;
    sigc::signal<void(const std::string&)> openInBrowser_;
    sigc::signal<void()> newRoute_;
};

} // namespace kevc
