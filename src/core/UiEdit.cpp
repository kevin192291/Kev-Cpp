#include "core/UiEdit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <cctype>
#include <stdexcept>

namespace kevc::ui {

namespace {

using json = nlohmann::ordered_json; // new objects get their defaults in the catalog's order

Slots slotsFrom(const std::string& s) {
    if (s.empty() || s == "none") return Slots::None;
    if (s == "window") return Slots::Window;
    if (s == "single") return Slots::Single;
    if (s == "children") return Slots::Children;
    if (s == "typed") return Slots::Typed;
    if (s == "properties") return Slots::Properties;
    if (s == "grid") return Slots::Grid;
    if (s == "pages") return Slots::Pages;
    if (s == "overlay") return Slots::Overlay;
    throw std::runtime_error("unknown slots \"" + s + "\"");
}

std::string normalized(std::string s) {
    std::replace(s.begin(), s.end(), '_', '-');
    return s;
}

bool isMenuElement(const Node& n) {
    return n.isElement("menu") || n.isElement("submenu") || n.isElement("section") || n.isElement("link");
}

// The ids GtkBuilder knows: objects', and menus' (with their submenus and sections).
std::set<std::string> idsIn(const UiDocument& doc) {
    std::set<std::string> ids;
    for (Node* o : doc.objects())
        if (o->name == "object")
            if (auto id = UiDocument::objectId(*o); !id.empty()) ids.insert(id);
    std::function<void(const Node&)> menus = [&](const Node& n) {
        if (isMenuElement(n))
            if (auto id = n.attribute("id"); !id.empty()) ids.insert(id);
        for (const Node* c : n.childElements()) menus(*c);
    };
    if (const Node* iface = doc.interface())
        for (const Node* m : iface->childElements("menu")) menus(*m);
    return ids;
}

std::string nextFree(const std::string& base, const std::set<std::string>& taken) {
    for (int n = 1;; ++n)
        if (const std::string id = base + std::to_string(n); !taken.count(id)) return id;
}

int indexAmongElements(const Node& parent, const Node& child) {
    const auto elements = parent.childElements();
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i] == &child) return int(i);
    return -1;
}

// Where a new <child> goes: before `before`'s, else after the last <child>, else before the
// elements GtkBuilder files put last (style, layout…).
int childIndex(const Node& parent, const Node* before) {
    if (before && before->parent && before->parent->parent == &parent) return indexAmongElements(parent, *before->parent);
    const auto elements = parent.childElements();
    int last = -1;
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i]->name == "child") last = int(i);
    if (last >= 0) return last + 1;
    for (size_t i = 0; i < elements.size(); ++i) {
        const std::string& n = elements[i]->name;
        if (n == "style" || n == "layout" || n == "accessibility" || n == "action-widgets") return int(i);
    }
    return -1;
}

// After the last property (and signal): where a property or signal goes.
int afterProperties(const Node& object, bool andSignals) {
    int index = 0, i = 0;
    for (Node* e : object.childElements()) {
        ++i;
        if (e->name == "property" || (andSignals && e->name == "signal")) index = i;
    }
    return index;
}

bool isPage(const Node& object) {
    const std::string cls = UiDocument::className(object);
    return cls == "GtkStackPage" || cls == "GtkNotebookPage" || cls == "GtkAssistantPage";
}

} // namespace

// ---- the catalog

Catalog Catalog::parse(std::string_view text) {
    Catalog catalog;
    try {
        const json j = json::parse(text);
        for (const auto& g : j.value("groups", json::array())) catalog.groups_.push_back(g.get<std::string>());
        for (const auto& c : j.at("classes")) {
            ClassInfo info;
            info.cls = c.at("class").get<std::string>();
            info.label = c.value("label", info.cls);
            info.group = c.value("group", "");
            info.slots = slotsFrom(c.value("slots", ""));
            for (const auto& n : c.value("names", json::array())) info.names.push_back(n.get<std::string>());
            info.property = c.value("property", "");
            info.page = c.value("page", "");
            info.xml = c.value("xml", "");
            if (c.contains("defaults"))
                for (const auto& [key, value] : c.at("defaults").items()) info.defaults.emplace_back(key, value.get<std::string>());
            catalog.classes_.push_back(std::move(info));
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("the designer catalog is broken: ") + e.what());
    }
    return catalog;
}

const ClassInfo* Catalog::find(std::string_view cls) const {
    for (const auto& c : classes_)
        if (c.cls == cls) return &c;
    return nullptr;
}

// ---- new objects

std::string freshId(const UiDocument& doc, const std::string& cls, const std::set<std::string>& taken) {
    std::string name = cls;
    for (const char* prefix : {"Gtk", "Adw"})
        if (name.rfind(prefix, 0) == 0 && name.size() > 3 && std::isupper(static_cast<unsigned char>(name[3]))) name.erase(0, 3);
    std::string base;
    for (size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (std::isupper(c) && i > 0) base += '_';
        base += char(std::tolower(c));
    }
    if (base.empty()) base = "object";
    auto ids = idsIn(doc);
    ids.insert(taken.begin(), taken.end());
    return nextFree(base, ids);
}

std::set<std::string> usedIds(const UiDocument& doc) {
    return idsIn(doc);
}

std::string newObjectXml(const UiDocument& doc, const std::string& cls, const ClassInfo* info) {
    std::string xml = "<object class=\"" + escape(cls, true) + "\" id=\"" + escape(freshId(doc, cls), true) + "\">";
    if (info) {
        for (const auto& [name, value] : info->defaults)
            xml += "<property name=\"" + escape(name, true) + "\">" + escape(value) + "</property>";
        xml += info->xml;
    }
    return xml + "</object>";
}

std::string withUniqueIds(const UiDocument& doc, std::string_view objectXml) {
    UiDocument fragment = UiDocument::parse(objectXml);
    std::set<std::string> taken = idsIn(doc);
    std::map<std::string, std::string> renamed;
    for (Node* o : fragment.objects()) {
        if (o->name != "object") continue;
        const std::string id = UiDocument::objectId(*o);
        if (id.empty()) continue;
        if (!taken.count(id)) {
            taken.insert(id);
            continue;
        }
        std::string base = id;
        while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
        if (base.empty()) base = id;
        const std::string fresh = nextFree(base, taken);
        taken.insert(fresh);
        renamed[id] = fresh;
        fragment.setAttribute(*o, "id", fresh);
    }
    // References inside what's pasted (mnemonic-widget, adjustment…) follow their object.
    for (Node* o : fragment.objects())
        for (Node* p : o->childElements("property"))
            if (p->childElements().empty())
                if (auto it = renamed.find(p->innerText()); it != renamed.end())
                    fragment.setProperty(*o, p->attribute("name"), it->second);
    return fragment.text();
}

std::string objectXml(const Node& object) {
    UiDocument fragment = UiDocument::parse(UiDocument::textOf(object));
    for (const auto& c : fragment.root().children)
        if (UiDocument::isObject(*c)) {
            if (Node* layout = c->firstChild("layout")) fragment.removeElement(*layout);
            break;
        }
    return fragment.text();
}

// ---- placing, moving, removing

Node* place(UiDocument& doc, const Placement& where, std::string_view xml, const Catalog& catalog) {
    using Kind = Placement::Kind;
    if (where.kind == Kind::TopLevel) {
        Node* iface = doc.interface();
        if (!iface) throw std::runtime_error("the file has no <interface>");
        return doc.insertElement(*iface, xml, -1);
    }
    if (!where.parent) throw std::runtime_error("no container to place into");
    Node& parent = *where.parent;
    switch (where.kind) {
    case Kind::Child: {
        const std::string type = where.name.empty() ? "" : " type=\"" + escape(where.name, true) + "\"";
        Node* holder = doc.insertElement(parent, "<child" + type + ">" + std::string(xml) + "</child>",
                                         childIndex(parent, where.before));
        return holder->firstChild("object");
    }
    case Kind::Property: {
        if (Node* existing = UiDocument::propertyNode(parent, where.name)) doc.removeElement(*existing);
        Node* property = doc.insertElement(parent, "<property name=\"" + escape(where.name, true) + "\">" +
                                                      std::string(xml) + "</property>",
                                           afterProperties(parent, false));
        return property->firstChild("object");
    }
    case Kind::Grid: {
        Placement child{Kind::Child, &parent, "", nullptr, 0, 0};
        Node* object = place(doc, child, xml, catalog);
        setLayoutProperty(doc, *object, "column", std::to_string(where.column));
        setLayoutProperty(doc, *object, "row", std::to_string(where.row));
        return object;
    }
    case Kind::Page: {
        const ClassInfo* info = catalog.find(UiDocument::className(parent));
        const std::string pageClass = info && !info->page.empty() ? info->page : "GtkStackPage";
        const std::string n = std::to_string(parent.childElements("child").size() + 1);
        std::string page = "<object class=\"" + escape(pageClass, true) + "\">";
        if (pageClass == "GtkNotebookPage")
            page += "<property name=\"child\">" + std::string(xml) +
                    "</property><property name=\"tab\"><object class=\"GtkLabel\"><property name=\"label\">Page " + n +
                    "</property></object></property>";
        else
            page += "<property name=\"name\">page" + n + "</property><property name=\"title\">Page " + n +
                    "</property><property name=\"child\">" + std::string(xml) + "</property>";
        page += "</object>";
        Node* pageObject = place(doc, Placement{Kind::Child, &parent, "", nullptr, 0, 0}, page, catalog);
        return propertyObject(*pageObject, "child");
    }
    case Kind::TopLevel: break;
    }
    return nullptr;
}

void removeObject(UiDocument& doc, Node& object) {
    Node* holder = object.parent;
    if (!holder) return;
    // A page's widget takes its page with it.
    if (holder->isElement("property") && normalized(holder->attribute("name")) == "child")
        if (Node* page = holder->parent; page && UiDocument::isObject(*page) && isPage(*page)) {
            removeObject(doc, *page);
            return;
        }
    if (holder->isElement("child") || holder->isElement("property")) doc.removeElement(*holder);
    else doc.removeElement(object);
}

bool isWithin(const Node& node, const Node& ancestor) {
    for (const Node* n = &node; n; n = n->parent)
        if (n == &ancestor) return true;
    return false;
}

Node* moveObject(UiDocument& doc, Node& object, const Placement& where, const Catalog& catalog) {
    if (where.parent && isWithin(*where.parent, object)) return nullptr; // into itself
    if (where.before == &object) return &object;
    const std::string xml = objectXml(object);
    removeObject(doc, object);
    return place(doc, where, xml, catalog);
}

Node* wrapObject(UiDocument& doc, Node& object, std::string_view containerXml, const Catalog& catalog) {
    Node* holder = object.parent;
    if (!holder || holder->isElement("interface") || !holder->isElement()) return nullptr;
    const int index = indexAmongElements(*holder, object);
    // The grid cell (or other layout) is where the object was: the container takes it.
    std::optional<std::string> layout;
    if (Node* l = object.firstChild("layout")) layout = UiDocument::textOf(*l);
    const std::string inner = objectXml(object);
    doc.removeElement(object);
    Node* container = doc.insertElement(*holder, containerXml, index);
    if (layout) doc.insertElement(*container, *layout);

    const ClassInfo* info = catalog.find(UiDocument::className(*container));
    Placement into{Placement::Kind::Child, container, "", nullptr, 0, 0};
    switch (info ? info->slots : Slots::Single) {
    case Slots::Properties:
        into.kind = Placement::Kind::Property;
        into.name = info->names.empty() ? "child" : info->names.front();
        break;
    case Slots::Grid: into.kind = Placement::Kind::Grid; break;
    case Slots::Pages: into.kind = Placement::Kind::Page; break;
    case Slots::Typed: into.name = info->names.empty() ? "" : info->names.front(); break;
    default: break;
    }
    place(doc, into, inner, catalog);
    return container;
}

Node* shiftObject(UiDocument& doc, Node& object, int delta) {
    Node* holder = object.parent;
    if (!holder || !holder->isElement("child") || !holder->parent) return nullptr;
    Node* parent = holder->parent;
    const std::string type = holder->attribute("type");
    std::vector<Node*> siblings;
    for (Node* c : parent->childElements("child"))
        if (c->attribute("type") == type) siblings.push_back(c);
    const auto it = std::find(siblings.begin(), siblings.end(), holder);
    const long i = it - siblings.begin(), j = i + delta;
    if (it == siblings.end() || j < 0 || j >= long(siblings.size()) || delta == 0) return nullptr;
    Node* other = siblings[size_t(j)];
    const std::string text = UiDocument::textOf(*holder);
    const std::vector<int> objectPath = [&] {
        // Where the object sits inside its <child>, to find it in the re-inserted copy.
        std::vector<int> path;
        for (const Node* n = &object; n != holder; n = n->parent) path.insert(path.begin(), indexAmongElements(*n->parent, *n));
        return path;
    }();
    doc.removeElement(*holder);
    const int at = indexAmongElements(*parent, *other);
    Node* moved = doc.insertElement(*parent, text, delta < 0 ? at : at + 1);
    for (int k : objectPath) {
        const auto elements = moved->childElements();
        if (k < 0 || size_t(k) >= elements.size()) return nullptr;
        moved = elements[size_t(k)];
    }
    return moved;
}

// ---- reading slots

Node* propertyObject(const Node& parent, std::string_view property) {
    Node* p = UiDocument::propertyNode(parent, property);
    return p ? p->firstChild("object") : nullptr;
}

std::vector<Node*> childObjectsOfType(const Node& parent, std::string_view type) {
    std::vector<Node*> out;
    for (Node* c : parent.childElements("child"))
        if (c->attribute("type") == type)
            for (Node* o : c->childElements("object")) out.push_back(o);
    return out;
}

std::string holderOf(const Node& object) {
    const Node* holder = object.parent;
    if (!holder) return {};
    if (holder->isElement("child")) {
        const std::string type = holder->attribute("type");
        return type.empty() ? "child" : "child:" + type;
    }
    if (holder->isElement("property")) return "property:" + normalized(holder->attribute("name"));
    return {};
}

// ---- layout and signals

std::optional<std::string> layoutProperty(const Node& object, std::string_view name) {
    if (Node* layout = object.firstChild("layout")) return UiDocument::property(*layout, name);
    return std::nullopt;
}

void setLayoutProperty(UiDocument& doc, Node& object, std::string_view name, std::string_view value) {
    Node* layout = object.firstChild("layout");
    if (!layout) layout = doc.insertElement(object, "<layout/>", -1);
    doc.setProperty(*layout, name, value);
}

std::vector<SignalHandler> signalHandlers(const Node& object) {
    std::vector<SignalHandler> out;
    for (Node* s : object.childElements("signal")) out.push_back({s->attribute("name"), s->attribute("handler")});
    return out;
}

void setSignalHandler(UiDocument& doc, Node& object, const std::string& signal, const std::string& handler) {
    std::vector<Node*> existing;
    for (Node* s : object.childElements("signal"))
        if (s->attribute("name") == signal) existing.push_back(s);
    if (handler.empty()) {
        for (Node* s : existing) doc.removeElement(*s);
        return;
    }
    if (!existing.empty()) {
        doc.setAttribute(*existing.front(), "handler", handler);
        return;
    }
    doc.insertElement(object, "<signal name=\"" + escape(signal, true) + "\" handler=\"" + escape(handler, true) + "\"/>",
                      afterProperties(object, true));
}

// ---- menus

MenuPart menuPart(const Node& node) {
    if (!node.isElement()) return MenuPart::None;
    if (node.name == "menu") return MenuPart::Menu;
    if (node.name == "submenu") return MenuPart::Submenu;
    if (node.name == "section") return MenuPart::Section;
    if (node.name == "item") return MenuPart::Item;
    if (node.name == "link") {
        const std::string name = node.attribute("name");
        if (name == "submenu") return MenuPart::Submenu;
        if (name == "section") return MenuPart::Section;
    }
    return MenuPart::None;
}

std::vector<Node*> menuEntries(const Node& part) {
    std::vector<Node*> out;
    for (Node* c : part.childElements()) {
        const MenuPart kind = menuPart(*c);
        if (kind == MenuPart::Item || kind == MenuPart::Section || kind == MenuPart::Submenu) out.push_back(c);
    }
    return out;
}

Node* menuOf(const Node& part) {
    for (const Node* n = &part; n; n = n->parent)
        if (n->isElement("menu")) return const_cast<Node*>(n);
    return nullptr;
}

namespace {

Node* attributeNode(const Node& part, std::string_view name) {
    for (Node* a : part.childElements("attribute"))
        if (a->attribute("name") == name) return a;
    return nullptr;
}

// An entry's index among all of `parent`'s elements (insertElement counts those).
int elementIndexOfEntry(const Node& parent, int entryIndex) {
    const auto entries = menuEntries(parent);
    if (entryIndex < 0 || size_t(entryIndex) >= entries.size()) return -1;
    return indexAmongElements(parent, *entries[size_t(entryIndex)]);
}

} // namespace

std::optional<std::string> menuAttribute(const Node& part, std::string_view name) {
    if (const Node* a = attributeNode(part, name)) return a->innerText();
    return std::nullopt;
}

void setMenuAttribute(UiDocument& doc, Node& part, std::string_view name, std::string_view value) {
    Node* a = attributeNode(part, name);
    if (value.empty()) {
        if (a) doc.removeElement(*a);
        return;
    }
    if (a) {
        doc.setText(*a, value);
        return;
    }
    // After the other attributes, before the entries.
    int index = 0, i = 0;
    for (Node* e : part.childElements()) {
        ++i;
        if (e->name == "attribute") index = i;
    }
    const std::string translatable = name == "label" ? " translatable=\"yes\"" : "";
    doc.insertElement(
        part, "<attribute name=\"" + escape(name, true) + "\"" + translatable + ">" + escape(value) + "</attribute>",
        index);
}

Node* addMenuPart(UiDocument& doc, Node& into, MenuPart what, int index) {
    const MenuPart kind = menuPart(into);
    if (kind != MenuPart::Menu && kind != MenuPart::Submenu && kind != MenuPart::Section) return nullptr;
    std::string xml;
    if (what == MenuPart::Item)
        xml = "<item><attribute name=\"label\" translatable=\"yes\">New Item</attribute></item>";
    else if (what == MenuPart::Section) xml = "<section/>";
    else if (what == MenuPart::Submenu)
        xml = "<submenu><attribute name=\"label\" translatable=\"yes\">New Menu</attribute></submenu>";
    else return nullptr;
    return doc.insertElement(into, xml, elementIndexOfEntry(into, index));
}

Node* addMenu(UiDocument& doc) {
    Node* iface = doc.interface();
    if (!iface) return nullptr;
    const std::string id = nextFree("menu", idsIn(doc));
    return doc.insertElement(
        *iface, "<menu id=\"" + id +
                    "\"><item><attribute name=\"label\" translatable=\"yes\">New Item</attribute></item></menu>");
}

Node* shiftMenuPart(UiDocument& doc, Node& part, int delta) {
    Node* parent = part.parent;
    if (!parent || delta == 0) return nullptr;
    const auto siblings = menuEntries(*parent);
    const auto it = std::find(siblings.begin(), siblings.end(), &part);
    const long i = it - siblings.begin(), j = i + delta;
    if (it == siblings.end() || j < 0 || j >= long(siblings.size())) return nullptr;
    Node* other = siblings[size_t(j)];
    const std::string text = UiDocument::textOf(part);
    doc.removeElement(part);
    const int at = indexAmongElements(*parent, *other);
    return doc.insertElement(*parent, text, delta < 0 ? at : at + 1);
}

std::string menuLabelText(std::string_view label) {
    std::string out;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == '_') {
            if (i + 1 < label.size() && label[i + 1] == '_') out += '_', ++i;
            continue;
        }
        out += label[i];
    }
    return out;
}

std::string menuPartWithUniqueIds(const UiDocument& doc, std::string_view xml) {
    UiDocument fragment = UiDocument::parse(xml);
    std::set<std::string> taken = idsIn(doc);
    std::function<void(Node&)> visit = [&](Node& n) {
        if (isMenuElement(n))
            if (const std::string id = n.attribute("id"); !id.empty()) {
                if (taken.count(id)) {
                    std::string base = id;
                    while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
                    const std::string fresh = nextFree(base.empty() ? id : base, taken);
                    fragment.setAttribute(n, "id", fresh);
                    taken.insert(fresh);
                } else {
                    taken.insert(id);
                }
            }
        for (Node* c : n.childElements()) visit(*c);
    };
    visit(fragment.root());
    return fragment.text();
}

} // namespace kevc::ui
