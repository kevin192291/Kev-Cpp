#pragma once

#include "core/ProjectSession.h"

#include <gtkmm.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace kevc {

// Shows a modal window with one text entry. `onAccept` returns an error message to keep the
// window open, or an empty string to close it.
void promptText(Gtk::Window& parent, const Glib::ustring& title, const Glib::ustring& label,
                const Glib::ustring& initial, std::function<Glib::ustring(const Glib::ustring&)> onAccept);

// Asks a question with several buttons; `onChoice` receives the button index, or -1 if dismissed.
void askChoice(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail,
               const std::vector<Glib::ustring>& buttons, int cancelIndex, int defaultIndex,
               std::function<void(int)> onChoice);

void showError(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail);
// The same dialog for news that isn't a problem ("Kev-C++ is up to date").
void showInfo(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail);

class NewProjectDialog : public Gtk::Window {
public:
    using CreateHandler =
        std::function<Glib::ustring(const std::string& name, const std::filesystem::path& location, ProjectTemplate)>;

    NewProjectDialog(Gtk::Window& parent, const std::filesystem::path& defaultLocation, CreateHandler onCreate);

private:
    void browse();
    void submit();

    CreateHandler onCreate_;
    Gtk::Box content_{Gtk::Orientation::VERTICAL, 8};
    Gtk::ListBox templates_;
    Gtk::Entry name_;
    Gtk::Entry location_;
    Gtk::Label error_;
};

} // namespace kevc
