#pragma once

#include <gtksourceview/gtksource.h>

namespace kevc {

// The C/C++ editors' indenter (core/CppStyle does the deciding): on Return the new line gets its
// level; '{' after a header moves to a line of its own; public:/private:/protected: snap to the
// class's indentation; a '}' starting a line lines up with its '{', and one closing a class, struct,
// union or enum body gets its ';'. Returns a new reference.
GtkSourceIndenter* newCppIndenter();

// Is this one of GtkSourceView's C or C++ languages (source or header)?
bool isCppLanguage(GtkSourceLanguage* language);

} // namespace kevc
