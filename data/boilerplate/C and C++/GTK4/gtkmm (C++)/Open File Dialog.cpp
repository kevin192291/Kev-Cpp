---
order: 31
section: dialogs
description: A Gtk::FileDialog that lets the user pick a text file, then reads it
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a member function of a Gtk::Window. The dialog doesn't block: the lambda runs once a file is chosen.
auto dialog = Gtk::FileDialog::create();
dialog->set_title("Open File");

auto textFiles = Gtk::FileFilter::create();
textFiles->set_name("Text files");
textFiles->add_mime_type("text/plain");
auto allFiles = Gtk::FileFilter::create();
allFiles->set_name("All files");
allFiles->add_pattern("*");
auto filters = Gio::ListStore<Gtk::FileFilter>::create();
filters->append(textFiles);
filters->append(allFiles);
dialog->set_filters(filters);

dialog->open(*this, [dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
    try
    {
        const Glib::RefPtr<Gio::File> file = dialog->open_finish(result);
        const std::string text = Glib::file_get_contents(file->get_path());
        {{cursor}}
    }
    catch (const Gtk::DialogError&)
    {
        // Cancelled.
    }
    catch (const Glib::Error& error)
    {
        auto alert = Gtk::AlertDialog::create("The file can't be opened");
        alert->set_detail(error.what());
        alert->show();
    }
});
