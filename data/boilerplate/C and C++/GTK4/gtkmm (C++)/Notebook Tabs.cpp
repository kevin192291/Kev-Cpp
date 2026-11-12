---
order: 16
section: widgets
description: A Gtk::Notebook whose tabs each have a close button
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// In a window's constructor. Each tab is a label and a close button; closing removes (and deletes) the page.
auto* notebook = Gtk::make_managed<Gtk::Notebook>();
notebook->set_scrollable(true); // arrows instead of squeezing when there are many tabs

auto addPage = [notebook](const Glib::ustring& title, Gtk::Widget& content) {
    auto* tab = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    tab->append(*Gtk::make_managed<Gtk::Label>(title));
    auto* close = Gtk::make_managed<Gtk::Button>();
    close->set_icon_name("window-close-symbolic");
    close->set_has_frame(false);
    close->set_tooltip_text("Close " + title);
    close->signal_clicked().connect([notebook, page = &content] { notebook->remove_page(*page); });
    tab->append(*close);

    notebook->append_page(content, *tab);
    notebook->set_tab_reorderable(content, true);
};
addPage("First", *Gtk::make_managed<Gtk::Label>("The first page"));
addPage("Second", *Gtk::make_managed<Gtk::Label>("The second page"));
{{cursor}}
set_child(*notebook);
