#include "ui/ToolchainWindow.h"

#include "ui/Dialogs.h"

namespace kevc {

ToolchainWindow::ToolchainWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                                 const Toolchain& toolchain)
    : ToolchainWindowUi(object, builder) {
    set_transient_for(parent);
    // Row 0 holds the design's headings.
    int row = 1;
    for (const ToolInfo& t : toolchain.tools()) {
        auto* name = Gtk::make_managed<Gtk::Label>(t.name);
        name->set_xalign(0.0f);
        name->set_valign(Gtk::Align::START);
        name->set_tooltip_text(Glib::ustring(t.required ? "Required: " : "Optional: ") + t.purpose);
        auto* kind = Gtk::make_managed<Gtk::Label>(t.required ? "required" : "optional");
        kind->set_xalign(0.0f);
        kind->add_css_class("dim-label");
        kind->add_css_class("caption");
        auto* nameBox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        nameBox->append(*name);
        nameBox->append(*kind);

        auto* where = Gtk::make_managed<Gtk::Label>();
        where->set_xalign(0.0f);
        where->set_valign(Gtk::Align::START);
        where->set_wrap(true);
        where->set_selectable(true);
        if (t.found()) {
            where->set_text(t.path->string());
        } else {
            // What it's for says what's missing without it.
            where->set_text("Not found: " + t.purpose);
            if (t.required) where->add_css_class("error");
            else where->add_css_class("dim-label");
        }
        auto* version = Gtk::make_managed<Gtk::Label>(t.version);
        version->set_xalign(0.0f);
        version->set_valign(Gtk::Align::START);
        version->set_wrap(true);
        version->set_max_width_chars(30);
        version->set_selectable(true);
        version->add_css_class("dim-label");

        tools_->attach(*nameBox, 0, row);
        tools_->attach(*where, 1, row);
        tools_->attach(*version, 2, row);
        ++row;
    }
    closeOnEscape(*this);
    deleteWhenHidden(this);
    close_->grab_focus();
}

void ToolchainWindow::on_close_clicked() {
    set_visible(false);
}

} // namespace kevc
