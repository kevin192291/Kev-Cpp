---
order: 30
section: dialogs
description: A Gtk::AlertDialog that asks Save, Discard or Cancel and runs code for the button chosen
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a member function of a Gtk::Window. The box doesn't block: the lambda runs once a button is chosen
// (capture this as well to call the window's members from it).
// A plain message is one line:  Gtk::AlertDialog::create("Saved.")->show(*this);
auto dialog = Gtk::AlertDialog::create("Save changes before closing?");
dialog->set_detail("If you don't save, your changes will be lost.");
dialog->set_buttons({"Cancel", "Discard", "Save"});
dialog->set_cancel_button(0);  // Escape and the window's close button choose Cancel
dialog->set_default_button(2); // Enter chooses Save
dialog->choose(*this, [dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
    int button = 0;
    try
    {
        button = dialog->choose_finish(result);
    }
    catch (const Gtk::DialogError&)
    {
        return; // dismissed some other way
    }
    switch (button)
    {
    case 1: // Discard
        break;
    case 2: // Save
        {{cursor}}
        break;
    default: // Cancel
        break;
    }
});
