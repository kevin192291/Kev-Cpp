---
order: 21
section: behaviour
description: A function that makes a clock label updated every second by a timer that stops with its window
packages: gtk4
includes: <gtk/gtk.h>
---
// A label showing the time, updated every second. Make it in activate():
//   gtk_box_append(GTK_BOX(box), create_clock_label());
static void update_clock_label(GtkWidget* label)
{
    GDateTime* now = g_date_time_new_now_local();
    char* text = g_date_time_format(now, "%H:%M:%S");
    gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
    g_date_time_unref(now);
}

// Runs on the GTK main loop, so it may change widgets. G_SOURCE_REMOVE stops the timer.
static gboolean on_clock_tick(gpointer user_data)
{
    GtkWidget* label = user_data;
    if (gtk_widget_get_root(label) == NULL)
    {
        return G_SOURCE_REMOVE; // the label has left its window: the window was closed
    }
    update_clock_label(label);
    {{cursor}}
    return G_SOURCE_CONTINUE;
}

static GtkWidget* create_clock_label(void)
{
    GtkWidget* label = gtk_label_new(NULL);
    update_clock_label(label);
    // The timer holds a reference to the label, dropped when the timer stops, so it never sees a freed one.
    // g_timeout_add_full() takes milliseconds instead, for faster timers.
    g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 1, on_clock_tick, g_object_ref(label), g_object_unref);
    return label;
}
