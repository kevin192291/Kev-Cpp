#pragma once

#include "build/CompilerPanel.ui.h"

#include "core/Diagnostics.h"

#include <vector>

namespace kevc {

// The "Compiler" tab: the errors, warnings and notes of the last build. Activating a row
// (double-click or Enter) reports its diagnostic so the editor can jump to it. Its layout is
// CompilerPanel.ui (edit it on Kev-C++'s Design page); this class adds a row per diagnostic.
class CompilerPanel : public CompilerPanelUi {
public:
    static Glib::RefPtr<CompilerPanel> create() { return CompilerPanelUi::create<CompilerPanel>(); }
    CompilerPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    void clear();
    void add(const Diagnostic& d);
    // Shown while the list is empty, e.g. "Building…" or "Build succeeded with no errors or warnings."
    void setEmptyText(const Glib::ustring& text);

    sigc::signal<void(const Diagnostic&)>& signalActivated() { return activated_; }

private:
    void updateVisibility();

    std::vector<Diagnostic> diagnostics_;
    sigc::signal<void(const Diagnostic&)> activated_;
};

} // namespace kevc
