---
order: 11
section: widgets
description: A vertical box with a heading, a text area that takes the spare room, and right-aligned buttons
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. A box lines its children up; hexpand/vexpand say who takes the spare room.
auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6); // 6 px between children
column->set_margin(12);

auto* heading = Gtk::make_managed<Gtk::Label>();
heading->set_markup("<b>Notes</b>");
heading->set_xalign(0);
column->append(*heading);

auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
scroller->set_child(*Gtk::make_managed<Gtk::TextView>());
scroller->set_vexpand(true);
scroller->set_has_frame(true);
column->append(*scroller);

auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
buttons->set_halign(Gtk::Align::END);
auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
auto* ok = Gtk::make_managed<Gtk::Button>("OK");
ok->add_css_class("suggested-action"); // the theme's colour for the main button
buttons->append(*cancel);
buttons->append(*ok);
column->append(*buttons);
{{cursor}}
set_child(*column);
