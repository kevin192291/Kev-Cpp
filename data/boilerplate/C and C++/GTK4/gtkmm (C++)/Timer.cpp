---
order: 41
section: behaviour
description: A timer that updates a label every second and stops by itself when the label is destroyed
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. The handler runs on the GTK main loop, so it may change widgets; returning
// false stops it. track_object ties it to the label: the timer is disconnected if the label is destroyed first.
auto* elapsed = Gtk::make_managed<Gtk::Label>("0 s");
set_child(*elapsed); // or append it to a Gtk::Box

Glib::signal_timeout().connect_seconds(
    sigc::track_object(
        [elapsed, seconds = 0]() mutable {
            ++seconds;
            elapsed->set_text(std::to_string(seconds) + " s");
            {{cursor}}
            return true; // keep running
        },
        *elapsed),
    1);
// To stop it from elsewhere, keep what connect_seconds() returns in a sigc::connection member and call its
// disconnect().
// Faster: Glib::signal_timeout().connect(handler, 100) runs every 100 ms. For animation use the widget's
// add_tick_callback(), which runs once per frame the screen draws.
