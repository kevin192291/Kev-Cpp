#include "designer/DesignerTypes.h"

#include <giomm.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <map>
#include <sstream>

namespace kevc::designer {

GType typeOf(const std::string& cls) {
    if (cls.empty()) return 0;
    GType type = g_type_from_name(cls.c_str());
    // GTK registers most of its types the first time they're used; GtkBuilder finds them by calling
    // gtk_foo_get_type(). Only for GTK's own names: any other name is the user's class.
    if (!type && cls.rfind("Gtk", 0) == 0) {
        static GtkBuilder* resolver = gtk_builder_new();
        type = gtk_builder_get_type_from_name(resolver, cls.c_str());
    }
    return type;
}

ui::ClassKind classKind(const std::string& cls) {
    const GType type = typeOf(cls);
    if (!type || G_TYPE_IS_ABSTRACT(type) || !g_type_is_a(type, G_TYPE_OBJECT)) return ui::ClassKind::Unknown;
    if (g_type_is_a(type, GTK_TYPE_WINDOW)) return ui::ClassKind::Window;
    if (g_type_is_a(type, GTK_TYPE_EVENT_CONTROLLER)) return ui::ClassKind::Controller;
    if (g_type_is_a(type, GTK_TYPE_NATIVE)) return ui::ClassKind::Other; // popovers: their button shows them
    if (g_type_is_a(type, GTK_TYPE_WIDGET)) return ui::ClassKind::Widget;
    return ui::ClassKind::Other;
}

const ui::Catalog& catalog() {
    static const ui::Catalog loaded = [] {
        try {
            auto bytes = Gio::Resource::lookup_data_global("/org/kevcpp/KevCpp/designer/catalog.json");
            gsize size = 0;
            const auto* data = static_cast<const char*>(bytes->get_data(size));
            return ui::Catalog::parse(std::string_view(data, size));
        } catch (const std::exception& e) { // Glib::Error is one too
            g_warning("GTK designer catalog: %s", e.what());
        }
        return ui::Catalog{};
    }();
    return loaded;
}

const ui::ClassInfo* catalogInfo(const std::string& cls) {
    if (const ui::ClassInfo* info = catalog().find(cls)) return info;
    for (GType t = typeOf(cls); t; t = g_type_parent(t))
        if (const ui::ClassInfo* info = catalog().find(g_type_name(t))) return info;
    return nullptr;
}

bool isWindowClass(const std::string& cls) {
    return classKind(cls) == ui::ClassKind::Window;
}

namespace {

// Types <gtk/gtk.h> declares: GTK's own, GDK, GSK, Pango and GLib/GIO (not GtkSource, VTE or Adw).
bool declaredByGtkHeader(const std::string& name) {
    if (name.rfind("GtkSource", 0) == 0) return false;
    for (const char* prefix : {"Gtk", "Gdk", "Gsk", "Pango"})
        if (name.rfind(prefix, 0) == 0) return true;
    return name.size() > 1 && name[0] == 'G' && std::isupper(static_cast<unsigned char>(name[1]));
}

std::string cTypeName(GType type) {
    type &= ~G_SIGNAL_TYPE_STATIC_SCOPE;
    const std::string name = g_type_name(type) ? g_type_name(type) : "";
    switch (G_TYPE_FUNDAMENTAL(type)) {
    case G_TYPE_NONE: return "void";
    case G_TYPE_BOOLEAN: return "gboolean";
    case G_TYPE_CHAR: return "gchar";
    case G_TYPE_UCHAR: return "guchar";
    case G_TYPE_INT: return "int";
    case G_TYPE_UINT: return "guint";
    case G_TYPE_LONG: return "glong";
    case G_TYPE_ULONG: return "gulong";
    case G_TYPE_INT64: return "gint64";
    case G_TYPE_UINT64: return "guint64";
    case G_TYPE_FLOAT: return "float";
    case G_TYPE_DOUBLE: return "double";
    case G_TYPE_STRING: return "const char *";
    case G_TYPE_VARIANT: return "GVariant *";
    case G_TYPE_PARAM: return "GParamSpec *";
    case G_TYPE_ENUM: return declaredByGtkHeader(name) ? name : "int";
    case G_TYPE_FLAGS: return declaredByGtkHeader(name) ? name : "guint";
    case G_TYPE_OBJECT:
    case G_TYPE_INTERFACE:
    case G_TYPE_BOXED: return declaredByGtkHeader(name) ? name + " *" : "gpointer";
    default: return "gpointer";
    }
}

// Where gtkmm's headers are (gtkmm/button.h…), from pkg-config.
std::vector<std::filesystem::path> gtkmmIncludeDirs() {
    std::vector<std::filesystem::path> dirs;
    if (FILE* pipe = popen("pkg-config --cflags-only-I gtkmm-4.0 2>/dev/null", "r")) {
        std::array<char, 4096> buffer{};
        std::string output;
        while (fgets(buffer.data(), int(buffer.size()), pipe)) output += buffer.data();
        pclose(pipe);
        std::istringstream words(output);
        for (std::string word; words >> word;)
            if (word.rfind("-I", 0) == 0) dirs.emplace_back(word.substr(2));
    }
    if (dirs.empty()) dirs.emplace_back("/usr/include/gtkmm-4.0");
    return dirs;
}

} // namespace

const ui::TypeInfo& gtkTypes() {
    static const ui::TypeInfo types = [] {
        ui::TypeInfo t;
        t.isWidget = [](const std::string& cls) {
            const GType g = typeOf(cls);
            return g && g_type_is_a(g, GTK_TYPE_WIDGET);
        };
        t.isWindow = [](const std::string& cls) {
            const GType g = typeOf(cls);
            return g && g_type_is_a(g, GTK_TYPE_WINDOW);
        };
        t.inGtk = [](const std::string& cls) { return typeOf(cls) && cls.rfind("Gtk", 0) == 0 && declaredByGtkHeader(cls); };
        t.gtkmmHas = [](const std::string& cls) {
            static const auto dirs = gtkmmIncludeDirs();
            static std::map<std::string, bool> known;
            if (cls.rfind("Gtk", 0) != 0 || cls.rfind("GtkSource", 0) == 0) return false;
            auto [it, fresh] = known.try_emplace(cls, false);
            if (fresh) {
                std::string header = cls.substr(3);
                for (char& c : header) c = char(std::tolower(static_cast<unsigned char>(c)));
                std::error_code ec;
                for (const auto& dir : dirs)
                    if (std::filesystem::exists(dir / "gtkmm" / (header + ".h"), ec)) it->second = true;
            }
            return it->second;
        };
        t.signal = [](const std::string& cls, const std::string& name) -> std::optional<ui::SignalSignature> {
            const GType g = typeOf(cls);
            if (!g) return std::nullopt;
            if (G_TYPE_IS_CLASSED(g)) g_type_class_ref(g); // signals are registered when the class is made
            const guint id = g_signal_lookup(name.c_str(), g);
            if (!id) return std::nullopt;
            GSignalQuery query;
            g_signal_query(id, &query);
            ui::SignalSignature signature;
            signature.returnType = cTypeName(query.return_type);
            for (guint i = 0; i < query.n_params; ++i) signature.parameters.push_back(cTypeName(query.param_types[i]));
            return signature;
        };
        t.valueKind = [](const std::string& cls, const std::string& property) {
            using Value = ui::TypeInfo::Value;
            const GType g = typeOf(cls);
            if (!g || !g_type_is_a(g, G_TYPE_OBJECT)) return Value::String;
            auto* klass = G_OBJECT_CLASS(g_type_class_ref(g));
            GParamSpec* spec = g_object_class_find_property(klass, property.c_str());
            if (!spec) return Value::String;
            switch (G_TYPE_FUNDAMENTAL(G_PARAM_SPEC_VALUE_TYPE(spec))) {
            case G_TYPE_BOOLEAN: return Value::Boolean;
            case G_TYPE_INT:
            case G_TYPE_UINT:
            case G_TYPE_LONG:
            case G_TYPE_ULONG:
            case G_TYPE_INT64:
            case G_TYPE_UINT64:
            case G_TYPE_FLOAT:
            case G_TYPE_DOUBLE: return Value::Number;
            case G_TYPE_ENUM: return Value::Enum;
            case G_TYPE_FLAGS: return Value::Flags;
            case G_TYPE_OBJECT:
            case G_TYPE_INTERFACE: return Value::Object;
            case G_TYPE_STRING: return Value::String;
            default: return Value::Other;
            }
        };
        return t;
    }();
    return types;
}

} // namespace kevc::designer
