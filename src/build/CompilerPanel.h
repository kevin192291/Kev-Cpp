#pragma once

#include "core/Diagnostics.h"

#include <gtkmm.h>

#include <vector>

namespace kevc {

// The "Compiler" tab: the errors, warnings and notes of the last build. Activating a row
// (double-click or Enter) reports its diagnostic so the editor can jump to it.
class CompilerPanel : public Gtk::ScrolledWindow {
public:
    CompilerPanel();

    void clear();
    void add(const Diagnostic& d);
    // Shown while the list is empty, e.g. "Building…" or "Build succeeded with no errors or warnings."
    void setEmptyText(const Glib::ustring& text);

    sigc::signal<void(const Diagnostic&)>& signalActivated() { return activated_; }

private:
    void updateVisibility();

    Gtk::Box box_{Gtk::Orientation::VERTICAL};
    Gtk::Label message_;
    Gtk::ListBox list_;
    std::vector<Diagnostic> diagnostics_;
    sigc::signal<void(const Diagnostic&)> activated_;
};

} // namespace kevc
