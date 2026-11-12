---
order: 13
section: widgets
description: A small form: entries, a password field and a check box, checked when Sign In is pressed
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. The status label reports what's wrong, or that it worked.
auto* form = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
form->set_margin(12);

auto* user = Gtk::make_managed<Gtk::Entry>();
user->set_placeholder_text("User name");
auto* password = Gtk::make_managed<Gtk::PasswordEntry>();
password->set_show_peek_icon(true);
auto* remember = Gtk::make_managed<Gtk::CheckButton>("_Remember me", true);
auto* signIn = Gtk::make_managed<Gtk::Button>("Sign In");
signIn->add_css_class("suggested-action");
auto* status = Gtk::make_managed<Gtk::Label>();
status->set_xalign(0);

form->append(*user);
form->append(*password);
form->append(*remember);
form->append(*signIn);
form->append(*status);
set_child(*form);

signIn->signal_clicked().connect([user, password, remember, status] {
    if (user->get_text().empty() || password->get_text().empty())
    {
        status->set_text("Enter a user name and a password.");
        return;
    }
    status->set_text("Signed in as " + user->get_text() + (remember->get_active() ? " (remembered)" : ""));
    {{cursor}}
});
user->signal_changed().connect([status] { status->set_text(""); });

// Enter in either field presses the window's default button.
set_default_widget(*signIn);
user->set_activates_default(true);
password->property_activates_default() = true;
