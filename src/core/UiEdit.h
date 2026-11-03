#pragma once

#include "core/UiDocument.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// The GTK designer's structural edits: what each container holds (data/designer/catalog.json), and
// adding, moving, wrapping and removing objects in a UiDocument so the result is what GtkBuilder
// expects for that container.
namespace kevc::ui {

enum class Slots {
    None,       // not a container
    Window,     // <child> and <child type="titlebar">
    Single,     // one <child>
    Children,   // any number of <child>
    Typed,      // <child type="…"> for each of `names`, plus the `property` slot
    Properties, // one object in each property of `names`
    Grid,       // <child> with <layout><property name="column">…
    Pages,      // a `page` object per page, holding the widget in its "child" property
    Overlay,    // a main <child>, then <child type="overlay">
};

struct ClassInfo {
    std::string cls, label, group;
    Slots slots = Slots::None;
    std::vector<std::string> names;
    std::string property;
    std::string page;
    std::vector<std::pair<std::string, std::string>> defaults;
    std::string xml; // added inside a new object
};

class Catalog {
public:
    // Throws std::runtime_error.
    static Catalog parse(std::string_view json);
    const ClassInfo* find(std::string_view cls) const;
    const std::vector<ClassInfo>& classes() const { return classes_; }
    const std::vector<std::string>& groups() const { return groups_; }

private:
    std::vector<ClassInfo> classes_;
    std::vector<std::string> groups_;
};

// Where an object goes.
struct Placement {
    enum class Kind {
        TopLevel, // inside <interface>
        Child,    // a <child> (with `type`), before `before`'s or after the last
        Property, // the object of property `name`
        Grid,     // a <child> at `column`, `row`
        Page,     // a new page of a stack or notebook
    };
    Kind kind = Kind::Child;
    Node* parent = nullptr;
    std::string name;          // Child: the child type ("" for none); Property: the property
    Node* before = nullptr;    // Child: the object to go before (nullptr: last)
    int column = 0, row = 0;   // Grid
};

// An id for a new object of class `cls` ("button1", "spin_button2") that the document doesn't use.
std::string freshId(const UiDocument& doc, const std::string& cls, const std::set<std::string>& taken = {});
// Every id the document gives GtkBuilder: its objects', menus', submenus' and sections'.
std::set<std::string> usedIds(const UiDocument& doc);
// A new object as XML: an id, the catalog's defaults and extra XML.
std::string newObjectXml(const UiDocument& doc, const std::string& cls, const ClassInfo* info);
// `objectXml` with each id the document already uses changed to a new one (for pasting).
std::string withUniqueIds(const UiDocument& doc, std::string_view objectXml);
// An object's XML, without its <layout> (its place in a grid belongs to where it was).
std::string objectXml(const Node& object);

// Adds `objectXml` (an <object> element) at `where`; returns the new object.
Node* place(UiDocument& doc, const Placement& where, std::string_view objectXml, const Catalog& catalog);
// Removes an object with whatever holds it (its <child>, its property, its stack page).
void removeObject(UiDocument& doc, Node& object);
// Moves an object (re-laid out at its new depth); returns it at its new place.
Node* moveObject(UiDocument& doc, Node& object, const Placement& where, const Catalog& catalog);
// Puts a new container (`containerXml`, of a class the catalog knows) where `object` is, holding it.
Node* wrapObject(UiDocument& doc, Node& object, std::string_view containerXml, const Catalog& catalog);
// Moves an object before its previous (-1) or after its next (+1) sibling of the same kind; returns
// it at its new place, or nullptr if it can't move that way.
Node* shiftObject(UiDocument& doc, Node& object, int delta);
// Whether `node` is `ancestor` or inside it.
bool isWithin(const Node& node, const Node& ancestor);

// The object in a container's property slot, and the objects in its <child type="type"> elements.
Node* propertyObject(const Node& parent, std::string_view property);
std::vector<Node*> childObjectsOfType(const Node& parent, std::string_view type);
// How an object is held by its parent: "child" / "child:titlebar" / "property:start-child" / "".
std::string holderOf(const Node& object);

// A child's <layout> properties (its grid cell).
std::optional<std::string> layoutProperty(const Node& object, std::string_view name);
void setLayoutProperty(UiDocument& doc, Node& object, std::string_view name, std::string_view value);

struct SignalHandler {
    std::string signal, handler;
};
std::vector<SignalHandler> signalHandlers(const Node& object);
// Sets the handler of `signal` ("" removes it).
void setSignalHandler(UiDocument& doc, Node& object, const std::string& signal, const std::string& handler);

// ---- menus: a top-level <menu> and the <submenu>, <section> and <item> elements in it

enum class MenuPart { None, Menu, Submenu, Section, Item };
// <menu>, <submenu>, <section>, <item>; an item's <link name="submenu|section"> counts as one.
MenuPart menuPart(const Node& node);
// The entries (items, sections, submenus) in a menu, submenu, section or link, in order.
std::vector<Node*> menuEntries(const Node& part);
// The top-level <menu> a part is in (itself for a menu).
Node* menuOf(const Node& part);
// A submenu's, section's or item's <attribute name="…">: label, action, target, icon, accel,
// hidden-when.
std::optional<std::string> menuAttribute(const Node& part, std::string_view name);
// Sets it ("" removes it). A new label is marked translatable, as Glade and Cambalache write it.
void setMenuAttribute(UiDocument& doc, Node& part, std::string_view name, std::string_view value);
// Adds an item ("New Item"), a section or a submenu ("New Menu") inside a menu, submenu or section,
// as its `index`th entry (-1: last). Returns it.
Node* addMenuPart(UiDocument& doc, Node& into, MenuPart what, int index = -1);
// A new top-level <menu> with a free id ("menu1") and one item, after the file's other objects.
Node* addMenu(UiDocument& doc);
// Moves an entry before its previous (-1) or after its next (+1) sibling entry; returns it at its new
// place, or nullptr if it can't move that way.
Node* shiftMenuPart(UiDocument& doc, Node& part, int delta);
// A label as the menu shows it: "_Save" → "Save" ("__" is an underscore).
std::string menuLabelText(std::string_view label);
// `xml` (a <menu>, <submenu>, <section> or <item>) with its ids made unique in the document, for
// pasting.
std::string menuPartWithUniqueIds(const UiDocument& doc, std::string_view xml);

} // namespace kevc::ui
