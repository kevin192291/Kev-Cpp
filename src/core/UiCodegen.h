#pragma once

#include "core/UiDocument.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Code from a GtkBuilder design: gtkmm classes, plain C, and Blueprint. Generated files are rewritten
// from the .ui every time it's saved; the user's files are written once and then left alone (only
// "Go to handler" adds a missing handler to them).
namespace kevc::ui {

struct SignalSignature {
    std::string returnType = "void";     // in C: void, gboolean, int…
    std::vector<std::string> parameters; // C types of the arguments after the instance
};

// What the generators need to know about classes. The IDE asks GType; basicGtkTypes() knows enough
// for the project templates and the tests.
struct TypeInfo {
    enum class Value { String, Number, Boolean, Enum, Flags, Object, Other };
    std::function<bool(const std::string& cls)> isWidget;
    std::function<bool(const std::string& cls)> isWindow;
    std::function<bool(const std::string& cls)> inGtk; // declared by <gtk/gtk.h>
    std::function<bool(const std::string& cls)> gtkmmHas;
    std::function<std::optional<SignalSignature>(const std::string& cls, const std::string& signal)> signal;
    std::function<Value(const std::string& cls, const std::string& property)> valueKind;
};
TypeInfo basicGtkTypes();

struct CodeFile {
    std::string name; // next to the .ui
    std::string text;
};

struct Code {
    std::vector<CodeFile> generated; // rewritten on every save
    std::vector<CodeFile> user;      // written only if missing
    std::vector<std::string> warnings;
};

// What code is generated for: top-level <object>s with an id that are windows, or GTK widgets no
// other object names (a panel; not a menu button's popover).
std::vector<Node*> codeRoots(const UiDocument& doc, const TypeInfo& types);

// "main_window" → "MainWindow"; anything that isn't a letter or digit separates words.
std::string pascalCase(const std::string& id);
// An id as a C/C++ identifier.
std::string identifier(const std::string& id);

// For `uiFile` ("main_window.ui"): `stem`.ui.h/.ui.cpp, and for each window `id`.h/`id`.cpp.
Code generateGtkmm(const UiDocument& doc, const std::string& uiFile, const TypeInfo& types);
// For `uiFile`: `stem`-ui.h/-ui.c, and for each window `id`.c.
Code generateC(const UiDocument& doc, const std::string& uiFile, const TypeInfo& types);
// The design in Blueprint syntax (written, never read back).
std::string generateBlueprint(const UiDocument& doc, const TypeInfo& types);

// The .ui text the generated code builds: the design without its <signal>s (the code connects them).
std::string embeddedUi(const UiDocument& doc);

// ---- Go to handler

struct HandlerStub {
    std::string file;           // the user's file the handler is defined in
    std::string declarationFile; // gtkmm: the header that declares it ("" for C)
    std::string definition;     // text to append to `file` if the handler isn't there
    std::string declaration;    // gtkmm: text to add to the class in `declarationFile`
    std::string qualifiedName;  // what to look for: MainWindow::on_save_clicked / on_save_clicked
};
// The stub for the handler of `signal` on `object`, for gtkmm (cpp = true) or C.
std::optional<HandlerStub> handlerStub(const UiDocument& doc, const Node& object, const std::string& signal,
                                       bool cpp, const TypeInfo& types);
// The 1-based line where `qualifiedName` is defined (its name followed by "(" at the start of a
// definition), if it is.
std::optional<int> findDefinition(const std::string& text, const std::string& qualifiedName);
// `header` with `declaration` added to class `className` (after the "Signal handlers" comment, or
// before the class's closing brace).
std::string addDeclaration(const std::string& header, const std::string& className, const std::string& declaration);

} // namespace kevc::ui
