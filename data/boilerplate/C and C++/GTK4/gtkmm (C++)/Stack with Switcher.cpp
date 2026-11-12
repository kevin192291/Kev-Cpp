---
order: 15
section: widgets
description: Pages in a Gtk::Stack with a row of buttons (StackSwitcher) that switches between them
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. A stack shows one page at a time; the switcher makes a button per page.
auto* stack = Gtk::make_managed<Gtk::Stack>();
stack->set_transition_type(Gtk::StackTransitionType::SLIDE_LEFT_RIGHT);
stack->set_vexpand(true);
stack->add(*Gtk::make_managed<Gtk::Label>("General settings"), "general", "General");
stack->add(*Gtk::make_managed<Gtk::Label>("Appearance settings"), "appearance", "Appearance");
stack->add(*Gtk::make_managed<Gtk::Label>("Advanced settings"), "advanced", "Advanced");
{{cursor}}
auto* switcher = Gtk::make_managed<Gtk::StackSwitcher>();
switcher->set_stack(*stack);
switcher->set_halign(Gtk::Align::CENTER);

auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
column->set_margin(12);
column->append(*switcher);
column->append(*stack);
set_child(*column);
// Show a page from code: stack->set_visible_child("advanced");
// A sidebar list instead of buttons: Gtk::StackSidebar with set_stack().
