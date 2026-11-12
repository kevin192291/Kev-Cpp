---
order: 42
section: behaviour
description: Loads CSS for the whole application and gives widgets classes to style them
packages: gtkmm-4.0
includes: <gtkmm.h>
---
// Once at start-up, e.g. in a window's constructor. GTK's CSS selects widgets by node name (button, label,
// entry, window) and by class (.title); see the GTK docs' "CSS Properties" for what can be set.
auto css = Gtk::CssProvider::create();
css->load_from_data(R"(
    .title {
        font-size: 18pt;
        font-weight: bold;
    }
    .warning {
        color: #c01c28;
    }
    button.big {
        padding: 12px 24px;
        border-radius: 12px;
    }
)");
Gtk::StyleProvider::add_provider_for_display(Gdk::Display::get_default(), css,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
{{cursor}}
// Then: label->add_css_class("title");  button->add_css_class("big");
// The theme's own classes: suggested-action, destructive-action, dim-label, flat, pill, monospace.
