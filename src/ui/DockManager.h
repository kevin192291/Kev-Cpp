#pragma once

#include <gtkmm.h>

#include <string>
#include <utility>
#include <vector>

namespace kevc {

// Tool panels (Compiler, Run, Terminal, ...) that live in the bottom dock, the right dock, or a
// window of their own. Tabs can be dragged between docks or out of the window, or moved with the
// menu on each tab. The arrangement is saved in ~/.config/kev-cpp/layout.json.
class DockManager : public sigc::trackable {
public:
    enum class Area { Bottom, Right, Floating };

    // `rightPaned` holds the editor area (start) and `right` (end); it's used to size the right dock.
    DockManager(Gtk::ApplicationWindow& main, Gtk::Notebook& bottom, Gtk::Notebook& right, Gtk::Paned& rightPaned);
    ~DockManager();

    // `icon` is a kevc-* icon name, shown on the panel's tab.
    void add(const std::string& id, const Glib::ustring& title, Gtk::Widget& widget, const char* icon);
    void move(Gtk::Widget& widget, Area area);
    // Brings a panel to the front: selects its tab and raises its window if it floats. A closed
    // panel comes back where it was.
    void show(Gtk::Widget& widget);
    // Closed panels leave their dock; reopening one puts it back where it was.
    void setClosed(const std::string& id, bool closed);
    bool closed(const std::string& id) const;
    // Panel ids and titles, in the order they were added (for View → Panels).
    std::vector<std::pair<std::string, Glib::ustring>> panels() const;
    // A panel was closed or reopened, by its tab menu (byUser) or by code such as show().
    sigc::signal<void(const std::string& id, bool closed, bool byUser)>& signalClosedChanged() { return closedChanged_; }
    void resetLayout();
    void restoreLayout();
    // Dropping a panel tab anywhere on `area` that isn't a dock opens the panel in its own window.
    void acceptFloatingDrops(Gtk::Widget& area);

private:
    struct Panel {
        std::string id;
        Glib::ustring title;
        Gtk::Widget* widget;
        const char* icon;
        bool closed = false; // out of every dock; we hold a reference so it isn't destroyed
        Area lastArea = Area::Bottom; // where a closed panel comes back
    };
    struct Floating {
        Gtk::Window* window;
        Gtk::Notebook* notebook;
    };

    void close(Panel& panel, bool byUser);
    void reopen(Panel& panel, bool byUser);
    Panel* find(Gtk::Widget& widget);
    Panel* find(const std::string& id);
    // The panel a drag carries (its widget, as a GValue), or nullptr.
    Panel* panelOf(const Glib::ValueBase& value);
    Gtk::Notebook* notebookOf(Gtk::Widget& widget);
    Area areaOf(const Gtk::Notebook* notebook) const;
    void setupNotebook(Gtk::Notebook& notebook);
    void attach(Panel& panel, Gtk::Notebook& notebook);
    Gtk::Widget& makeTabLabel(const Panel& panel);
    Gtk::Notebook* newFloatingWindow(const Glib::ustring& title);
    void closeFloating(Gtk::Window* window, bool redock);
    void updateVisibility();
    void saveLayoutSoon();
    void saveLayout();
    // Puts a panel into `to` before tab `position` (-1: at the end), from wherever it is.
    void moveTo(Gtk::Widget& widget, Gtk::Notebook& to, int position);
    // The tab a drop at (x, y) on `notebook` lands before, or -1 for the end.
    int dropPosition(Gtk::Notebook& notebook, double x, double y);
    bool isDock(const Gtk::Notebook* notebook) const;

    Gtk::ApplicationWindow& main_;
    Gtk::Notebook& bottom_;
    Gtk::Notebook& right_;
    Gtk::Paned& rightPaned_;
    std::vector<Panel> panels_;
    std::vector<Floating> floating_;
    Glib::RefPtr<Gio::SimpleActionGroup> actions_;
    bool restoring_ = false;
    // Nothing is saved until the previous layout has been read back; otherwise the default layout
    // set up at startup could overwrite it first.
    bool restored_ = false;
    bool shuttingDown_ = false;
    bool rightSized_ = false;
    sigc::connection saveTimer_;
    sigc::signal<void(const std::string&, bool, bool)> closedChanged_;
};

} // namespace kevc
