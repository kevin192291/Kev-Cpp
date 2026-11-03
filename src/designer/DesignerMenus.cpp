// DesignerPage's menus: the outline's rows for a <menu> and what's in it, the properties of a menu,
// submenu, section or item, and adding, pasting and highlighting them. A menu isn't a widget, so it
// has no place of its own in the preview: selecting one outlines the widget that shows it.
#include "designer/DesignerPage.h"

namespace kevc {

using ui::MenuPart;
using ui::Node;
using ui::UiDocument;

namespace {

const char* partName(MenuPart part) {
    switch (part) {
    case MenuPart::Menu: return "Menu";
    case MenuPart::Submenu: return "Submenu";
    case MenuPart::Section: return "Section";
    case MenuPart::Item: return "Item";
    case MenuPart::None: break;
    }
    return "";
}

const char* partIcon(MenuPart part) {
    switch (part) {
    case MenuPart::Menu: return "kevc-namespace";
    case MenuPart::Submenu: return "kevc-folder";
    case MenuPart::Section: return "kevc-enum";
    default: return "kevc-function";
    }
}

Gtk::Label& note(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->set_wrap(true);
    label->set_xalign(0);
    label->add_css_class("dim-label");
    label->set_margin_top(8);
    return *label;
}

// The attributes a part can have, in the order they're listed: name, what it's for.
std::vector<std::pair<const char*, const char*>> attributesOf(MenuPart part) {
    std::vector<std::pair<const char*, const char*>> out;
    if (part == MenuPart::Menu) return out;
    out.push_back({"label", "What it says. An underscore marks the letter that picks it from the keyboard (_File)"});
    if (part == MenuPart::Section) return out;
    if (part == MenuPart::Item) {
        out.push_back(
            {"action", "What it runs: win.<name> is one of the window's actions, app.<name> the application's"});
        out.push_back({"target", "The action's parameter, as GVariant text ('debug' with the quotes for a string)"});
        out.push_back({"accel", "The shortcut shown beside it, such as <Control>s"});
    }
    out.push_back({"icon", "An icon name, shown where the menu has room for icons"});
    return out;
}

} // namespace

bool DesignerPage::selectedIsMenuPart() const {
    const Node* n = doc_.find(selected_);
    return n && ui::menuPart(*n) != MenuPart::None;
}

void DesignerPage::addMenuRows(const Node& part, int depth) {
    const MenuPart kind = ui::menuPart(part);
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    box->set_margin_start(6 + depth * 16);
    box->set_margin_top(2);
    box->set_margin_bottom(2);
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(partIcon(kind));
    box->append(*icon);
    const std::string id = part.attribute("id");
    const auto label = ui::menuAttribute(part, "label");
    Glib::ustring markup;
    auto dim = [](const std::string& text, const char* alpha = "65%") {
        return "  <span alpha=\"" + std::string(alpha) + "\">" + Glib::Markup::escape_text(text) + "</span>";
    };
    if (kind == MenuPart::Item) {
        markup = Glib::Markup::escape_text(label ? ui::menuLabelText(*label) : std::string("(no label)"));
        if (auto action = ui::menuAttribute(part, "action")) markup += dim(*action);
        if (auto hidden = ui::menuAttribute(part, "hidden-when")) markup += dim("hidden when " + *hidden, "50%");
    } else {
        if (label) markup = "<b>" + Glib::Markup::escape_text(ui::menuLabelText(*label)) + "</b>";
        else if (!id.empty()) markup = "<b>" + Glib::Markup::escape_text(id) + "</b>";
        std::string what = kind == MenuPart::Menu ? "menu" : kind == MenuPart::Submenu ? "submenu" : "section";
        if (label && !id.empty()) what = id + "  " + what;
        markup += markup.empty() ? Glib::Markup::escape_text(what) : dim(what);
    }
    auto* text = Gtk::make_managed<Gtk::Label>();
    text->set_markup(markup);
    text->set_xalign(0);
    text->set_ellipsize(Pango::EllipsizeMode::END);
    box->append(*text);
    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*box);
    outline_.append(*row);
    rowUids_[row] = part.uid;
    if (part.uid == selected_) outline_.select_row(*row);
    for (const Node* entry : ui::menuEntries(part)) addMenuRows(*entry, depth + 1);
    // An item's <link name="submenu"> holds entries of its own.
    if (kind == MenuPart::Item)
        for (const Node* link : part.childElements("link")) addMenuRows(*link, depth + 1);
}

void DesignerPage::rebuildMenuProperties(Node& part) {
    const int uid = part.uid;
    const MenuPart kind = ui::menuPart(part);
    auto* title = Gtk::make_managed<Gtk::Label>();
    title->set_markup("<big><b>" + Glib::ustring(partName(kind)) + "</b></big>  <span alpha=\"60%\">in " +
                      Glib::Markup::escape_text(ui::menuOf(part) ? ui::menuOf(part)->attribute("id") : std::string()) +
                      "</span>");
    title->set_xalign(0);
    props_.append(*title);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_column_spacing(8);
    grid->set_row_spacing(4);
    int row = 0;
    auto addRow = [&](const std::string& name, Gtk::Widget& editor, bool set, const Glib::ustring& tip,
                      std::function<void()> reset) {
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup(set ? "<b>" + Glib::Markup::escape_text(name) + "</b>" : Glib::Markup::escape_text(name));
        label->set_xalign(0);
        label->set_tooltip_text(tip);
        grid->attach(*label, 0, row);
        editor.set_hexpand(true);
        editor.set_tooltip_text(tip);
        grid->attach(editor, 1, row);
        propertyEditors_["menu:" + name] = &editor;
        if (set && reset) {
            auto* clear = Gtk::make_managed<Gtk::Button>();
            clear->set_icon_name("edit-clear-symbolic");
            clear->set_has_frame(false);
            clear->set_tooltip_text("Remove from the file");
            clear->signal_clicked().connect(reset);
            grid->attach(*clear, 2, row);
        }
        ++row;
    };
    // A text field that commits on Enter and when the focus leaves it.
    auto field = [this](const std::string& value, std::function<void(const std::string&)> commit) -> Gtk::Entry& {
        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(value);
        entry->set_width_chars(6);
        auto done = [this, entry, commit] {
            if (!syncing_) commit(entry->get_text());
        };
        entry->signal_activate().connect(done);
        auto focus = Gtk::EventControllerFocus::create();
        focus->signal_leave().connect(done);
        entry->add_controller(focus);
        return *entry;
    };

    // Menus, submenus and sections can have an id: code finds them by it to fill or change them.
    if (kind != MenuPart::Item) {
        Gtk::Entry& id = field(part.attribute("id"), [this, uid](const std::string& value) {
            Node* p = doc_.find(uid);
            if (!p || value == p->attribute("id")) return;
            if (!value.empty() && ui::usedIds(doc_).count(value)) {
                showMessage("Another object or menu already has the id \"" + value + "\".", true);
                return;
            }
            if (value.empty() && ui::menuPart(*p) == MenuPart::Menu) {
                showMessage("A menu needs an id: the widgets that show it name it.", true);
                return;
            }
            edit([&] { doc_.setAttribute(*p, "id", value); });
        });
        id.set_placeholder_text("none (code can't look it up)");
        addRow("id", id, false, "The name GtkBuilder and your code find it by", {});
    }
    for (const auto& [name, tip] : attributesOf(kind)) {
        const std::string key = name;
        const auto value = ui::menuAttribute(part, key);
        Gtk::Entry& entry = field(value.value_or(""), [this, uid, key](const std::string& text) {
            Node* p = doc_.find(uid);
            if (!p || ui::menuAttribute(*p, key).value_or("") == text) return;
            edit([&] { ui::setMenuAttribute(doc_, *p, key, text); });
        });
        addRow(key, entry, value.has_value(), tip, [this, uid, key] {
            edit([&] {
                if (Node* p = doc_.find(uid)) ui::setMenuAttribute(doc_, *p, key, "");
            });
        });
    }
    if (kind == MenuPart::Item) {
        // hidden-when: GTK hides the item rather than greying it out.
        static const std::vector<Glib::ustring> choices{"(shown, greyed out when it can't run)", "action-disabled",
                                                        "action-missing", "macos-menubar"};
        auto* drop = Gtk::make_managed<Gtk::DropDown>(choices);
        const std::string current = ui::menuAttribute(part, "hidden-when").value_or("");
        guint selected = 0;
        for (guint i = 1; i < choices.size(); ++i)
            if (choices[i].raw() == current) selected = i;
        drop->set_selected(selected);
        drop->property_selected().signal_changed().connect([this, drop, uid] {
            if (syncing_) return;
            const guint i = drop->get_selected();
            const std::string value = i == 0 || i >= choices.size() ? std::string() : std::string(choices[i]);
            edit([&] {
                if (Node* p = doc_.find(uid)) ui::setMenuAttribute(doc_, *p, "hidden-when", value);
            });
        });
        addRow("hidden-when", *drop, !current.empty(),
               "When the item isn't there at all: action-disabled hides it while its action can't run", {});
    }
    props_.append(*grid);

    // Adding entries: into a menu, submenu or section; after an item, beside it.
    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    buttons->set_margin_top(10);
    for (const auto& [label, what] : {std::pair{"Add Item", MenuPart::Item},
                                      {"Add Section", MenuPart::Section},
                                      {"Add Submenu", MenuPart::Submenu}}) {
        auto* b = Gtk::make_managed<Gtk::Button>(label);
        const MenuPart adding = what;
        b->signal_clicked().connect([this, adding] { addMenuEntry(adding); });
        buttons->append(*b);
    }
    props_.append(*buttons);

    if (kind == MenuPart::Menu)
        props_.append(note("A widget shows a menu by naming its id: a GtkPopoverMenuBar's or GtkMenuButton's "
                           "menu-model, a GtkPopoverMenu's menu-model. Selecting the menu outlines that widget."));
    else if (kind == MenuPart::Section)
        props_.append(note("A section is a group of entries with a line between it and the next. A label "
                           "shows as a heading above it."));
    else if (kind == MenuPart::Item && !ui::menuAttribute(part, "action"))
        props_.append(note("Without an action the item is greyed out: give it one the window or the application "
                           "has (win.save, app.quit)."));
}

void DesignerPage::addMenuEntry(MenuPart what) {
    Node* selection = doc_.find(selected_);
    if (!selection) return;
    // Into the selected menu, submenu or section; beside a selected item.
    Node* into = selection;
    int index = -1;
    if (ui::menuPart(*selection) == MenuPart::Item) {
        into = selection->parent;
        const auto entries = into ? ui::menuEntries(*into) : std::vector<Node*>{};
        const auto it = std::find(entries.begin(), entries.end(), selection);
        index = it == entries.end() ? -1 : int(it - entries.begin()) + 1;
    }
    if (!into || ui::menuPart(*into) == MenuPart::None || ui::menuPart(*into) == MenuPart::Item) return;
    int added = 0;
    edit([&] {
        if (Node* n = ui::addMenuPart(doc_, *into, what, index)) added = n->uid;
    });
    if (added) select(added);
}

void DesignerPage::addTopLevelMenu() {
    if (view_ != View::Design || !doc_.interface()) return;
    int added = 0;
    edit([&] {
        if (Node* n = ui::addMenu(doc_)) added = n->uid;
    });
    if (added) select(added);
    status_.emit("Added a menu: a GtkPopoverMenuBar or GtkMenuButton shows it when its menu-model names it");
}

bool DesignerPage::pasteMenuPart(const UiDocument& pasted) {
    const Node* part = nullptr;
    for (const auto& n : pasted.root().children)
        if (n->isElement() && ui::menuPart(*n) != MenuPart::None) part = n.get();
    if (!part) return false;
    const std::string xml = ui::menuPartWithUniqueIds(doc_, UiDocument::textOf(*part));
    int added = 0;
    if (ui::menuPart(*part) == MenuPart::Menu) {
        edit([&] {
            if (Node* iface = doc_.interface()) added = doc_.insertElement(*iface, xml)->uid;
        });
    } else {
        Node* selection = doc_.find(selected_);
        Node* into = selection;
        int index = -1;
        if (selection && ui::menuPart(*selection) == MenuPart::Item) {
            into = selection->parent;
            const auto entries = ui::menuEntries(*into);
            const auto it = std::find(entries.begin(), entries.end(), selection);
            index = it == entries.end() ? -1 : int(it - entries.begin()) + 1;
        }
        if (!into || ui::menuPart(*into) == MenuPart::None || ui::menuPart(*into) == MenuPart::Item) {
            showMessage("Select a menu, submenu, section or item to paste a menu entry into.", true);
            return true;
        }
        edit([&] {
            // insertElement counts all elements; the entries come after a part's attributes.
            const auto entries = ui::menuEntries(*into);
            int at = -1;
            if (index >= 0 && size_t(index) < entries.size()) {
                const auto all = into->childElements();
                at = int(std::find(all.begin(), all.end(), entries[size_t(index)]) - all.begin());
            }
            added = doc_.insertElement(*into, xml, at)->uid;
        });
    }
    if (added) select(added);
    return true;
}

int DesignerPage::shownFor(int uid) const {
    const Node* n = doc_.find(uid);
    if (!n || ui::menuPart(*n) == MenuPart::None) return uid;
    // The widget that shows the menu: its menu-model (or another property) names it.
    const Node* menu = ui::menuOf(*n);
    const std::string id = menu ? menu->attribute("id") : std::string();
    if (id.empty()) return 0;
    for (const Node* o : doc_.objects())
        for (const Node* p : o->childElements("property"))
            if (p->childElements().empty() && p->innerText() == id) return o->uid;
    return 0;
}

} // namespace kevc
