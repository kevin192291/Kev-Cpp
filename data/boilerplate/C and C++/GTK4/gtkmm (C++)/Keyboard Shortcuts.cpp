---
order: 40
section: behaviour
description: Window-wide shortcuts with a ShortcutController, and raw key presses with an EventControllerKey
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. Shortcuts that belong to a menu item are better as actions with
// app->set_accel_for_action("win.save", "<Primary>s"), so the menu shows the key too.

// A key combination that works wherever the focus is in the window. <Primary> is Ctrl (Cmd on macOS).
auto shortcuts = Gtk::ShortcutController::create();
shortcuts->set_scope(Gtk::ShortcutScope::GLOBAL);
shortcuts->add_shortcut(Gtk::Shortcut::create(
    Gtk::ShortcutTrigger::parse_string("<Primary>r"),
    Gtk::CallbackAction::create([](Gtk::Widget&, const Glib::VariantBase&) {
        {{cursor}}
        return true; // handled
    })));
add_controller(shortcuts);

// Every key press that reaches the window, e.g. for a game or a custom widget.
auto keys = Gtk::EventControllerKey::create();
keys->signal_key_pressed().connect(
    [](guint keyval, guint /*keycode*/, Gdk::ModifierType state) {
        const bool ctrl = (state & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
        if (keyval == GDK_KEY_Escape || (ctrl && keyval == GDK_KEY_w))
        {
            return true; // handled: nothing else sees the key
        }
        return false; // let the focused widget have it
    },
    false);
add_controller(keys);
