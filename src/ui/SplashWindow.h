#pragma once

#include "ui/SplashWindow.ui.h"

#include <string>

namespace kevc {

// The start-up splash, as Dev-C++ shows one: the logo, "Kev-C++ Pro", and the version in the
// bottom right corner. Its layout is SplashWindow.ui (edit it on Kev-C++'s Design page); this class
// adds the version and how it goes away. It sits over the main window while that settles (a window
// of its own, transient for the main one, so window managers centre it there), then closes itself;
// a click or a key closes it at once. It deletes itself.
class SplashWindow : public SplashWindowUi {
public:
    // Builds it from the design.
    static SplashWindow* create(Gtk::Window& parent, const std::string& version, unsigned showMs = 2200) {
        return SplashWindowUi::create<SplashWindow>(parent, version, showMs);
    }
    SplashWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                 const std::string& version, unsigned showMs);
};

} // namespace kevc
