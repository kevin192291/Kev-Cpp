#pragma once

#include <gtksourceview/gtksource.h>

#include <string>

namespace kevc {

class EditorTab;
class LspManager;

// GtkSourceView providers backed by the language servers. Both return a new reference.
GtkSourceCompletionProvider* newLspCompletionProvider(LspManager* manager, EditorTab* tab);
GtkSourceHoverProvider* newLspHoverProvider(LspManager* manager, EditorTab* tab);

// Pango markup for an LSP CompletionItemKind / SymbolKind badge, e.g. a purple "f" for functions.
std::string completionKindBadge(int lspCompletionKind);
std::string symbolKindBadge(int lspSymbolKind);

} // namespace kevc
