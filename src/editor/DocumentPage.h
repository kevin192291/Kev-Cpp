#pragma once

#include <gtkmm.h>

#include <filesystem>

namespace kevc {

// A page in the editor area that edits one file (a text editor, the GTK designer). MainWindow's
// saving, closing and unsaved-changes questions go through this, whatever the page is.
class DocumentPage {
public:
    virtual ~DocumentPage() = default;

    virtual const std::filesystem::path& path() const = 0;
    virtual bool modified() const = 0;
    // Throws Glib::Error on failure.
    virtual void save() = 0;
    // The file was renamed or moved (from the Project tab): keep editing it, and save it, there.
    virtual void moveTo(const std::filesystem::path& file) = 0;

    virtual Gtk::Widget& pageWidget() = 0;
    virtual Gtk::Widget& tabLabel() = 0;
    virtual void focusPage() = 0;
    // The Edit menu: "text.undo", "text.redo", "clipboard.cut", "clipboard.copy", "clipboard.paste",
    // "selection.select-all" (GtkTextView's action names).
    virtual void activateTextAction(const char* action) = 0;

    virtual sigc::signal<void()>& signalCloseRequested() = 0;
};

} // namespace kevc
