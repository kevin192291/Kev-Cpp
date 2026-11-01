#pragma once

#include <gtkmm.h>

#include <string>

namespace kevc {

// The start-up splash, as Dev-C++ shows one: the logo, "Kev-C++ Pro", and the version in the
// bottom right corner. It sits over the main window while that settles (a window of its own,
// transient for the main one, so window managers centre it there), then closes itself; a click or a
// key closes it at once. It deletes itself.
class SplashWindow : public Gtk::Window {
public:
    SplashWindow(Gtk::Window& parent, const std::string& version, unsigned showMs = 2200);

private:
    Gtk::Label status_;
};

} // namespace kevc
