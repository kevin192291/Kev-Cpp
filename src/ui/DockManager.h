#pragma once

#include <gtkmm.h>

#include <string>
#include <vector>

namespace kevc {

// Tool panels (Compiler, Run, Terminal, ...) that live in the bottom dock, the right dock, or a
// window of their own. Tabs can be dragged between docks or out of the window, or moved with the
// menu on each tab. The arrangement is saved in ~/.config/kev-cpp/layout.json.
class DockManager {
public:
    enum class Area { Bottom, Right, Floating };

    // `rightPaned` holds the editor area (start) and `right` (end); it's used to size the right dock.
    DockManager(Gtk::ApplicationWindow& main, Gtk::Notebook& bottom, Gtk::Notebook& right, Gtk::Paned& rightPaned);
    ~DockManager();

    void add(const std::string& id, const Glib::ustring& title, Gtk::Widget& widget);
    void move(Gtk::Widget& widget, Area area);
    // Brings a panel to the front: selects its tab and raises its window if it floats.
    void show(Gtk::Widget& widget);
    void resetLayout();
    void restoreLayout();
    // Dropping a panel tab anywhere on `area` that isn't a dock opens the panel in its own window.
    void acceptFloatingDrops(Gtk::Widget& area);

private:
    struct Panel {
        std::string id;
        Glib::ustring title;
        Gtk::Widget* widget;
    };
    struct Floating {
        Gtk::Window* window;
        Gtk::Notebook* notebook;
    };

    Panel* find(Gtk::Widget& widget);
    Panel* find(const std::string& id);
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
    static GtkNotebook* onCreateWindow(GtkNotebook* notebook, GtkWidget* page, gpointer self);

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
};

} // namespace kevc
