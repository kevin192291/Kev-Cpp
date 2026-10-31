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

} // namespace kevc::ui
