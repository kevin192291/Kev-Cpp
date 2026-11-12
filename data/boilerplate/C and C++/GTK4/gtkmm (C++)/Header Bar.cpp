---
order: 14
section: widgets
description: A header bar title bar with an Open button and a menu button holding app actions
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. A header bar replaces the title bar; buttons go at its start or end.
auto* header = Gtk::make_managed<Gtk::HeaderBar>();

auto* open = Gtk::make_managed<Gtk::Button>("_Open", true);
open->set_tooltip_text("Open a file");
open->signal_clicked().connect([] {
    {{cursor}}
});
header->pack_start(*open);

// The menu's items run actions: add them with add_action() on the application ("app.") or window ("win.").
auto menu = Gio::Menu::create();
menu->append("_Preferences", "app.preferences");
menu->append("_About", "app.about");
menu->append("_Quit", "app.quit");
auto* menuButton = Gtk::make_managed<Gtk::MenuButton>();
menuButton->set_icon_name("open-menu-symbolic");
menuButton->set_tooltip_text("Main menu");
menuButton->set_menu_model(menu);
header->pack_end(*menuButton);

set_titlebar(*header);
