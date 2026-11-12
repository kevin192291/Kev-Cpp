---
order: 32
section: dialogs
description: A Gtk::FileDialog that asks where to save, then writes the text to the chosen file
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a member function of a Gtk::Window. The dialog doesn't block: the lambda runs once a name is chosen,
// so it takes a copy of what to save.
const std::string contents = "Text to save\n";

auto dialog = Gtk::FileDialog::create();
dialog->set_title("Save As");
dialog->set_initial_name("untitled.txt");
dialog->save(*this, [dialog, contents](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try
    {
        const Glib::RefPtr<Gio::File> file = dialog->save_finish(result);
        // Writes a temporary file and renames it over the old one, so a failed save loses nothing.
        Glib::file_set_contents(file->get_path(), contents);
        {{cursor}}
    }
    catch (const Gtk::DialogError&)
    {
        // Cancelled.
    }
    catch (const Glib::Error& error)
    {
        auto alert = Gtk::AlertDialog::create("The file can't be saved");
        alert->set_detail(error.what());
        alert->show();
    }
});
