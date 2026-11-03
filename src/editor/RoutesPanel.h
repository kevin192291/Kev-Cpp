#pragma once

#include "core/SvelteKit.h"

#include "editor/RoutesPanel.ui.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace kevc {

// The Routes tab for SvelteKit projects: every route under src/routes with its files, what each
// exports (load, actions, HTTP verbs), where a page's `data` comes from, and remote functions. The
// field on top keeps the routes whose URL, files or exports hold every word typed.
class RoutesPanel : public RoutesPanelUi {
public:
    static Glib::RefPtr<RoutesPanel> create() { return RoutesPanelUi::create<RoutesPanel>(); }
    RoutesPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    void setApp(const kit::App& app, const std::filesystem::path& root);
    void showMessage(const Glib::ustring& text);

    sigc::signal<void(const std::filesystem::path& file, int line)>& signalOpen() { return open_; }
    // A page route's URL pattern, e.g. "/blog/[slug]".
    sigc::signal<void(const std::string& url)>& signalOpenInBrowser() { return openInBrowser_; }
    sigc::signal<void()>& signalNewRoute() { return newRoute_; }

protected:
    void on_filter_search_changed() override;
    void on_filter_stop_search() override;
    void on_new_route_clicked() override;

private:
    struct Target {
        std::filesystem::path file;
        int line = 1;
    };
    Gtk::ListBoxRow& addRow(Gtk::Widget& content, int depth, std::optional<Target> target);
    Gtk::Widget& line(const char* icon, const Glib::ustring& markup);
    // Rows added after this belong to a group (a route and its files) that the filter shows or hides
    // as one, by `text`.
    void beginGroup(std::string text);
    void onFilterChanged();

    std::vector<std::string> groupText_;
    std::map<Gtk::ListBoxRow*, size_t> rowGroup_;
    Glib::ustring emptyMessage_; // shown when there are no routes at all
    std::map<Gtk::ListBoxRow*, Target> targets_;
    sigc::signal<void(const std::filesystem::path&, int)> open_;
    sigc::signal<void(const std::string&)> openInBrowser_;
    sigc::signal<void()> newRoute_;
};

} // namespace kevc
