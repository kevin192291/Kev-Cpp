#pragma once

#include "core/Lsp.h"

#include <gtkmm.h>

#include <vector>

namespace kevc {

// The "Classes" tab: the current file's symbols (from textDocument/documentSymbol) as an
// indented list. Activating a row reports its LSP position.
class SymbolsPanel : public Gtk::ScrolledWindow {
public:
    SymbolsPanel();

    void setSymbols(const lsp::json& symbols);
    void showMessage(const Glib::ustring& text);

    // line and UTF-16 column, both 0-based.
    sigc::signal<void(int line, int utf16)>& signalActivated() { return activated_; }

private:
    void addSymbol(const lsp::json& symbol, int depth);

    Gtk::Box box_{Gtk::Orientation::VERTICAL};
    Gtk::Label message_;
    Gtk::ListBox list_;
    std::vector<std::pair<int, int>> positions_;
    sigc::signal<void(int, int)> activated_;
};

} // namespace kevc
