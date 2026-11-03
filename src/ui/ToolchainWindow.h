#pragma once

#include "ui/ToolchainWindow.ui.h"

#include "core/Toolchain.h"

namespace kevc {

// Tools → Toolchain Info: each program Kev-C++ uses, where it was found and its version. Its layout
// is ToolchainWindow.ui (edit it on Kev-C++'s Design page); this class fills in the rows, one per
// tool, below the design's headings. It deletes itself when closed.
class ToolchainWindow : public ToolchainWindowUi {
public:
    static ToolchainWindow* create(Gtk::Window& parent, const Toolchain& toolchain) {
        return ToolchainWindowUi::create<ToolchainWindow>(parent, toolchain);
    }
    ToolchainWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                    const Toolchain& toolchain);

protected:
    void on_close_clicked() override;
};

} // namespace kevc
