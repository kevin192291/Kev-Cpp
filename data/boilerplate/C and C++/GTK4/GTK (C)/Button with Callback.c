---
order: 10
section: widgets
description: A function that makes a button counting its clicks, with the callback it calls
packages: gtk4
includes: <gtk/gtk.h>
---
// A button that counts its clicks. Make it in activate():
//   gtk_box_append(GTK_BOX(box), create_counter_button());
static void on_counter_clicked(GtkButton* button, gpointer user_data)
{
    int* count = user_data;
    ++*count;
    char text[32];
    g_snprintf(text, sizeof text, "Clicked %d times", *count);
    gtk_button_set_label(button, text);
    {{cursor}}
}

static GtkWidget* create_counter_button(void)
{
    GtkWidget* button = gtk_button_new_with_mnemonic("_Click Me"); // Alt+C presses it
    gtk_widget_set_tooltip_text(button, "Counts your clicks");

    // The count lives as long as the button: set_data_full frees it when the button is destroyed.
    int* count = g_new0(int, 1);
    g_object_set_data_full(G_OBJECT(button), "click-count", count, g_free);
    g_signal_connect(button, "clicked", G_CALLBACK(on_counter_clicked), count);
    return button;
}
