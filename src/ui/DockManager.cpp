#include "ui/DockManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace kevc {

namespace {

constexpr const char* kGroup = "kevc-panels";
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
    notebook.set_group_name(kGroup);
    notebook.set_scrollable(true);
    g_signal_connect(notebook.gobj(), "create-window", G_CALLBACK(&DockManager::onCreateWindow), this);
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
    box->append(*Gtk::make_managed<Gtk::Label>(panel.title));

    auto menu = Gio::Menu::create();
    auto item = [&](const char* label, const char* where) {
        auto i = Gio::MenuItem::create(label, "");
        i->set_action_and_target("dock.move", Glib::Variant<Glib::ustring>::create(panel.id + ":" + where));
        menu->append_item(i);
    };
    item("Dock at Bottom", "bottom");
    item("Dock on Right", "right");
    item("Open in Separate Window", "window");
    auto* button = Gtk::make_managed<Gtk::MenuButton>();
    button->set_icon_name("view-more-symbolic");
    button->set_has_frame(false);
    button->set_menu_model(menu);
    button->set_tooltip_text("Move this panel (or drag the tab)");
    button->add_css_class("kevc-tab-menu");
    box->append(*button);
    return *box;
}

void DockManager::attach(Panel& panel, Gtk::Notebook& notebook) {
    notebook.append_page(*panel.widget, makeTabLabel(panel));
    notebook.set_tab_reorderable(*panel.widget, true);
    notebook.set_tab_detachable(*panel.widget, true);
}

void DockManager::add(const std::string& id, const Glib::ustring& title, Gtk::Widget& widget) {
    panels_.push_back({id, title, &widget});
    attach(panels_.back(), bottom_);
}

void DockManager::move(Gtk::Widget& widget, Area area) {
    Panel* panel = find(widget);
    if (!panel) return;
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

void DockManager::show(Gtk::Widget& widget) {
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
    // GtkNotebook only opens a window itself when nothing accepts the drop. Over the editor (a text
    // drop target) or the bare X11 root window that never happens and the drag is left hanging, so
    // the window accepts panel tabs itself.
    auto target = Gtk::DropTarget::create(GTK_TYPE_NOTEBOOK_PAGE, Gdk::DragAction::MOVE);
    target->signal_drop().connect(
        [this](const Glib::ValueBase& value, double, double) {
            auto* page = GTK_NOTEBOOK_PAGE(g_value_get_object(value.gobj()));
            GtkWidget* child = page ? gtk_notebook_page_get_child(page) : nullptr;
            Gtk::Widget* widget = nullptr;
            for (auto& p : panels_)
                if (p.widget->gobj() == child) widget = p.widget;
            if (!widget) return false;
            // Let the notebook finish its drag before the page moves.
            Glib::signal_idle().connect_once([this, widget] { move(*widget, Area::Floating); });
            return true;
        },
        false);
    area.add_controller(target);
}

GtkNotebook* DockManager::onCreateWindow(GtkNotebook*, GtkWidget* page, gpointer data) {
    // A tab was dropped outside every notebook: give it a window of its own.
    auto* self = static_cast<DockManager*>(data);
    Glib::ustring title = "Panel";
    for (auto& p : self->panels_)
        if (p.widget->gobj() == page) title = p.title;
    return self->newFloatingWindow(title)->gobj();
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
    // Restore the original tab order.
    for (size_t i = 0; i < panels_.size(); ++i) bottom_.reorder_child(*panels_[i].widget, static_cast<int>(i));
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
    for (auto& p : panels_)
        if (Gtk::Notebook* nb = notebookOf(*p.widget)) panels[p.id] = areaName(areaOf(nb));
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
