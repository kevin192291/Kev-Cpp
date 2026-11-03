#pragma once

#include <gtkmm.h>

#include <functional>
#include <vector>

namespace kevc {

// The message boxes below are MessageDialog.ui, Kev-C++'s one design for them.

// Shows a modal window with one text entry. `onAccept` returns an error message to keep the
// window open, or an empty string to close it. The entry starts with the file stem of `initial`
// selected, or its first `selectChars` characters, or (-2) all of it.
void promptText(Gtk::Window& parent, const Glib::ustring& title, const Glib::ustring& label,
                const Glib::ustring& initial, std::function<Glib::ustring(const Glib::ustring&)> onAccept,
                int selectChars = -1);

// Asks a question with several buttons; `onChoice` receives the button index. Escape or the
// window's close button choose `cancelIndex` (-1 if there's none).
void askChoice(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail,
               const std::vector<Glib::ustring>& buttons, int cancelIndex, int defaultIndex,
               std::function<void(int)> onChoice);

void showError(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail);
// The same box for news that isn't a problem ("Kev-C++ is up to date").
void showInfo(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail);

// For Kev-C++'s own windows: Escape closes `window` as its close button does.
void closeOnEscape(Gtk::Window& window);
// Hides `window` when it's closed, and then deletes it (once GTK is done with it).
void deleteWhenHidden(Gtk::Window* window);

} // namespace kevc
