#include "ui/AboutWindow.h"

#include "ui/Dialogs.h"

namespace kevc {

AboutWindow::AboutWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                         const std::string& version)
    : AboutWindowUi(object, builder) {
    set_transient_for(parent);
    // The commit it was built from: date, time and hash.
    version_->set_text("Version " + version);
    closeOnEscape(*this);
    deleteWhenHidden(this);
    close_->grab_focus();
}

void AboutWindow::on_close_clicked() {
    set_visible(false);
}

} // namespace kevc
