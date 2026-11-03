#include "core/UiCodegen.h"

#include "core/UiEdit.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace kevc::ui {

namespace {

bool truthy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == "true" || s == "yes" || s == "t" || s == "y" || s == "1";
}

bool isIdentifier(const std::string& s) {
    if (s.empty() || std::isdigit(static_cast<unsigned char>(s[0]))) return false;
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}

std::string stemOf(const std::string& uiFile) {
    std::string name = uiFile.substr(uiFile.find_last_of('/') + 1);
    if (name.size() > 3 && name.ends_with(".ui")) name.resize(name.size() - 3);
    return name;
}

std::string cString(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20) {
                char octal[8];
                std::snprintf(octal, sizeof octal, "\\%03o", c);
                out += octal;
            } else {
                out += char(c);
            }
        }
    }
    return out;
}

// A long text as C string literals, one line each.
std::string cLiteral(const std::string& text) {
    std::string out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        end = end == std::string::npos ? text.size() : end + 1;
        out += "    \"" + cString(text.substr(start, end - start)) + "\"\n";
        start = end;
    }
    if (out.empty()) out = "    \"\"\n";
    out.pop_back();
    return out;
}

// gtkmm's own default handlers: a design's handler with one of these names would override it.
const std::set<std::string>& gtkmmVirtuals() {
    static const std::set<std::string> names = {
        "on_show", "on_hide", "on_map", "on_unmap", "on_realize", "on_unrealize", "on_size_allocate",
        "on_state_flags_changed", "on_direction_changed", "on_mnemonic_activate", "on_query_tooltip",
        "on_close_request", "on_activate_default", "on_activate_focus", "on_keys_changed", "on_clicked",
        "on_activate", "on_toggled", "on_changed", "on_value_changed", "on_insert_text", "on_delete_text",
        "on_set_focus_child", "on_snapshot", "on_measure", "on_root", "on_unroot", "on_focus", "on_grab_focus"};
    return names;
}

struct Member {
    std::string id, name, cls;
    bool widget = false;
};

struct Handler {
    std::string name, signal;
    std::string member; // "" for the window itself
    std::string objectClass;
    SignalSignature signature;
    bool after = false, swapped = false;
};

struct Window {
    const Node* root = nullptr;
    std::string id, name, cls;
    std::vector<Member> members;
    std::vector<Handler> handlers;        // every connection
    std::vector<const Handler*> declared; // one per handler name
};

// The class name members for <menu>, <submenu> and <section> ids get (not a GType name).
const char* const kMenuClass = "GMenu";

// The ids an object and those inside it give in properties: the objects and menus they name.
std::set<std::string> namesIn(const UiDocument& doc, const Node& top) {
    std::set<std::string> out;
    for (Node* o : doc.objects())
        if (isWithin(*o, top))
            for (Node* p : o->childElements("property"))
                if (p->childElements().empty()) out.insert(p->innerText());
    return out;
}

// What else in the file a code root's class holds: the top-level <menu>s and objects beside the
// roots that it names (a menu bar's menu-model, a menu button's popover), directly or through
// something it names, and those nothing names, which go to every root. A top-level widget that
// nothing names isn't one of them: it's a root of its own, or (not GTK's) nothing would keep it alive.
std::vector<const Node*> satellites(const UiDocument& doc, const Node& root, const TypeInfo& types) {
    const Node* iface = doc.interface();
    if (!iface) return {};
    const auto roots = codeRoots(doc, types);
    std::vector<const Node*> candidates;
    for (const Node* e : iface->childElements())
        if (e->name == "menu" || (e->name == "object" && std::find(roots.begin(), roots.end(), e) == roots.end()))
            candidates.push_back(e);
    auto idOf = [](const Node* n) { return n->name == "menu" ? n->attribute("id") : UiDocument::objectId(*n); };
    auto reachedFrom = [&](const Node& from) {
        std::set<const Node*> reached;
        std::set<std::string> names = namesIn(doc, from);
        for (bool more = true; more;) {
            more = false;
            for (const Node* c : candidates) {
                const std::string id = idOf(c);
                if (reached.count(c) || id.empty() || !names.count(id)) continue;
                reached.insert(c);
                const auto further = namesIn(doc, *c);
                names.insert(further.begin(), further.end());
                more = true;
            }
        }
        return reached;
    };
    std::set<const Node*> named, mine;
    for (const Node* r : roots) {
        const auto reached = reachedFrom(*r);
        named.insert(reached.begin(), reached.end());
        if (r == &root) mine = reached;
    }
    std::vector<const Node*> out;
    for (const Node* c : candidates) {
        const bool widget = c->name == "object" && types.isWidget(UiDocument::className(*c));
        if (mine.count(c) || (!named.count(c) && !widget)) out.push_back(c);
    }
    return out;
}

Window describe(const UiDocument& doc, const Node& root, const TypeInfo& types, bool cpp, std::vector<std::string>& warnings) {
    Window w;
    w.root = &root;
    w.id = UiDocument::objectId(root);
    w.name = identifier(w.id);
    w.cls = UiDocument::className(root);
    std::vector<const Node*> tops{&root};
    for (const Node* s : satellites(doc, root, types)) tops.push_back(s);
    auto held = [&tops](const Node& n) {
        return std::any_of(tops.begin(), tops.end(), [&n](const Node* t) { return isWithin(n, *t); });
    };
    for (Node* o : doc.objects()) {
        if (o == &root || !held(*o) || o->name != "object") continue;
        const std::string id = UiDocument::objectId(*o);
        if (id.empty()) continue;
        const std::string cls = UiDocument::className(*o);
        // A class this process doesn't know is a widget if it's some widget's <child>.
        const bool widget = types.isWidget(cls) || (!types.inGtk(cls) && o->parent && o->parent->isElement("child"));
        w.members.push_back({id, identifier(id), cls, widget});
    }
    // Menus, and the submenus and sections in them with an id, which code fills or changes.
    for (const Node* t : tops) {
        if (t->name != "menu") continue;
        std::function<void(const Node&)> add = [&](const Node& n) {
            if (const std::string id = n.attribute("id"); !id.empty())
                w.members.push_back({id, identifier(id), kMenuClass, false});
            for (const Node* c : n.childElements()) add(*c);
        };
        add(*t);
    }
    std::map<std::string, std::string> returnTypes;
    for (Node* o : doc.objects()) {
        if (!held(*o) || o->name != "object") continue;
        const std::string id = UiDocument::objectId(*o), cls = UiDocument::className(*o);
        for (Node* s : o->childElements("signal")) {
            Handler h;
            h.name = s->attribute("handler");
            h.signal = s->attribute("name");
            if (h.name.empty() || h.signal.empty()) continue;
            if (!isIdentifier(h.name)) {
                warnings.push_back("\"" + h.name + "\" isn't a valid function name, so " + h.signal + " isn't connected.");
                continue;
            }
            if (id.empty()) {
                warnings.push_back(h.name + " (" + h.signal + " of a " + cls + " without an id) isn't connected: give the " + cls + " an id.");
                continue;
            }
            if (cpp && gtkmmVirtuals().count(h.name)) {
                warnings.push_back(h.name + " is the name of one of gtkmm's own handlers; rename it so it's connected.");
                continue;
            }
            h.member = o == &root ? "" : identifier(id);
            h.objectClass = cls;
            h.after = truthy(s->attribute("after"));
            h.swapped = truthy(s->attribute("swapped"));
            h.signature = types.signal(cls, h.signal.substr(0, h.signal.find("::"))).value_or(SignalSignature{});
            if (cpp && h.signature.returnType != "void" && h.signature.returnType != "gboolean") {
                warnings.push_back(h.name + ": " + h.signal + " returns " + h.signature.returnType + ", which generated gtkmm code doesn't handle.");
                continue;
            }
            const std::string key = cpp ? h.signature.returnType : h.signature.returnType + "|" + std::to_string(h.signature.parameters.size());
            if (auto [it, fresh] = returnTypes.try_emplace(h.name, key); !fresh && it->second != key) {
                warnings.push_back(h.name + " handles signals with different arguments; only the first is connected.");
                continue;
            }
            w.handlers.push_back(h);
        }
    }
    std::set<std::string> seen;
    for (const Handler& h : w.handlers)
        if (seen.insert(h.name).second) w.declared.push_back(&h);
    return w;
}

std::string gtkmmClass(const std::string& cls) { return "Gtk::" + cls.substr(3); }

std::string memberType(const Member& m, const TypeInfo& types) {
    if (m.cls == kMenuClass) return "Glib::RefPtr<Gio::Menu>";
    const bool known = types.gtkmmHas(m.cls);
    if (m.widget) return (known ? gtkmmClass(m.cls) : std::string("Gtk::Widget")) + "*";
    return "Glib::RefPtr<" + (known ? gtkmmClass(m.cls) : std::string("Glib::Object")) + ">";
}

std::string cppReturn(const SignalSignature& s) { return s.returnType == "gboolean" ? "bool" : "void"; }

// C
std::string cType(const std::string& cls, const TypeInfo& types, bool widget) {
    if (types.inGtk(cls) || cls == kMenuClass) return cls;
    return widget || types.isWidget(cls) ? "GtkWidget" : "GObject";
}

std::string cParameters(const Handler& h, const std::string& uiType, const TypeInfo& types, std::vector<std::string>* names) {
    std::vector<std::pair<std::string, std::string>> params;
    const std::pair<std::string, std::string> self{cType(h.objectClass, types, true) + " *", "self"};
    const std::pair<std::string, std::string> ui{uiType + " *", "ui"};
    if (h.swapped) params.push_back(ui);
    else params.push_back(self);
    for (size_t i = 0; i < h.signature.parameters.size(); ++i) {
        std::string type = h.signature.parameters[i];
        if (!type.ends_with("*")) type += " ";
        params.emplace_back(type, "arg" + std::to_string(i + 1));
    }
    params.push_back(h.swapped ? self : ui);
    std::string out;
    for (const auto& [type, name] : params) {
        if (!out.empty()) out += ", ";
        out += type + name;
        if (names) names->push_back(name);
    }
    return out;
}

std::string cPrototype(const Handler& h, const std::string& uiType, const TypeInfo& types, std::vector<std::string>* names = nullptr) {
    return h.signature.returnType + " " + h.name + "(" + cParameters(h, uiType, types, names) + ")";
}

std::string cReturn(const SignalSignature& s) {
    if (s.returnType == "void") return "";
    return s.returnType == "gboolean" ? "    return FALSE;\n" : "    return 0;\n";
}

} // namespace

// ---- basics

TypeInfo basicGtkTypes() {
    static const std::set<std::string> objects = {"GtkAdjustment", "GtkStringList", "GtkStackPage", "GtkNotebookPage",
                                                  "GtkSizeGroup", "GtkTextBuffer", "GtkEntryBuffer", "GtkListStore",
                                                  "GtkTreeStore", "GtkFileFilter", "GtkAssistantPage", "GtkEventControllerKey",
                                                  "GtkGestureClick", "GtkShortcutController"};
    static const std::set<std::string> windows = {"GtkWindow", "GtkApplicationWindow", "GtkDialog", "GtkAboutDialog",
                                                  "GtkMessageDialog", "GtkAssistant", "GtkShortcutsWindow"};
    static const std::set<std::string> numbers = {"spacing", "row-spacing", "column-spacing", "margin-top", "margin-bottom",
                                                  "margin-start", "margin-end", "default-width", "default-height",
                                                  "width-request", "height-request", "xalign", "yalign", "column", "row",
                                                  "column-span", "row-span", "max-width-chars", "width-chars", "lines",
                                                  "pixel-size", "value", "upper", "lower", "step-increment",
                                                  "page-increment", "fraction", "digits", "position"};
    static const std::set<std::string> booleans = {"visible", "sensitive", "hexpand", "vexpand", "use-underline",
                                                   "wrap", "active", "homogeneous", "resizable", "modal", "selectable",
                                                   "use-markup", "show-title-buttons", "expanded", "reveal-child",
                                                   "spinning", "draw-value", "can-focus", "focusable", "has-frame",
                                                   "editable", "show-peek-icon", "decorated"};
    static const std::set<std::string> enums = {"orientation", "halign", "valign", "justify", "wrap-mode", "ellipsize",
                                                "selection-mode", "transition-type", "position", "policy",
                                                "hscrollbar-policy", "vscrollbar-policy", "input-purpose",
                                                "accessible-role", "overflow"};
    TypeInfo t;
    t.isWidget = [](const std::string& cls) { return cls.rfind("Gtk", 0) == 0 && !objects.count(cls); };
    t.isWindow = [](const std::string& cls) { return windows.count(cls) > 0; };
    t.inGtk = [](const std::string& cls) { return cls.rfind("Gtk", 0) == 0; };
    t.gtkmmHas = [](const std::string& cls) { return cls.rfind("Gtk", 0) == 0; };
    t.signal = [](const std::string&, const std::string& signal) -> std::optional<SignalSignature> {
        if (signal == "close-request" || signal == "query-tooltip" || signal == "mnemonic-activate") return SignalSignature{"gboolean", {}};
        if (signal == "state-set") return SignalSignature{"gboolean", {"gboolean"}};
        if (signal == "notify") return SignalSignature{"void", {"GParamSpec *"}};
        return SignalSignature{};
    };
    t.valueKind = [](const std::string&, const std::string& property) {
        if (numbers.count(property)) return TypeInfo::Value::Number;
        if (booleans.count(property)) return TypeInfo::Value::Boolean;
        if (enums.count(property)) return TypeInfo::Value::Enum;
        if (property == "adjustment" || property == "mnemonic-widget" || property == "model" ||
            property == "default-widget" || property == "transient-for")
            return TypeInfo::Value::Object;
        return TypeInfo::Value::String;
    };
    return t;
}

std::vector<Node*> codeRoots(const UiDocument& doc, const TypeInfo& types) {
    // A top-level widget another object names (a menu button's popover, a label's mnemonic-widget)
    // belongs to that object; one nothing names is a widget of its own, like a panel.
    std::set<std::string> named;
    for (Node* o : doc.objects())
        for (Node* p : o->childElements("property"))
            if (p->childElements().empty()) named.insert(p->innerText());
    std::vector<Node*> out;
    for (Node* o : doc.topLevelObjects()) {
        const std::string id = UiDocument::objectId(*o), cls = UiDocument::className(*o);
        if (o->name != "object" || id.empty()) continue;
        if (types.isWindow(cls) || (types.isWidget(cls) && types.inGtk(cls) && !named.count(id))) out.push_back(o);
    }
    return out;
}

std::string pascalCase(const std::string& id) {
    std::string out;
    bool upper = true;
    for (unsigned char c : id) {
        if (!std::isalnum(c)) {
            upper = true;
            continue;
        }
        out += upper ? char(std::toupper(c)) : char(c);
        upper = false;
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "Window" + out;
    return out;
}

std::string identifier(const std::string& id) {
    static const std::set<std::string> keywords = {
        "auto", "bool", "break", "case", "catch", "char", "class", "const", "continue", "default", "delete", "do",
        "double", "else", "enum", "explicit", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
        "int", "long", "namespace", "new", "operator", "private", "protected", "public", "register", "return",
        "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw", "true", "try",
        "typedef", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "while"};
    std::string out;
    for (unsigned char c : id) out += std::isalnum(c) || c == '_' ? char(c) : '_';
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "_" + out;
    if (keywords.count(out)) out += "_";
    return out;
}

std::string embeddedUi(const UiDocument& doc) {
    UiDocument copy = UiDocument::parse(doc.text());
    std::vector<Node*> signals;
    for (Node* o : copy.objects())
        for (Node* s : o->childElements("signal")) signals.push_back(s);
    for (Node* s : signals) copy.removeElement(*s);
    return copy.text();
}

// ---- gtkmm

Code generateGtkmm(const UiDocument& doc, const std::string& uiFile, const TypeInfo& types) {
    Code code;
    const std::string stem = stemOf(uiFile);
    const std::string banner = "// Generated by Kev-C++ from " + uiFile + " each time it's saved: don't edit this file.\n";
    std::ostringstream h, cpp;
    const auto roots = codeRoots(doc, types);
    const bool widgets = std::any_of(roots.begin(), roots.end(), [&](const Node* r) { return !types.isWindow(UiDocument::className(*r)); });
    h << banner << "// Each class below is the base of your own " << (widgets ? "window or widget" : "window")
      << " class, which Kev-C++ wrote once for you.\n"
      << "#pragma once\n\n#include <gtkmm.h>\n";
    cpp << banner << "#include \"" << stem << ".ui.h\"\n\n"
        << "namespace {\n\n"
        << "// " << uiFile << " without its <signal>s: the constructors below connect them.\n"
        << "const char* const kUi = R\"__kevc_ui__(" << embeddedUi(doc) << ")__kevc_ui__\";\n\n"
        << "} // namespace\n";

    for (Node* top : doc.topLevelObjects())
        if (top->name == "template")
            code.warnings.push_back("The template " + UiDocument::objectId(*top) + " gets no code: use an <object> with an id.");
    if (roots.empty())
        code.warnings.push_back("There's no window or widget with an id in " + uiFile + " to write a class for.");

    for (const Node* root : roots) {
        const Window w = describe(doc, *root, types, true, code.warnings);
        const std::string cls = pascalCase(w.id), base = cls + "Ui";
        const bool window = types.isWindow(w.cls);
        const std::string gtkBase = types.gtkmmHas(w.cls) ? gtkmmClass(w.cls) : window ? "Gtk::Window" : "Gtk::Widget";

        h << "\n// " << w.id << " (" << w.cls << "). Your class " << cls << " (" << w.name << ".h, " << w.name
          << ".cpp) derives from\n// this one: the objects with an id are its members, and it overrides the handlers.\n"
          << "class " << base << " : public " << gtkBase << " {\n"
          << "public:\n";
        if (window)
            h << "    // Builds the window as a T, a class derived from this one (" << cls << "::create() calls it).\n"
              << "    // Any arguments go on to T's constructor, after the object and the builder.\n"
              << "    template <class T, class... Args>\n"
              << "    static T* create(Args&&... args) {\n"
              << "        return Gtk::Builder::get_widget_derived<T>(Gtk::Builder::create_from_string(ui()), \"" << cString(w.id)
              << "\",\n                                                   std::forward<Args>(args)...);\n"
              << "    }\n";
        else
            h << "    // Builds the widget as a T, a class derived from this one (" << cls << "::create() calls it).\n"
              << "    // Any arguments go on to T's constructor, after the object and the builder. The widget lives\n"
              << "    // while the RefPtr, or a container it's added to, holds it.\n"
              << "    template <class T, class... Args>\n"
              << "    static Glib::RefPtr<T> create(Args&&... args) {\n"
              << "        const auto builder = Gtk::Builder::create_from_string(ui());\n"
              << "        T* widget = Gtk::Builder::get_widget_derived<T>(builder, \"" << cString(w.id)
              << "\", std::forward<Args>(args)...);\n"
              << "        widget->reference(); // the builder's own reference goes with the builder\n"
              << "        return Glib::make_refptr_for_instance(widget);\n"
              << "    }\n";
        h << "    static const char* ui();\n\n"
          << "protected:\n"
          << "    " << base << "(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);\n";
        if (!w.members.empty()) {
            h << "\n    // The objects with an id.\n";
            for (const Member& m : w.members)
                h << "    " << memberType(m, types) << " " << m.name << (m.widget ? " = nullptr" : "") << ";\n";
        }
        if (!w.declared.empty()) {
            h << "\n    // The signal handlers named in the design: override them in " << cls << ".\n";
            for (const Handler* d : w.declared)
                h << "    virtual " << cppReturn(d->signature) << " " << d->name << "() {"
                  << (d->signature.returnType == "gboolean" ? " return false; " : "") << "}\n";
        }
        h << "};\n";

        cpp << "\nconst char* " << base << "::ui() {\n    return kUi;\n}\n\n"
            << base << "::" << base << "(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)\n"
            << "    : " << gtkBase << "(object) {\n";
        if (w.members.empty()) cpp << "    (void)builder;\n";
        for (const Member& m : w.members) {
            const bool known = types.gtkmmHas(m.cls);
            if (m.widget)
                cpp << "    " << m.name << " = builder->get_widget<" << (known ? gtkmmClass(m.cls) : "Gtk::Widget") << ">(\""
                    << cString(m.id) << "\");\n";
            else if (m.cls == kMenuClass)
                cpp << "    " << m.name << " = builder->get_object<Gio::Menu>(\"" << cString(m.id) << "\");\n";
            else if (known)
                cpp << "    " << m.name << " = builder->get_object<" << gtkmmClass(m.cls) << ">(\"" << cString(m.id) << "\");\n";
            else
                cpp << "    " << m.name << " = builder->get_object(\"" << cString(m.id) << "\");\n";
        }
        for (const Handler& hd : w.handlers) {
            const std::string instance = hd.member.empty() ? "gobj()" : hd.member + "->gobj()";
            const bool boolean = hd.signature.returnType == "gboolean";
            cpp << "    g_signal_connect_data(" << instance << ", \"" << cString(hd.signal) << "\",\n"
                << "                          G_CALLBACK(+[](" << base << "* self)"
                << (boolean ? " -> gboolean { return self->" + hd.name + "(); }" : " { self->" + hd.name + "(); }")
                << "), this, nullptr,\n"
                << "                          GConnectFlags(G_CONNECT_SWAPPED" << (hd.after ? " | G_CONNECT_AFTER" : "") << "));\n";
        }
        cpp << "}\n";

        std::ostringstream uh, ucpp;
        uh << "#pragma once\n\n#include \"" << stem << ".ui.h\"\n\n"
           << "// The " << (window ? "window" : "widget") << " designed in " << uiFile
           << ". Kev-C++ wrote this file once; it's yours to change.\n"
           << "class " << cls << " : public " << base << " {\n"
           << "public:\n"
           << "    " << cls << "(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);\n"
           << "    static " << (window ? cls + "*" : "Glib::RefPtr<" + cls + ">") << " create() { return " << base
           << "::create<" << cls << ">(); }\n\n"
           << "protected:\n"
           << "    // Signal handlers (Kev-C++ adds new ones here)\n";
        for (const Handler* d : w.declared) uh << "    " << cppReturn(d->signature) << " " << d->name << "() override;\n";
        uh << "};\n";
        ucpp << "#include \"" << w.name << ".h\"\n\n"
             << cls << "::" << cls << "(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)\n"
             << "    : " << base << "(object, builder) {\n}\n";
        for (const Handler* d : w.declared)
            ucpp << "\n" << cppReturn(d->signature) << " " << cls << "::" << d->name << "() {\n"
                 << (d->signature.returnType == "gboolean" ? "    return false;\n" : "") << "}\n";
        code.user.push_back({w.name + ".h", uh.str()});
        code.user.push_back({w.name + ".cpp", ucpp.str()});
    }
    code.generated.push_back({stem + ".ui.h", h.str()});
    code.generated.push_back({stem + ".ui.cpp", cpp.str()});
    return code;
}

// ---- C

Code generateC(const UiDocument& doc, const std::string& uiFile, const TypeInfo& types) {
    Code code;
    const std::string stem = stemOf(uiFile);
    const std::string banner = "/* Generated by Kev-C++ from " + uiFile + " each time it's saved: don't edit this file. */\n";
    std::ostringstream h, c;
    h << banner << "#pragma once\n\n#include <gtk/gtk.h>\n";
    c << banner << "#include \"" << stem << "-ui.h\"\n\n"
      << "/* " << uiFile << " without its <signal>s: the functions below connect them. */\n"
      << "static const char " << identifier(stem) << "_ui[] =\n" << cLiteral(embeddedUi(doc)) << ";\n";

    for (Node* top : doc.topLevelObjects())
        if (top->name == "template")
            code.warnings.push_back("The template " + UiDocument::objectId(*top) + " gets no code: use an <object> with an id.");
    const auto roots = codeRoots(doc, types);
    if (roots.empty()) code.warnings.push_back("There's no window or widget with an id in " + uiFile + " to write code for.");

    for (const Node* root : roots) {
        const Window w = describe(doc, *root, types, false, code.warnings);
        const std::string type = pascalCase(w.id) + "Ui", make = w.name + "_ui_new";
        const bool window = types.isWindow(w.cls);
        h << "\n/* " << w.id << " (" << w.cls << ") and its objects with an id. */\n"
          << "typedef struct " << type << " " << type << ";\n"
          << "struct " << type << " {\n"
          << "    " << cType(w.cls, types, true) << " *" << w.name << ";\n";
        for (const Member& m : w.members) h << "    " << cType(m.cls, types, m.widget) << " *" << m.name << ";\n";
        h << "};\n\n"
          << "/* Builds " << w.id << " and connects its signals. The " << (window ? "window" : "widget") << " frees the "
          << type << " when it's\n   destroyed."
          << (window ? "" : " The widget is new, as from gtk_box_new(): the container you add it to\n   owns it.")
          << " */\n"
          << type << " *" << make << "(void);\n";
        if (!w.declared.empty()) {
            h << "\n/* The handlers named in the design. Write them in " << w.name << ".c; until you do, stand-ins\n"
              << "   that do nothing are used. */\n";
            for (const Handler* d : w.declared) h << cPrototype(*d, type, types) << ";\n";
        }

        c << "\n" << type << " *" << make << "(void) {\n"
          << "    GtkBuilder *builder = gtk_builder_new_from_string(" << identifier(stem) << "_ui, -1);\n"
          << "    " << type << " *ui = g_new0(" << type << ", 1);\n"
          << "    ui->" << w.name << " = (" << cType(w.cls, types, true) << " *) gtk_builder_get_object(builder, \"" << cString(w.id) << "\");\n";
        for (const Member& m : w.members)
            c << "    ui->" << m.name << " = (" << cType(m.cls, types, m.widget) << " *) gtk_builder_get_object(builder, \"" << cString(m.id) << "\");\n";
        for (const Handler& hd : w.handlers) {
            const std::string instance = "ui->" + (hd.member.empty() ? w.name : hd.member);
            if (hd.swapped && hd.after)
                c << "    g_signal_connect_data(" << instance << ", \"" << cString(hd.signal) << "\", G_CALLBACK(" << hd.name
                  << "), ui, NULL, G_CONNECT_SWAPPED | G_CONNECT_AFTER);\n";
            else
                c << "    " << (hd.swapped ? "g_signal_connect_swapped" : hd.after ? "g_signal_connect_after" : "g_signal_connect")
                  << "(" << instance << ", \"" << cString(hd.signal) << "\", G_CALLBACK(" << hd.name << "), ui);\n";
        }
        c << "    g_object_set_data_full(G_OBJECT(ui->" << w.name << "), \"kevc-ui\", ui, g_free);\n";
        if (window)
            c << "    g_object_unref(builder); /* GTK keeps its windows */\n";
        else
            c << "    g_object_ref(ui->" << w.name << "); /* past the builder, which holds the only reference */\n"
              << "    g_object_unref(builder);\n"
              << "    g_object_force_floating(G_OBJECT(ui->" << w.name << ")); /* a new widget's reference */\n";
        c << "    return ui;\n}\n";
        if (!w.declared.empty()) {
            c << "\n/* Stand-ins for the handlers not written yet: yours replace them. */\n";
            for (const Handler* d : w.declared) {
                std::vector<std::string> names;
                const std::string prototype = cPrototype(*d, type, types, &names);
                c << "__attribute__((weak)) " << prototype << " {\n";
                for (const auto& n : names) c << "    (void) " << n << ";\n";
                c << cReturn(d->signature) << "}\n";
            }
        }

        std::ostringstream user;
        user << "#include \"" << stem << "-ui.h\"\n\n"
             << "/* The signal handlers of " << w.id << " (" << uiFile << "). Kev-C++ wrote this file once; it's\n"
             << "   yours to change. */\n";
        for (const Handler* d : w.declared) {
            std::vector<std::string> names;
            const std::string prototype = cPrototype(*d, type, types, &names);
            user << "\n" << prototype << " {\n";
            for (const auto& n : names) user << "    (void) " << n << ";\n";
            user << cReturn(d->signature) << "}\n";
        }
        code.user.push_back({w.name + ".c", user.str()});
    }
    code.generated.push_back({stem + "-ui.h", h.str()});
    code.generated.push_back({stem + "-ui.c", c.str()});
    return code;
}

// ---- Go to handler

std::optional<HandlerStub> handlerStub(const UiDocument& doc, const Node& object, const std::string& signal, bool cpp,
                                       const TypeInfo& types) {
    const Node* top = &object;
    while (const Node* up = UiDocument::parentObject(*top)) top = up;
    // The class it's in: its own top-level object's, or that of the root that holds that one.
    const Node* root = nullptr;
    for (const Node* r : codeRoots(doc, types)) {
        const auto held = satellites(doc, *r, types);
        if (r == top || std::find(held.begin(), held.end(), top) != held.end()) {
            root = r;
            break;
        }
    }
    if (!root) return std::nullopt;
    std::vector<std::string> warnings;
    const Window w = describe(doc, *root, types, cpp, warnings);
    const std::string handlerName = [&] {
        for (Node* s : object.childElements("signal"))
            if (s->attribute("name") == signal) return s->attribute("handler");
        return std::string();
    }();
    const Handler* found = nullptr;
    for (const Handler* d : w.declared)
        if (d->name == handlerName) found = d;
    if (!found) return std::nullopt;

    HandlerStub stub;
    if (cpp) {
        const std::string cls = pascalCase(w.id);
        stub.file = w.name + ".cpp";
        stub.declarationFile = w.name + ".h";
        stub.qualifiedName = cls + "::" + found->name;
        stub.declaration = "    " + cppReturn(found->signature) + " " + found->name + "() override;\n";
        stub.definition = "\n" + cppReturn(found->signature) + " " + cls + "::" + found->name + "() {\n" +
                          (found->signature.returnType == "gboolean" ? "    return false;\n" : "") + "}\n";
    } else {
        std::vector<std::string> names;
        const std::string prototype = cPrototype(*found, pascalCase(w.id) + "Ui", types, &names);
        stub.file = w.name + ".c";
        stub.qualifiedName = found->name;
        stub.definition = "\n" + prototype + " {\n";
        for (const auto& n : names) stub.definition += "    (void) " + n + ";\n";
        stub.definition += cReturn(found->signature) + "}\n";
    }
    return stub;
}

std::optional<int> findDefinition(const std::string& text, const std::string& qualifiedName) {
    // The name, then "(", on a line that isn't a declaration ending in ";".
    std::string pattern;
    for (char c : qualifiedName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') pattern += c;
        else pattern += std::string("\\") + c;
    }
    const std::regex re("(^|[^A-Za-z0-9_:])" + pattern + "\\s*\\(");
    std::istringstream in(text);
    int number = 0;
    for (std::string line; std::getline(in, line);) {
        ++number;
        if (!std::regex_search(line, re)) continue;
        const auto end = line.find_last_not_of(" \t\r");
        if (end != std::string::npos && line[end] == ';') continue; // a declaration or a call
        return number;
    }
    return std::nullopt;
}

std::string addDeclaration(const std::string& header, const std::string& className, const std::string& declaration) {
    const std::string marker = "// Signal handlers";
    if (const size_t at = header.find(marker); at != std::string::npos) {
        const size_t lineEnd = header.find('\n', at);
        if (lineEnd != std::string::npos) return header.substr(0, lineEnd + 1) + declaration + header.substr(lineEnd + 1);
    }
    // Before the closing brace of the class.
    const std::regex classRe("class\\s+" + className + "\\b[^;{]*\\{");
    std::smatch m;
    if (!std::regex_search(header, m, classRe)) return header;
    int depth = 0;
    for (size_t i = size_t(m.position(0) + m.length(0)) - 1; i < header.size(); ++i) {
        if (header[i] == '{') ++depth;
        else if (header[i] == '}' && --depth == 0) {
            const size_t lineStart = header.rfind('\n', i) + 1;
            return header.substr(0, lineStart) + "protected:\n" + declaration + header.substr(lineStart);
        }
    }
    return header;
}

// ---- Blueprint

namespace {

class BlueprintWriter {
public:
    explicit BlueprintWriter(const TypeInfo& types) : types_(types) {}

    std::string write(const UiDocument& doc) {
        Node* iface = doc.interface();
        bool adw = false;
        for (Node* o : doc.objects()) adw = adw || UiDocument::className(*o).rfind("Adw", 0) == 0;
        out_ << "// Written by Kev-C++ from the .ui file, which is what the designer edits: changes made\n"
             << "// here aren't read back.\n"
             << "using Gtk 4.0;\n";
        if (adw) out_ << "using Adw 1;\n";
        if (!iface) return out_.str();
        for (Node* e : iface->childElements()) {
            if (e->name == "requires") continue;
            out_ << "\n";
            if (e->name == "object") object(*e, 0, "");
            else if (e->name == "template") templ(*e);
            else if (e->name == "menu") menu(*e, 0);
            else out_ << "// <" << e->name << "> isn't converted to Blueprint\n";
        }
        return out_.str();
    }

private:
    const TypeInfo& types_;
    std::ostringstream out_;

    static std::string pad(int depth) { return std::string(size_t(depth) * 2, ' '); }

    static std::string typeName(const std::string& cls) {
        auto prefixed = [&](const char* p) {
            return cls.rfind(p, 0) == 0 && cls.size() > 3 && std::isupper(static_cast<unsigned char>(cls[3]));
        };
        if (prefixed("Gtk")) return cls.substr(3);
        if (prefixed("Adw")) return "Adw." + cls.substr(3);
        return "$" + cls;
    }

    static std::string quoted(const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out += c;
        }
        return out + "\"";
    }

    static std::string enumValue(std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        std::replace(v.begin(), v.end(), '-', '_');
        return v;
    }

    std::string value(const std::string& cls, const Node& property) {
        const std::string name = property.attribute("name"), text = property.innerText();
        const bool translatable = truthy(property.attribute("translatable"));
        switch (types_.valueKind(cls, name)) {
        case TypeInfo::Value::Number: return text;
        case TypeInfo::Value::Boolean: return truthy(text) ? "true" : "false";
        case TypeInfo::Value::Enum: return enumValue(text);
        case TypeInfo::Value::Flags: {
            std::string out;
            std::istringstream parts(text);
            for (std::string part; std::getline(parts, part, '|');) {
                part.erase(0, part.find_first_not_of(' '));
                part.erase(part.find_last_not_of(' ') + 1);
                out += (out.empty() ? "" : " | ") + enumValue(part);
            }
            return out;
        }
        case TypeInfo::Value::Object: return text;
        case TypeInfo::Value::String:
        case TypeInfo::Value::Other: break;
        }
        return translatable ? "_(" + quoted(text) + ")" : quoted(text);
    }

    void body(const Node& o, int depth) {
        const std::string cls = UiDocument::className(o);
        for (Node* e : o.childElements()) {
            if (e->name == "property") {
                const std::string name = e->attribute("name");
                if (Node* inner = e->firstChild("object")) {
                    object(*inner, depth, name + ": ");
                    continue;
                }
                if (const std::string source = e->attribute("bind-source"); !source.empty()) {
                    const std::string flags = e->attribute("bind-flags");
                    out_ << pad(depth) << name << ": bind " << source << "." << e->attribute("bind-property")
                         << (flags.find("bidirectional") != std::string::npos ? " bidirectional" : "")
                         << (flags.find("invert-boolean") != std::string::npos ? " inverted" : "") << ";\n";
                    continue;
                }
                out_ << pad(depth) << name << ": " << value(cls, *e) << ";\n";
            } else if (e->name == "signal") {
                out_ << pad(depth) << e->attribute("name") << " => $" << e->attribute("handler") << "()"
                     << (truthy(e->attribute("swapped")) ? " swapped" : "") << (truthy(e->attribute("after")) ? " after" : "") << ";\n";
            } else if (e->name == "child") {
                std::string slot;
                if (const std::string type = e->attribute("type"); !type.empty()) slot = "[" + type + "] ";
                if (const std::string internal = e->attribute("internal-child"); !internal.empty()) slot = "[internal-child " + internal + "] ";
                for (Node* inner : e->childElements("object")) object(*inner, depth, slot);
            } else if (e->name == "style") {
                std::string classes;
                for (Node* c : e->childElements("class")) classes += (classes.empty() ? "" : ", ") + quoted(c->attribute("name"));
                out_ << pad(depth) << "styles [" << classes << "]\n";
            } else if (e->name == "layout") {
                out_ << pad(depth) << "layout {\n";
                for (Node* p : e->childElements("property"))
                    out_ << pad(depth + 1) << p->attribute("name") << ": " << p->innerText() << ";\n";
                out_ << pad(depth) << "}\n";
            } else if (e->name == "items") {
                std::string items;
                for (Node* i : e->childElements("item")) {
                    const std::string text = quoted(i->innerText());
                    items += (items.empty() ? "" : ", ") + (truthy(i->attribute("translatable")) ? "_(" + text + ")" : text);
                }
                out_ << pad(depth) << "strings [" << items << "]\n";
            } else {
                out_ << pad(depth) << "// <" << e->name << "> isn't converted to Blueprint\n";
            }
        }
    }

    void object(const Node& o, int depth, const std::string& prefix) {
        const std::string id = UiDocument::objectId(o);
        out_ << pad(depth) << prefix << typeName(UiDocument::className(o)) << (id.empty() ? "" : " " + id) << " {\n";
        body(o, depth + 1);
        out_ << pad(depth) << "}" << (prefix.ends_with(": ") ? ";" : "") << "\n";
    }

    void templ(const Node& t) {
        out_ << "template $" << t.attribute("class") << " : " << typeName(t.attribute("parent")) << " {\n";
        body(t, 1);
        out_ << "}\n";
    }

    void menu(const Node& m, int depth) {
        const std::string id = m.attribute("id");
        out_ << pad(depth) << m.name << (id.empty() ? "" : " " + id) << " {\n";
        for (Node* e : m.childElements()) {
            if (e->name == "attribute") {
                const std::string text = quoted(e->innerText());
                out_ << pad(depth + 1) << e->attribute("name") << ": "
                     << (truthy(e->attribute("translatable")) ? "_(" + text + ")" : text) << ";\n";
            } else {
                menu(*e, depth + 1);
            }
        }
        out_ << pad(depth) << "}\n";
    }
};

} // namespace

std::string generateBlueprint(const UiDocument& doc, const TypeInfo& types) {
    return BlueprintWriter(types).write(doc);
}

} // namespace kevc::ui
