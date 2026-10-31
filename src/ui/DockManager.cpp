#include "ui/DockManager.h"

#include "ui/IconWidgets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr int kRightDockWidth = 420;

fs::path layoutFile() {
    return fs::path(Glib::get_user_config_dir()) / "kev-cpp" / "layout.json";
}

const char* areaName(DockManager::Area area) {
    switch (area) {
    case DockManager::Area::Bottom: return "bottom";
    case DockManager::Area::Right: return "right";
    case DockManager::Area::Floating: return "window";
    }
    return "bottom";
}

} // namespace

DockManager::DockManager(Gtk::ApplicationWindow& main, Gtk::Notebook& bottom, Gtk::Notebook& right,
                         Gtk::Paned& rightPaned)
    : main_(main), bottom_(bottom), right_(right), rightPaned_(rightPaned) {
    actions_ = Gio::SimpleActionGroup::create();
    actions_->add_action_with_parameter("move", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& v) {
        // "<panel id>:<bottom|right|window>"
        const std::string arg = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get();
        const auto colon = arg.rfind(':');
        Panel* panel = find(arg.substr(0, colon));
        if (!panel) return;
        const std::string where = arg.substr(colon + 1);
        // Defer: the menu that triggered this lives in the tab being moved.
        Gtk::Widget* widget = panel->widget;
        const Area area = where == "right" ? Area::Right : where == "window" ? Area::Floating : Area::Bottom;
        Glib::signal_idle().connect_once([this, widget, area] { move(*widget, area); });
    });
    actions_->add_action_with_parameter("close", Glib::VARIANT_TYPE_STRING, [this](const Glib::VariantBase& v) {
        const std::string id = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get();
        // Defer, like "move": the menu that triggered this lives in the tab being closed.
        Glib::signal_idle().connect_once([this, id] {
            if (Panel* panel = find(id); panel && !panel->closed) close(*panel, true);
        });
    });
    main_.insert_action_group("dock", actions_);
    setupNotebook(bottom_);
    setupNotebook(right_);
    updateVisibility();
}

DockManager::~DockManager() {
    shuttingDown_ = true;
    saveTimer_.disconnect();
    // Empty the floating windows before deleting them: their panels belong to the main window.
    for (auto& f : floating_) {
        while (f.notebook->get_n_pages() > 0) f.notebook->remove_page(0);
        delete f.window;
    }
    floating_.clear();
    for (auto& p : panels_)
        if (p.closed) g_object_unref(p.widget->gobj());
}

void DockManager::close(Panel& panel, bool byUser) {
    Gtk::Notebook* from = notebookOf(*panel.widget);
    panel.lastArea = from ? areaOf(from) : panel.lastArea;
    panel.closed = true;
    // Our reference keeps the panel alive outside any dock (some panels are managed widgets).
    g_object_ref(panel.widget->gobj());
    if (from) from->remove_page(*panel.widget);
    saveLayoutSoon();
    closedChanged_.emit(panel.id, true, byUser);
}

void DockManager::reopen(Panel& panel, bool byUser) {
    panel.closed = false;
    Gtk::Notebook* to = panel.lastArea == Area::Bottom  ? &bottom_
                        : panel.lastArea == Area::Right ? &right_
                                                        : newFloatingWindow(panel.title);
    attach(panel, *to);
    g_object_unref(panel.widget->gobj());
    closedChanged_.emit(panel.id, false, byUser);
}

void DockManager::setClosed(const std::string& id, bool closed) {
    Panel* panel = find(id);
    if (!panel || panel->closed == closed) return;
    if (closed) close(*panel, false);
    else reopen(*panel, false);
}

bool DockManager::closed(const std::string& id) const {
    for (const auto& p : panels_)
        if (p.id == id) return p.closed;
    return false;
}

std::vector<std::pair<std::string, Glib::ustring>> DockManager::panels() const {
    std::vector<std::pair<std::string, Glib::ustring>> out;
    for (const auto& p : panels_) out.emplace_back(p.id, p.title);
    return out;
}

DockManager::Panel* DockManager::find(Gtk::Widget& widget) {
    for (auto& p : panels_)
        if (p.widget == &widget) return &p;
    return nullptr;
}

DockManager::Panel* DockManager::find(const std::string& id) {
    for (auto& p : panels_)
        if (p.id == id) return &p;
    return nullptr;
}

DockManager::Panel* DockManager::panelOf(const Glib::ValueBase& value) {
    if (!G_VALUE_HOLDS(value.gobj(), GTK_TYPE_WIDGET)) return nullptr;
    const auto* widget = static_cast<GtkWidget*>(g_value_get_object(value.gobj()));
    for (auto& p : panels_)
        if (p.widget->gobj() == widget) return &p;
    return nullptr;
}

bool DockManager::isDock(const Gtk::Notebook* notebook) const {
    if (notebook == &bottom_ || notebook == &right_) return true;
    return std::any_of(floating_.begin(), floating_.end(), [notebook](const Floating& f) { return f.notebook == notebook; });
}

Gtk::Notebook* DockManager::notebookOf(Gtk::Widget& widget) {
    if (bottom_.page_num(widget) >= 0) return &bottom_;
    if (right_.page_num(widget) >= 0) return &right_;
    for (auto& f : floating_)
        if (f.notebook->page_num(widget) >= 0) return f.notebook;
    return nullptr;
}

DockManager::Area DockManager::areaOf(const Gtk::Notebook* notebook) const {
    if (notebook == &bottom_) return Area::Bottom;
    if (notebook == &right_) return Area::Right;
    return Area::Floating;
}

void DockManager::setupNotebook(Gtk::Notebook& notebook) {
    notebook.set_scrollable(true);
    // A panel tab dropped anywhere on a dock (its tabs or its panel) goes into it.
    auto target = Gtk::DropTarget::create(GTK_TYPE_WIDGET, Gdk::DragAction::MOVE);
    target->signal_drop().connect(
        [this, nb = &notebook](const Glib::ValueBase& value, double x, double y) {
            Panel* panel = panelOf(value);
            if (!panel) return false;
            Gtk::Widget* widget = panel->widget;
            const int position = dropPosition(*nb, x, y);
            // Let the drag finish before its source tab moves.
            Glib::signal_idle().connect_once([this, widget, nb, position] {
                if (isDock(nb)) moveTo(*widget, *nb, position);
            });
            // GtkNotebook switches to a tab a drag rests on for 500 ms, even after the drop; select
            // the dropped panel again once that can no longer happen.
            Glib::signal_timeout().connect_once(sigc::track_obj([this, widget] { show(*widget); }, *this), 600);
            return true;
        },
        false);
    notebook.add_controller(target);
    auto changed = [this, nb = &notebook](Gtk::Widget*, guint) {
        if (shuttingDown_) return;
        updateVisibility();
        saveLayoutSoon();
        // A floating window whose last panel was dragged or moved away closes itself.
        if (nb->get_n_pages() == 0) {
            for (auto& f : floating_)
                if (f.notebook == nb) {
                    Gtk::Window* w = f.window;
                    Glib::signal_idle().connect_once([this, w] { closeFloating(w, false); });
                }
        }
    };
    notebook.signal_page_added().connect(changed);
    notebook.signal_page_removed().connect(changed);
}

Gtk::Widget& DockManager::makeTabLabel(const Panel& panel) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    box->append(*makeIconLabel(panel.icon, panel.title));

    auto menu = Gio::Menu::create();
    auto item = [&](const char* label, const char* where) {
        auto i = Gio::MenuItem::create(label, "");
        i->set_action_and_target("dock.move", Glib::Variant<Glib::ustring>::create(panel.id + ":" + where));
        menu->append_item(i);
    };
    item("Dock at Bottom", "bottom");
    item("Dock on Right", "right");
    item("Open in Separate Window", "window");
    auto closeSection = Gio::Menu::create();
    auto closeItem = Gio::MenuItem::create("Close Panel", "");
    closeItem->set_action_and_target("dock.close", Glib::Variant<Glib::ustring>::create(panel.id));
    closeSection->append_item(closeItem);
    menu->append_section(closeSection);
    auto* button = Gtk::make_managed<Gtk::MenuButton>();
    button->set_icon_name("view-more-symbolic");
    button->set_has_frame(false);
    button->set_menu_model(menu);
    button->set_tooltip_text("Move this panel (or drag the tab)");
    button->add_css_class("kevc-tab-menu");
    box->append(*button);

    // Tabs are dragged with our own drag source rather than GtkNotebook's detachable tabs. Those
    // offer the tab to every program on the desktop, and GTK 4.14 crashes when one of them reads it
    // during the drag before the tab is dropped on another notebook (Cinnamon's desktop does). This
    // drag carries the panel widget, which only Kev-C++ can take.
    auto source = Gtk::DragSource::create();
    source->set_actions(Gdk::DragAction::MOVE);
    Gtk::Widget* widget = panel.widget;
    auto hotspot = std::make_shared<std::pair<double, double>>(0.0, 0.0);
    source->signal_prepare().connect(
        [widget, hotspot](double x, double y) {
            *hotspot = {x, y};
            GValue value = G_VALUE_INIT;
            g_value_init(&value, GTK_TYPE_WIDGET);
            g_value_set_object(&value, widget->gobj());
            auto content = Glib::wrap(gdk_content_provider_new_for_value(&value));
            g_value_unset(&value);
            return content;
        },
        false);
    Gtk::DragSource* src = source.get(); // not the RefPtr: the controller would keep itself alive
    source->signal_drag_begin().connect([src, box, hotspot](const Glib::RefPtr<Gdk::Drag>&) {
        src->set_icon(Gtk::WidgetPaintable::create(*box), static_cast<int>(hotspot->first),
                      static_cast<int>(hotspot->second));
    });
    // Dropped outside every Kev-C++ window: give the panel a window of its own.
    source->signal_drag_cancel().connect(
        [this, widget](const Glib::RefPtr<Gdk::Drag>&, Gdk::DragCancelReason reason) {
            if (reason != Gdk::DragCancelReason::NO_TARGET) return false;
            Glib::signal_idle().connect_once([this, widget] { move(*widget, Area::Floating); });
            return true;
        },
        false);
    box->add_controller(source);
    return *box;
}

void DockManager::attach(Panel& panel, Gtk::Notebook& notebook) {
    notebook.append_page(*panel.widget, makeTabLabel(panel));
}

void DockManager::add(const std::string& id, const Glib::ustring& title, Gtk::Widget& widget, const char* icon) {
    panels_.push_back({id, title, &widget, icon});
    attach(panels_.back(), bottom_);
}

void DockManager::move(Gtk::Widget& widget, Area area) {
    Panel* panel = find(widget);
    if (!panel) return;
    if (panel->closed) {
        panel->lastArea = area; // where it comes back when reopened
        return;
    }
    Gtk::Notebook* from = notebookOf(widget);
    if (from && areaOf(from) == area && area != Area::Floating) {
        show(widget);
        return;
    }
    if (from && area == Area::Floating && from->get_n_pages() == 1 && areaOf(from) == Area::Floating) {
        show(widget); // already alone in its own window
        return;
    }
    Gtk::Notebook* to = area == Area::Bottom ? &bottom_ : area == Area::Right ? &right_ : newFloatingWindow(panel->title);
    // Keep the panel alive while it has no parent (some panels are managed widgets).
    g_object_ref(widget.gobj());
    if (from) from->remove_page(widget);
    attach(*panel, *to);
    g_object_unref(widget.gobj());
    show(widget);
}

void DockManager::moveTo(Gtk::Widget& widget, Gtk::Notebook& to, int position) {
    Panel* panel = find(widget);
    if (!panel) return;
    Gtk::Notebook* from = notebookOf(widget);
    if (from == &to) {
        // Dropped on its own dock: a reorder. Tabs after it move left once it's taken out.
        const int current = to.page_num(widget);
        if (position < 0) position = to.get_n_pages() - 1;
        else if (position > current) --position;
        to.reorder_child(widget, position);
        show(widget);
        return;
    }
    g_object_ref(widget.gobj()); // keep it alive while it has no parent (some panels are managed)
    if (from) from->remove_page(widget);
    attach(*panel, to);
    if (position >= 0) to.reorder_child(widget, position);
    g_object_unref(widget.gobj());
    show(widget);
}

int DockManager::dropPosition(Gtk::Notebook& notebook, double x, double y) {
    for (int i = 0; i < notebook.get_n_pages(); ++i) {
        Gtk::Widget* tab = notebook.get_tab_label(*notebook.get_nth_page(i));
        graphene_rect_t r;
        if (!tab || !gtk_widget_compute_bounds(tab->gobj(), GTK_WIDGET(notebook.gobj()), &r)) continue;
        // Only drops on the tab row pick a place; a drop on the panel itself appends.
        const double top = r.origin.y - 8, bottom = r.origin.y + r.size.height + 8;
        if (y < top || y > bottom) return -1;
        if (x < r.origin.x + r.size.width / 2) return i;
    }
    return -1;
}

void DockManager::show(Gtk::Widget& widget) {
    if (Panel* panel = find(widget); panel && panel->closed) reopen(*panel, false);
    Gtk::Notebook* nb = notebookOf(widget);
    if (!nb) return;
    nb->set_visible(true);
    nb->set_current_page(nb->page_num(widget));
    if (areaOf(nb) == Area::Floating)
        if (auto* window = dynamic_cast<Gtk::Window*>(nb->get_root())) window->present();
}

Gtk::Notebook* DockManager::newFloatingWindow(const Glib::ustring& title) {
    auto* window = new Gtk::Window();
    window->set_title(title + " — Kev-C++");
    window->set_default_size(760, 420);
    window->set_transient_for(main_);
    // Same keyboard shortcuts as the main window (F9, F10, F5, ...) and the panel menus.
    if (auto app = main_.get_application()) app->add_window(*window);
    window->insert_action_group("win", Glib::wrap(G_ACTION_GROUP(main_.gobj()), true));
    window->insert_action_group("dock", actions_);

    auto* notebook = Gtk::make_managed<Gtk::Notebook>();
    setupNotebook(*notebook);
    notebook->signal_switch_page().connect([this, window](Gtk::Widget* page, guint) {
        if (Panel* p = page ? find(*page) : nullptr) window->set_title(p->title + " — Kev-C++");
    });
    window->set_child(*notebook);
    // Closing a panel window docks its panels back at the bottom rather than losing them.
    window->signal_close_request().connect(
        [this, window] {
            closeFloating(window, true);
            return true;
        },
        false);
    floating_.push_back({window, notebook});
    window->present();
    return notebook;
}

void DockManager::closeFloating(Gtk::Window* window, bool redock) {
    auto it = std::find_if(floating_.begin(), floating_.end(), [window](const Floating& f) { return f.window == window; });
    if (it == floating_.end()) return;
    Gtk::Notebook* nb = it->notebook;
    if (redock) {
        std::vector<Gtk::Widget*> pages;
        for (int i = 0; i < nb->get_n_pages(); ++i) pages.push_back(nb->get_nth_page(i));
        for (Gtk::Widget* page : pages) move(*page, Area::Bottom);
    }
    if (nb->get_n_pages() > 0) return; // something we don't manage is still in there
    floating_.erase(it);
    window->set_visible(false);
    Glib::signal_idle().connect_once([window] { delete window; });
    saveLayoutSoon();
}

void DockManager::acceptFloatingDrops(Gtk::Widget& area) {
    // A panel tab dropped on the window but not on a dock (e.g. on the editor) gets its own window.
    auto target = Gtk::DropTarget::create(GTK_TYPE_WIDGET, Gdk::DragAction::MOVE);
    target->signal_drop().connect(
        [this](const Glib::ValueBase& value, double, double) {
            Panel* panel = panelOf(value);
            if (!panel) return false;
            Gtk::Widget* widget = panel->widget;
            // Let the drag finish before its source tab moves.
            Glib::signal_idle().connect_once([this, widget] { move(*widget, Area::Floating); });
            return true;
        },
        false);
    area.add_controller(target);
}

void DockManager::updateVisibility() {
    bottom_.set_visible(bottom_.get_n_pages() > 0);
    const bool showRight = right_.get_n_pages() > 0;
    right_.set_visible(showRight);
    if (showRight && !rightSized_) {
        rightSized_ = true;
        const int width = rightPaned_.get_width();
        rightPaned_.set_position(width > kRightDockWidth * 2 ? width - kRightDockWidth : 820);
    }
    if (!showRight) rightSized_ = false;
}

void DockManager::resetLayout() {
    for (auto& p : panels_) move(*p.widget, Area::Bottom);
    // Restore the original tab order (closed panels stay closed, but come back at the bottom).
    int position = 0;
    for (auto& p : panels_)
        if (!p.closed) bottom_.reorder_child(*p.widget, position++);
    if (!panels_.empty()) bottom_.set_current_page(0);
}

void DockManager::saveLayoutSoon() {
    if (!restored_ || restoring_ || shuttingDown_) return;
    saveTimer_.disconnect();
    saveTimer_ = Glib::signal_timeout().connect(
        [this] {
            saveLayout();
            return false;
        },
        400);
}

void DockManager::saveLayout() {
    nlohmann::json panels = nlohmann::json::object();
    for (auto& p : panels_) {
        if (p.closed) panels[p.id] = areaName(p.lastArea);
        else if (Gtk::Notebook* nb = notebookOf(*p.widget)) panels[p.id] = areaName(areaOf(nb));
    }
    std::error_code ec;
    fs::create_directories(layoutFile().parent_path(), ec);
    std::ofstream(layoutFile()) << nlohmann::json{{"version", 1}, {"panels", panels}}.dump(2) << "\n";
}

void DockManager::restoreLayout() {
    restored_ = true;
    std::ifstream in(layoutFile());
    if (!in) return;
    nlohmann::json layout;
    try {
        layout = nlohmann::json::parse(in);
    } catch (const std::exception&) {
        return; // a damaged layout file just means the default layout
    }
    restoring_ = true;
    const nlohmann::json panels = layout.value("panels", nlohmann::json::object()); // keep alive while iterating
    for (auto& [id, where] : panels.items()) {
        Panel* p = find(id);
        if (!p || !where.is_string()) continue;
        const std::string w = where.get<std::string>();
        if (w == "right") move(*p->widget, Area::Right);
        else if (w == "window") move(*p->widget, Area::Floating);
    }
    restoring_ = false;
    if (!panels_.empty() && bottom_.get_n_pages() > 0) bottom_.set_current_page(0);
}

} // namespace kevc
