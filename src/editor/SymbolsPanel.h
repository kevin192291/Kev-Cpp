#pragma once

#include "editor/SymbolsPanel.ui.h"

#include "core/Lsp.h"

#include <vector>

namespace kevc {

// The "Classes" tab: the current file's symbols (from textDocument/documentSymbol) as an
// indented list. Activating a row reports its LSP position. Its layout is SymbolsPanel.ui (edit it on
// Kev-C++'s Design page); this class adds a row per symbol.
class SymbolsPanel : public SymbolsPanelUi {
public:
    static Glib::RefPtr<SymbolsPanel> create() { return SymbolsPanelUi::create<SymbolsPanel>(); }
    SymbolsPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    void setSymbols(const lsp::json& symbols);
    void showMessage(const Glib::ustring& text);

    // line and UTF-16 column, both 0-based.
    sigc::signal<void(int line, int utf16)>& signalActivated() { return activated_; }

private:
    // `parentKind`: the enclosing symbol's kind (0 at the top), which tells locals from globals.
    void addSymbol(const lsp::json& symbol, int depth, int parentKind);

    std::vector<std::pair<int, int>> positions_;
    sigc::signal<void(int, int)> activated_;
};

} // namespace kevc
