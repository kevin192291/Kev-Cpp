#pragma once

#include "ui/AboutWindow.ui.h"

#include <string>

namespace kevc {

// Help → About: the logo, the name, the version and a link to the project. Its layout is
// AboutWindow.ui (edit it on Kev-C++'s Design page); this class adds the version. It deletes itself
// when closed.
class AboutWindow : public AboutWindowUi {
public:
    static AboutWindow* create(Gtk::Window& parent, const std::string& version) {
        return AboutWindowUi::create<AboutWindow>(parent, version);
    }
    AboutWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                const std::string& version);

protected:
    void on_close_clicked() override;
};

} // namespace kevc
