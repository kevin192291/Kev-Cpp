---
order: 21
section: lists
description: A Gtk::DropDown of strings that runs code when the choice changes
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. Gtk::DropDown replaces the old Gtk::ComboBoxText.
auto* sizes = Gtk::make_managed<Gtk::DropDown>(std::vector<Glib::ustring>{"Small", "Medium", "Large"});
sizes->set_selected(1); // positions count from 0
sizes->property_selected().signal_changed().connect([sizes] {
    auto chosen = std::dynamic_pointer_cast<Gtk::StringObject>(sizes->get_selected_item());
    if (chosen)
    {
        const Glib::ustring size = chosen->get_string();
        {{cursor}}
    }
});
set_child(*sizes); // or append it to a Gtk::Box
