---
order: 12
section: widgets
description: A grid of labels and fields in rows and columns, with a button spanning both columns
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. attach(widget, column, row, width, height) places a widget in the grid.
auto* grid = Gtk::make_managed<Gtk::Grid>();
grid->set_margin(12);
grid->set_row_spacing(6);
grid->set_column_spacing(12);

const char* labels[] = {"_Name", "_Email", "_Phone"};
for (int row = 0; row < 3; ++row)
{
    auto* label = Gtk::make_managed<Gtk::Label>(labels[row], true);
    label->set_xalign(1); // right-aligned against its field
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_hexpand(true); // the field column takes the extra width
    label->set_mnemonic_widget(*entry); // Alt+N, Alt+E, Alt+P jump to the field
    grid->attach(*label, 0, row);
    grid->attach(*entry, 1, row);
}

auto* add = Gtk::make_managed<Gtk::Button>("Add Contact");
grid->attach(*add, 0, 3, 2, 1); // two columns wide
{{cursor}}
set_child(*grid);
