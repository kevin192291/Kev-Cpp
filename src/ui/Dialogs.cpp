#include "ui/Dialogs.h"

#include "ui/MessageDialog.h"

namespace kevc {

void closeOnEscape(Gtk::Window& window) {
    auto shortcuts = Gtk::ShortcutController::create();
    shortcuts->add_shortcut(
        Gtk::Shortcut::create(Gtk::KeyvalTrigger::create(GDK_KEY_Escape), Gtk::NamedAction::create("window.close")));
    window.add_controller(shortcuts);
}

void deleteWhenHidden(Gtk::Window* window) {
    // Hidden rather than destroyed by its close button, so this object stays the window's owner.
    window->set_hide_on_close(true);
    window->signal_hide().connect([window] { Glib::signal_idle().connect_once([window] { delete window; }); });
}

void promptText(Gtk::Window& parent, const Glib::ustring& title, const Glib::ustring& label,
                const Glib::ustring& initial, std::function<Glib::ustring(const Glib::ustring&)> onAccept,
                int selectChars) {
    auto* dialog = MessageDialog::create(&parent, MessageDialog::Kind::Prompt, label, "");
    dialog->set_title(title);
    Gtk::Entry& entry = dialog->entry();
    entry.set_text(initial);
    dialog->setButtons({"Cancel", "OK"}, 0, 1, [dialog, &entry, onAccept](int choice) {
        if (choice != 1) return true;
        const Glib::ustring problem = onAccept(entry.get_text());
        dialog->showProblem(problem);
        return problem.empty();
    });
    dialog->open();
    if (selectChars == -2) {
        entry.select_region(0, -1); // all of it
        return;
    }
    if (selectChars >= 0) {
        entry.select_region(0, selectChars);
        return;
    }
    // Select the file stem so typing replaces "untitled" but keeps the extension.
    const auto dot = initial.rfind('.');
    const auto slash = initial.rfind('/');
    const int start = slash == Glib::ustring::npos ? 0 : static_cast<int>(slash + 1);
    entry.select_region(start,
                        dot == Glib::ustring::npos || dot < static_cast<size_t>(start) ? -1 : static_cast<int>(dot));
}

void askChoice(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail,
               const std::vector<Glib::ustring>& buttons, int cancelIndex, int defaultIndex,
               std::function<void(int)> onChoice) {
    auto* dialog = MessageDialog::create(&parent, MessageDialog::Kind::Question, message, detail);
    dialog->setButtons(buttons, cancelIndex, defaultIndex, [onChoice](int choice) {
        // After the box has gone: a handler that opens another box, or closes the main window,
        // shouldn't do it under this one.
        Glib::signal_idle().connect_once([onChoice, choice] { onChoice(choice); });
        return true;
    });
    dialog->open();
}

void showError(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail) {
    auto* dialog = MessageDialog::create(&parent, MessageDialog::Kind::Error, message, detail);
    dialog->setButtons({"Close"}, 0, 0, nullptr);
    dialog->open();
}

void showInfo(Gtk::Window& parent, const Glib::ustring& message, const Glib::ustring& detail) {
    auto* dialog = MessageDialog::create(&parent, MessageDialog::Kind::Info, message, detail);
    dialog->setButtons({"OK"}, 0, 0, nullptr);
    dialog->open();
}

} // namespace kevc
