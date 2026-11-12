---
order: 20
section: lists
description: A scrolling Gtk::ListView of strings with one selected row; double-click or Enter activates a row
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. The model holds the data, the factory makes and fills a row widget for each
// visible item (rows are reused while scrolling), and the selection model tracks the selected row.
auto items = Gtk::StringList::create({"Apples", "Bananas", "Cherries", "Dates"});
auto selection = Gtk::SingleSelection::create(items);

auto factory = Gtk::SignalListItemFactory::create();
factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* label = Gtk::make_managed<Gtk::Label>();
    label->set_xalign(0);
    item->set_child(*label);
});
factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
    auto* label = dynamic_cast<Gtk::Label*>(item->get_child());
    auto string = std::dynamic_pointer_cast<Gtk::StringObject>(item->get_item());
    if (label && string)
    {
        label->set_text(string->get_string());
    }
});

auto* list = Gtk::make_managed<Gtk::ListView>(selection, factory);
list->signal_activate().connect([items](guint position) {
    const Glib::ustring chosen = items->get_string(position);
    {{cursor}}
});

auto* scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
scroller->set_child(*list);
scroller->set_vexpand(true);
set_child(*scroller);
// Change the list and the view follows: items->append("Elderberries"); items->remove(0);
// The selected row: selection->get_selected() (GTK_INVALID_LIST_POSITION when none).
