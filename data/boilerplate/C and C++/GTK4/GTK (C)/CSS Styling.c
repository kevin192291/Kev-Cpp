---
order: 20
section: behaviour
description: A function that loads CSS for the whole application; widgets pick styles up through classes
packages: gtk4
includes: <gtk/gtk.h>
---
// Loads CSS for the whole application; call it once from activate(). GTK's CSS selects widgets by node
// name (button, label, entry, window) and by class:  gtk_widget_add_css_class(label, "title");
// The theme's own classes: suggested-action, destructive-action, dim-label, flat, pill, monospace.
static void load_css(void)
{
    static const char css[] =
        ".title { font-size: 18pt; font-weight: bold; }\n"
        ".warning { color: #c01c28; }\n"
        "button.big { padding: 12px 24px; border-radius: 12px; }\n";

    GtkCssProvider* provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
#endif
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider); // the display keeps its own reference
    {{cursor}}
}
