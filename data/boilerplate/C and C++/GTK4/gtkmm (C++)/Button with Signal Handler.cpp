---
order: 10
section: widgets
description: A button whose click runs a lambda; the code goes in a window's constructor
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. make_managed hands the button to its parent, which deletes it.
auto* button = Gtk::make_managed<Gtk::Button>("_Save", true); // true: the underscore makes Alt+S press it
button->set_tooltip_text("Save the file");
button->signal_clicked().connect([button] {
    button->set_label("Saved");
    {{cursor}}
});
set_child(*button); // or append it to a Gtk::Box
// A member function as the handler: button->signal_clicked().connect(sigc::mem_fun(*this, &MyWindow::on_save));
