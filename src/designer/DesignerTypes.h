#pragma once

#include "core/UiCodegen.h"
#include "core/UiDocument.h"
#include "core/UiEdit.h"

#include <gtk/gtk.h>

#include <string>

// What the GTK designer asks of this process's GType system, and its widget catalog.
namespace kevc::designer {

// The GType of a class named in a .ui file, or 0 if this process doesn't have it.
GType typeOf(const std::string& cls);
ui::ClassKind classKind(const std::string& cls);
// data/designer/catalog.json, from the app's resources.
const ui::Catalog& catalog();
// The catalog's entry for a class or its nearest catalogued parent (GtkApplicationWindow → its own,
// a subclass of GtkBox → GtkBox's); nullptr for anything else.
const ui::ClassInfo* catalogInfo(const std::string& cls);
// Whether a class is a window (it can only be a top-level object).
bool isWindowClass(const std::string& cls);
// What the code generators need, from this process's GType system and the gtkmm headers installed.
const ui::TypeInfo& gtkTypes();

} // namespace kevc::designer
