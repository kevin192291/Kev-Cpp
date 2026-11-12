#pragma once

#include <gtksourceview/gtksource.h>

#include <functional>
#include <string>

namespace kevc {

class EditorTab;
class LspManager;

// GtkSourceView providers backed by the language servers. Both return a new reference.
GtkSourceCompletionProvider* newLspCompletionProvider(LspManager* manager, EditorTab* tab);
// Over an error or warning that has possible fixes, the hover also shows a "Show possible fixes"
// button, which closes it and calls `showFixes` with the place hovered.
GtkSourceHoverProvider* newLspHoverProvider(LspManager* manager, EditorTab* tab,
                                            std::function<void(const GtkTextIter& at)> showFixes);

} // namespace kevc
